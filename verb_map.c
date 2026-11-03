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
    int room = vm_args->room;
    int w = cs_geti(room, "mapw", MAP_W_DEF);
    int h = cs_geti(room, "maph", MAP_H_DEF);
    int rows, cols, r, c, it;
    char grid[MAP_H_MAX + 2][MAP_W_MAX + 2];
    char line[MAP_W_MAX + 3];
    char nm[48];
    struct cs_out o;

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
        grid[er][ec] = vert ? (cs_streq(d, "u") ? '<' : '>') : ' ';
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
    _exit(0);
}
