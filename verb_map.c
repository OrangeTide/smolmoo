/* verb_map.c : draw a small ASCII map of the current room. The box is sized by
 * the room's mapw/maph (a default when unset), with an opening in the wall at
 * each exit's `dir` tag (n/s/e/w and the diagonals as wall gaps, u/d as the
 * roguelike < and > inside). A legend under the box ties each direction to its
 * exit name. This is the read-only first cut; fixed-object and character
 * overlays follow in later steps. (ChromeSix.) */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

#define MAP_W_DEF 9
#define MAP_H_DEF 5
#define MAP_W_MAX 39
#define MAP_H_MAX 19

/* The full compass/vertical word for a dir tag, or 0 when it is not a dir. */
static const char *
dir_word(const char *d)
{
    if (cs_streq(d, "n"))  return "north";
    if (cs_streq(d, "s"))  return "south";
    if (cs_streq(d, "e"))  return "east";
    if (cs_streq(d, "w"))  return "west";
    if (cs_streq(d, "ne")) return "northeast";
    if (cs_streq(d, "nw")) return "northwest";
    if (cs_streq(d, "se")) return "southeast";
    if (cs_streq(d, "sw")) return "southwest";
    if (cs_streq(d, "u"))  return "up";
    if (cs_streq(d, "d"))  return "down";
    return 0;
}

/* The glyph that marks a cardinal exit on the wall: its direction letter, so
 * the opening is labelled and the border keeps a character (the edges stay
 * aligned). Diagonals use a corner slash; u/d are handled separately inside. */
static char
dir_glyph(const char *d)
{
    if (cs_streq(d, "n")) return 'n';
    if (cs_streq(d, "s")) return 's';
    if (cs_streq(d, "e")) return 'e';
    if (cs_streq(d, "w")) return 'w';
    if (cs_streq(d, "ne") || cs_streq(d, "sw")) return '/';
    if (cs_streq(d, "nw") || cs_streq(d, "se")) return '\\';
    return '+';
}

/* Pick a random free interior cell (a ' ' not taken by a wall, marker, feature
 * or character). Returns 1 with *pr,*pc set, or 0 when the floor is full. */
static int
free_cell(char grid[][MAP_W_MAX + 2], int w, int h, int *pr, int *pc)
{
    int r, c, n = 0, pick;

    for (r = 1; r <= h; r++)
        for (c = 1; c <= w; c++)
            if (grid[r][c] == ' ') n++;
    if (n == 0) return 0;
    pick = sys_random(n);
    if (pick < 0) pick = 0;
    if (pick >= n) pick = n - 1;
    for (r = 1; r <= h; r++)
        for (c = 1; c <= w; c++)
            if (grid[r][c] == ' ' && pick-- == 0) { *pr = r; *pc = c; return 1; }
    return 0;
}

/* The grid cell a dir tag marks on a box of `rows` x `cols`. Returns 1 and sets
 * *r,*c (plus *vert for the u/d interior stair markers) for a known dir. */
static int
dir_cell(const char *d, int rows, int cols, int *r, int *c, int *vert)
{
    int mr = rows / 2, mc = cols / 2;

    *vert = 0;
    if (cs_streq(d, "n"))  { *r = 0;        *c = mc;        return 1; }
    if (cs_streq(d, "s"))  { *r = rows - 1; *c = mc;        return 1; }
    if (cs_streq(d, "w"))  { *r = mr;       *c = 0;         return 1; }
    if (cs_streq(d, "e"))  { *r = mr;       *c = cols - 1;  return 1; }
    if (cs_streq(d, "nw")) { *r = 0;        *c = 1;         return 1; }
    if (cs_streq(d, "ne")) { *r = 0;        *c = cols - 2;  return 1; }
    if (cs_streq(d, "sw")) { *r = rows - 1; *c = 1;         return 1; }
    if (cs_streq(d, "se")) { *r = rows - 1; *c = cols - 2;  return 1; }
    if (cs_streq(d, "u"))  { *r = 1;        *c = 1; *vert = 1; return 1; }
    if (cs_streq(d, "d"))  { *r = rows - 2; *c = 1; *vert = 1; return 1; }
    return 0;
}

int
main(void)
{
    int me = vm_args->player;
    char *arg = (char *)vm_args->dobjstr;
    int room, w, h, rows, cols, r, c, it, k;
    char grid[MAP_H_MAX + 2][MAP_W_MAX + 2];
    char line[MAP_W_MAX + 3];
    char nm[48];
    struct cs_out o;
    int fid[16], fx[16], fy[16], nf = 0;      /* fixed-feature overlay records */
    int cid[16], cx[16], cy[16], cself[16], nc = 0;   /* character overlay records */

    grant_accept();   /* lazily stores a map position on each character here */

    /* `map on` / `map off` toggles the opt-in automap that draws on room entry */
    while (arg && *arg == ' ') arg++;
    if (arg && (cs_streq(arg, "on") || cs_streq(arg, "off"))) {
        int on = cs_streq(arg, "on");

        cs_seti(me, "automap", on);
        puts(on ? "Automap on: the map is drawn when you enter a room."
                : "Automap off.");
        _exit(0);
    }

    /* Draw the room the mover is actually in. Reading it from the player's
     * location (not vm_args->room) keeps the automap correct when `go` calls
     * this through sys_call, where the passed room is the one just left. */
    room = sys_getobj(me, "location");
    if (room <= 0) room = vm_args->room;
    w = cs_geti(room, "mapw", MAP_W_DEF);
    h = cs_geti(room, "maph", MAP_H_DEF);
    if (w < 3) w = 3;
    if (w > MAP_W_MAX) w = MAP_W_MAX;
    if (h < 3) h = 3;
    if (h > MAP_H_MAX) h = MAP_H_MAX;
    rows = h + 2;
    cols = w + 2;

    /* frame the box: corners, horizontal and vertical walls, open interior */
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            char ch;

            if ((r == 0 || r == rows - 1) && (c == 0 || c == cols - 1)) ch = '+';
            else if (r == 0 || r == rows - 1) ch = '-';
            else if (c == 0 || c == cols - 1) ch = '|';
            else ch = ' ';
            grid[r][c] = ch;
        }
    }

    /* cut an opening, or drop a stair marker, for each exit that names a dir */
    it = 0;
    while ((it = sys_next(room, it)) != 0) {
        char d[8];
        int er, ec, vert;

        if (cs_geti(it, "dest", 0) <= 0) continue;    /* not an exit */
        cs_getstr(it, "dir", d, sizeof(d));
        if (!d[0]) continue;
        if (!dir_cell(d, rows, cols, &er, &ec, &vert)) continue;
        grid[er][ec] = vert ? (cs_streq(d, "u") ? '<' : '>') : dir_glyph(d);
    }

    /* overlay fixed features: an authored mapx/mapy when it names a free cell,
     * else first-fit over the interior in a stable order. Each placement marks
     * its cell so the next feature cannot land on it; a feature that finds no
     * free cell is recorded unplaced and only named in the legend. */
    it = 0;
    while ((it = sys_next(room, it)) != 0 && nf < 16) {
        int mx, my, pr = -1, pc = -1;

        if (!cs_geti(it, "fixed", 0)) continue;
        mx = cs_geti(it, "mapx", -1);
        my = cs_geti(it, "mapy", -1);
        if (mx >= 0 && my >= 0 && mx < w && my < h && grid[my + 1][mx + 1] == ' ') {
            pr = my + 1;
            pc = mx + 1;
        } else {
            for (r = 1; r <= h && pr < 0; r++)
                for (c = 1; c <= w && pr < 0; c++)
                    if (grid[r][c] == ' ') { pr = r; pc = c; }
        }
        if (pr >= 0) {
            grid[pr][pc] = '*';
            fx[nf] = pc - 1;
            fy[nf] = pr - 1;
        } else {
            fx[nf] = -1;
            fy[nf] = -1;
        }
        fid[nf++] = it;
    }

    /* collect the characters in the room: a child that is not an exit and has a
     * sheet (a charid, or attributes inherited from the character prototype). */
    it = 0;
    while ((it = sys_next(room, it)) != 0 && nc < 16) {
        if (cs_geti(it, "dest", 0) > 0) continue;
        if (cs_geti(it, "charid", -1) < 0 && cs_geti(it, "mig", 0) <= 0) continue;
        cid[nc] = it;
        cx[nc] = -1;
        cy[nc] = -1;
        cself[nc] = (it == me);
        nc++;
    }
    /* pass A: honour a stored position tagged for this room while its cell is
     * still free, so a character keeps its spot across redraws. */
    for (k = 0; k < nc; k++) {
        int mx, my;

        if (cs_geti(cid[k], "maproom", -1) != room) continue;
        mx = cs_geti(cid[k], "mapx", -1);
        my = cs_geti(cid[k], "mapy", -1);
        if (mx >= 0 && my >= 0 && mx < w && my < h && grid[my + 1][mx + 1] == ' ') {
            grid[my + 1][mx + 1] = cself[k] ? '@' : '&';
            cx[k] = mx;
            cy[k] = my;
        }
    }
    /* pass B: place the rest at random in a free cell and store it, tagged with
     * this room. Lazy placement means movement needs no hook: a position tagged
     * with another room is simply replaced the next time the room is drawn. */
    for (k = 0; k < nc; k++) {
        int pr, pc;

        if (cx[k] >= 0) continue;
        if (free_cell(grid, w, h, &pr, &pc)) {
            grid[pr][pc] = cself[k] ? '@' : '&';
            cs_seti(cid[k], "mapx", pc - 1);
            cs_seti(cid[k], "mapy", pr - 1);
            cs_seti(cid[k], "maproom", room);
            cx[k] = pc - 1;
            cy[k] = pr - 1;
        }
    }

    /* header, then the box row by row */
    cs_getstr(room, "name", nm, sizeof(nm));
    o.len = 0;
    cs_s(&o, "[ ");
    cs_s(&o, nm);
    cs_s(&o, " ]");
    puts(cs_cstr(&o));
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) line[c] = grid[r][c];
        line[cols] = '\0';
        puts(line);
    }

    /* legend: each exit's direction and name */
    puts("Exits:");
    it = 0;
    while ((it = sys_next(room, it)) != 0) {
        const char *dw;
        char d[8], en[32];

        if (cs_geti(it, "dest", 0) <= 0) continue;
        cs_getstr(it, "dir", d, sizeof(d));
        cs_getstr(it, "name", en, sizeof(en));
        dw = d[0] ? dir_word(d) : 0;
        o.len = 0;
        cs_s(&o, "  ");
        cs_s(&o, dw ? dw : "?");
        cs_s(&o, ": ");
        cs_s(&o, en);
        puts(cs_cstr(&o));
    }

    /* features legend: each fixed object and where it sits, or that it did not fit */
    if (nf > 0) {
        int k;
        char fn[32];

        puts("Features:");
        for (k = 0; k < nf; k++) {
            cs_getstr(fid[k], "name", fn, sizeof(fn));
            o.len = 0;
            cs_s(&o, "  ");
            cs_s(&o, fn);
            if (fx[k] >= 0) {
                cs_s(&o, " at (");
                cs_i(&o, fx[k]);
                cs_s(&o, ",");
                cs_i(&o, fy[k]);
                cs_s(&o, ")");
            } else {
                cs_s(&o, " (no room)");
            }
            puts(cs_cstr(&o));
        }
    }

    /* who-is-here legend: each character, the viewer marked, and its cell */
    if (nc > 0) {
        char cn[32];

        puts("Here:");
        for (k = 0; k < nc; k++) {
            cs_getstr(cid[k], "name", cn, sizeof(cn));
            o.len = 0;
            cs_s(&o, "  ");
            cs_s(&o, cn);
            if (cself[k]) cs_s(&o, " (you)");
            if (cx[k] >= 0) {
                cs_s(&o, " at (");
                cs_i(&o, cx[k]);
                cs_s(&o, ",");
                cs_i(&o, cy[k]);
                cs_s(&o, ")");
            } else {
                cs_s(&o, " (no room)");
            }
            puts(cs_cstr(&o));
        }
    }
    _exit(0);
}
