/* chromesix_verb.h : shared helpers for ChromeSix verb programs */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#ifndef CHROMESIX_VERB_H
#define CHROMESIX_VERB_H

#include "mulibc.h"

/* Unsigned divide using only shifts and subtracts. This avoids any
 * compiler-emitted division helper. Returns the quotient and stores
 * the remainder. */
static int
cs_divmod(int num, int div, int *rem)
{
    unsigned n = (unsigned)num, d = (unsigned)div, q = 0, r = 0;

    if (d == 0) { *rem = 0; return 0; }
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= (1u << i); }
    }
    *rem = (int)r;
    return (int)q;
}

static int
cs_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* parse a leading non-negative integer (multiply by ten via shifts) */
static int
cs_atoi(const char *s)
{
    int v = 0;

    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') {
        v = (v << 3) + (v << 1) + (*s - '0');
        s++;
    }
    return v;
}

/* read an integer property, or dflt if unset/empty */
static int
cs_geti(int obj, const char *name, int dflt)
{
    char buf[32];
    int n = sys_getprop(obj, name, buf, sizeof(buf) - 1);

    if (n <= 0) return dflt;
    buf[n] = '\0';
    return cs_atoi(buf);
}

/* format a non-negative integer into dst as a NUL-terminated string */
static void
cs_itoa(char *dst, int v)
{
    char t[12];
    int n = 0, rem;

    do {
        v = cs_divmod(v, 10, &rem);
        t[n++] = '0' + rem;
    } while (v);
    for (int i = 0; i < n; i++)
        dst[i] = t[n - 1 - i];
    dst[n] = '\0';
}

/* build the property name "sk_<skill>" into dst */
static void
cs_skname(char *dst, const char *skill)
{
    int i = 0;

    dst[0] = 's'; dst[1] = 'k'; dst[2] = '_';
    while (skill[i]) { dst[3 + i] = skill[i]; i++; }
    dst[3 + i] = '\0';
}

/* line buffer, flushed to stdout */
struct cs_out { char buf[512]; int len; };

static void
cs_s(struct cs_out *o, const char *s)
{
    while (*s && o->len < (int)sizeof(o->buf))
        o->buf[o->len++] = *s++;
}

static void
cs_i(struct cs_out *o, int v)
{
    char t[12];
    int n = 0, rem;

    if (v < 0) {
        if (o->len < (int)sizeof(o->buf)) o->buf[o->len++] = '-';
        v = -v;
    }
    do {
        v = cs_divmod(v, 10, &rem);
        t[n++] = '0' + rem;
    } while (v);
    while (n && o->len < (int)sizeof(o->buf))
        o->buf[o->len++] = t[--n];
}

/* format points as dice, e.g. 7 -> "2D+1" */
static void
cs_dice(struct cs_out *o, int pts)
{
    int pips, dice = cs_divmod(pts, 3, &pips);

    cs_i(o, dice);
    cs_s(o, "D");
    if (pips) { cs_s(o, "+"); cs_i(o, pips); }
}

static void
cs_flush(struct cs_out *o)
{
    if (o->len) write(1, o->buf, o->len);
    o->len = 0;
}

/* NUL-terminate the buffer and return it, for syscalls that want a
 * C string (sys_broadcast, sys_setprop). */
static char *
cs_cstr(struct cs_out *o)
{
    int n = o->len < (int)sizeof(o->buf) ? o->len : (int)sizeof(o->buf) - 1;

    o->buf[n] = '\0';
    return o->buf;
}

/* one d6 in [1,6] */
static int
cs_d6(void)
{
    return 1 + sys_random(6);
}

/* Roll a ChromeSix pool of `points` (3 pts = 1D, remainder pips).
 * *wild: +1 edge (wild die 6), -1 glitch (wild die 1), 0 otherwise. */
static int
cs_roll(int points, int *wild)
{
    int pips, dice = cs_divmod(points, 3, &pips);
    int n = dice > 0 ? dice : 1;
    int total = pips;

    *wild = 0;
    for (int i = 0; i < n; i++) {
        int r = cs_d6();
        if (i == 0) *wild = (r == 6) ? 1 : (r == 1) ? -1 : 0;
        total += r;
    }
    return total;
}

/* Passive Defense bonus from the hook property */
static int
cs_hook_bonus(int obj)
{
    char h[16];
    int n = sys_getprop(obj, "hook", h, sizeof(h) - 1);

    if (n <= 0) return 0;
    h[n] = '\0';
    if (cs_streq(h, "cyber")) return 6;
    if (cs_streq(h, "awakened")) return 4;
    if (cs_streq(h, "street")) return 2;
    return 0;
}

/* The twenty skills with their governing attribute, shared by the verbs. */
struct cs_skill { const char *name; const char *attr; };
static const struct cs_skill cs_skills[20] = {
    { "firearms", "agi" }, { "melee", "agi" }, { "stealth", "agi" },
    { "athletics", "agi" }, { "drive", "agi" },
    { "heavy", "mig" }, { "brawl", "mig" }, { "endurance", "mig" },
    { "haul", "mig" }, { "demolitions", "mig" },
    { "hacking", "wit" }, { "cybertech", "wit" }, { "medicine", "wit" },
    { "perception", "wit" }, { "spellcasting", "wit" },
    { "command", "cha" }, { "negotiate", "cha" }, { "con", "cha" },
    { "streetwise", "cha" }, { "performance", "cha" },
};

/* index of the named skill, or -1 if unknown */
static int
cs_skill_idx(const char *name)
{
    for (int i = 0; i < 20; i++)
        if (cs_streq(name, cs_skills[i].name))
            return i;
    return -1;
}

/* Derived stats. Recalc folds the size step (Section 15) and the Hook
 * bonus into Passive Defense, Soak, and Max BP. Size 0 is a standard
 * human, where the base formulas stand. */
struct cs_derived { int pd; int soak; int maxbp; int maxgrit; };

/* defined below; cs_recalc reads it for the passive cyberware unlocks (M35c) */
static int cs_has_unlock(int sheet, const char *name);

static void
cs_recalc(int ch, struct cs_derived *d)
{
    int ap, mp, wp, cp;
    int ad = cs_divmod(cs_geti(ch, "agi", 0), 3, &ap);
    int md = cs_divmod(cs_geti(ch, "mig", 0), 3, &mp);
    int wd = cs_divmod(cs_geti(ch, "wit", 0), 3, &wp);
    int cd = cs_divmod(cs_geti(ch, "cha", 0), 3, &cp);
    int size = cs_geti(ch, "size", 0);
    int armor = cs_geti(ch, "armor", 0);

    d->pd = (ad << 1) + ap + cs_hook_bonus(ch) - size * 2;
    if (cs_has_unlock(ch, "reflex")) d->pd += 2;    /* M36c reflex booster */
    if (cs_has_unlock(ch, "kinetic")) d->pd += 1;   /* M42c kinetic absorbers */
    if (cs_geti(ch, "shield", 0) > 0) d->pd += 3;   /* M41a mana shield, timed */
    if (d->pd > 20) d->pd = 20;
    if (d->pd < 0) d->pd = 0;
    d->soak = (md << 1) + mp + armor + (size > 0 ? size : 0);
    if (cs_has_unlock(ch, "dermal")) d->soak += 2;  /* M35c dermal plating */
    if (cs_geti(ch, "mesh", 0) > 0) d->soak += wd;  /* M41a dermal wire mesh, timed */
    d->maxbp = 12 + md * 3 + size * 6;
    if (d->maxbp < 1) d->maxbp = 1;
    d->maxgrit = 3 + wd + cd;
    if (cs_has_unlock(ch, "vigor")) d->maxgrit += 3;  /* M36c vigor */
    if (cs_has_unlock(ch, "kinetic")) d->maxgrit -= 1; /* M42c neural load */
    if (d->maxgrit < 1) d->maxgrit = 1;
}

/* Passive Perception (chromesix.md): the always-on notice TN an ambusher's
 * Stealth must beat. 4 + (Wit + Perception) dice x2 + the leftover pips. */
static int
cs_passperc(int ch)
{
    int r;
    int dice = cs_divmod(cs_geti(ch, "wit", 0) + cs_geti(ch, "sk_perception", 0),
                         3, &r);
    return 4 + (dice << 1) + r;
}

/* Social-conflict derived stats (rules Section 16), the talk-side mirror of
 * Max BP, Soak, and Passive Defense. Max Resolve is the social buffer, 12 plus
 * three per Charm die. Composure is the social Soak, Wit dice x2 plus the
 * leftover pips. Passive Resolve is the always-on TN a push must beat,
 * 4 + (Wit + Charm dice) x2 + the leftover pips. */
static int
cs_max_resolve(int ch)
{
    int r, cd = cs_divmod(cs_geti(ch, "cha", 0), 3, &r);

    return 12 + cd * 3;
}

static int
cs_composure(int ch)
{
    int wp, wd = cs_divmod(cs_geti(ch, "wit", 0), 3, &wp);

    return (wd << 1) + wp;
}

static int
cs_passresolve(int ch)
{
    int r;
    int dice = cs_divmod(cs_geti(ch, "wit", 0) + cs_geti(ch, "cha", 0), 3, &r);

    return 4 + (dice << 1) + r;
}

/* Faction standing (rules Section 14): the signed step for `faction`, read
 * from the sheet's `standing` property -- comma-separated `id:step` pairs --
 * and defaulting to 0 (Neutral) when the faction is absent. Steps are small
 * signed integers on a seven-step band from +3 Allied to -3 Hunted. */
static int
cs_standing(int sheet, const char *faction)
{
    char buf[128];
    int n = sys_getprop(sheet, "standing", buf, sizeof(buf) - 1);
    int i = 0;

    if (n <= 0) return 0;
    buf[n] = '\0';
    while (buf[i]) {
        int j = 0, match = 1, sign = 1, v = 0;

        while (buf[i] && buf[i] != ':' && buf[i] != ',') {
            /* advance j only on a match, so faction[j] is never read past its
             * NUL when a stored id is longer than the queried faction */
            if (faction[j] == '\0' || buf[i] != faction[j]) match = 0;
            else j++;
            i++;
        }
        if (faction[j] != '\0') match = 0;      /* id must end exactly at ':' */
        if (buf[i] == ':') {
            i++;
            if (buf[i] == '-') { sign = -1; i++; }
            while (buf[i] >= '0' && buf[i] <= '9') {
                v = v * 10 + (buf[i] - '0');
                i++;
            }
            if (match) return sign * v;
        }
        while (buf[i] && buf[i] != ',') i++;     /* skip to the next pair */
        if (buf[i] == ',') i++;
    }
    return 0;
}

/* Add `delta` to the sheet's standing with `faction` (M37c), the verb-side
 * writer mirroring the host @standing merge. Rebuilds the comma-separated
 * `id:step` list, adjusting the matching pair or appending a new one, clamped to
 * the [-3, +3] band. The player owns the sheet, so the caller writes it. */
static void
cs_standing_add(int sheet, const char *faction, int delta)
{
    char buf[128];
    struct cs_out o;
    int n = sys_getprop(sheet, "standing", buf, sizeof(buf) - 1);
    int newv = cs_standing(sheet, faction) + delta;
    int i = 0, found = 0;

    if (n < 0) n = 0;
    buf[n] = '\0';
    if (newv > 3) newv = 3;
    if (newv < -3) newv = -3;
    o.len = 0;
    while (buf[i]) {                     /* copy each pair, replacing the match */
        char fac[32];
        int fj = 0, v = 0, sign = 1, ismatch;

        while (buf[i] && buf[i] != ':' && buf[i] != ',' && fj < 31)
            fac[fj++] = buf[i++];
        fac[fj] = '\0';
        if (buf[i] == ':') {
            i++;
            if (buf[i] == '-') { sign = -1; i++; }
            while (buf[i] >= '0' && buf[i] <= '9') { v = v * 10 + (buf[i] - '0'); i++; }
        }
        ismatch = cs_streq(fac, faction);
        if (fac[0]) {
            if (o.len) cs_s(&o, ",");
            cs_s(&o, fac); cs_s(&o, ":");
            cs_i(&o, ismatch ? newv : sign * v);
        }
        if (ismatch) found = 1;
        while (buf[i] && buf[i] != ',') i++;
        if (buf[i] == ',') i++;
    }
    if (!found) {
        if (o.len) cs_s(&o, ",");
        cs_s(&o, faction); cs_s(&o, ":"); cs_i(&o, newv);
    }
    sys_setprop(sheet, "standing", cs_cstr(&o));
}

/* The band word for a standing step (rules Section 14). */
static const char *
cs_standing_word(int step)
{
    if (step >= 3) return "Allied";
    if (step == 2) return "Friendly";
    if (step == 1) return "Known";
    if (step == 0) return "Neutral";
    if (step == -1) return "Watched";
    if (step == -2) return "Hostile";
    return "Hunted";
}

/* store a non-negative integer property (negatives clamp to 0) */
static int
cs_seti(int obj, const char *name, int v)
{
    char b[12];

    if (v < 0) v = 0;
    cs_itoa(b, v);
    return sys_setprop(obj, name, b);
}

/* read a string property into dst, always NUL-terminated */
static void
cs_getstr(int obj, const char *name, char *dst, int sz)
{
    int n = sys_getprop(obj, name, dst, sz - 1);

    if (n < 0) n = 0;
    dst[n] = '\0';
}

/* An NPC's effective standing toward a player against `faction` (M38c): the
 * player's `cs_standing` plus the NPC's optional `disp` offset, clamped to the
 * [-3, +3] band. With no `disp` it reads exactly as the faction standing, so a
 * specific NPC can be warmer or colder than its faction (rules Section 14): a
 * fixer whose faction hunts you may still deal with you, or a grudge-holder be
 * hostile where the faction is neutral. */
static int
cs_disposition(int npc, int sheet, const char *faction)
{
    char db[12];
    int off = 0, sign = 1, k = 0, v;

    /* `disp` may be negative (a grudge), which cs_geti/cs_atoi cannot read, so
     * parse the sign here. */
    cs_getstr(npc, "disp", db, sizeof(db));
    if (db[0] == '-') { sign = -1; k++; }
    while (db[k] >= '0' && db[k] <= '9') off = off * 10 + (db[k++] - '0');
    v = cs_standing(sheet, faction) + sign * off;
    if (v > 3) v = 3;
    if (v < -3) v = -3;
    return v;
}

/* --- M39a: short rest, the between-fights BP recovery (Section 8) ---
 *
 * BP climbs back toward maxbp while a character sits in a `safe` room and out of
 * a fight, with no rest command. It is computed lazily: `rest_since` stamps when
 * the current calm stretch began, in the `rest_room` it began in, and each read
 * pays out the elapsed whole-BP share, advancing `rest_since` by only the time
 * that share consumed so a remainder is never lost. A step into an unsafe room
 * (a new room re-anchors the clock) or a fight restarts it. The rest period is
 * `#0.rest_secs` (default REST_SECS), so a test can rest in seconds. */
#define REST_SECS 7200          /* full BP over two in-game hours (Section 8) */

static void
cs_rest(int sheet, int room)
{
    struct cs_derived d;
    int now, since, bp, period, elapsed, gained, consumed;

    if (cs_geti(room, "safe", 0) != 1 || cs_geti(room, "cb_active", 0) == 1) {
        cs_seti(sheet, "rest_since", 0);   /* unsafe or fighting: clock off */
        return;
    }
    now = sys_now();
    since = cs_geti(sheet, "rest_since", 0);
    if (since == 0 || cs_geti(sheet, "rest_room", 0) != room) {
        cs_seti(sheet, "rest_since", now); /* a fresh calm stretch starts here */
        cs_seti(sheet, "rest_room", room);
        return;
    }
    cs_recalc(sheet, &d);
    bp = cs_geti(sheet, "bp", d.maxbp);
    if (bp >= d.maxbp) { cs_seti(sheet, "rest_since", now); return; }
    period = cs_geti(0, "rest_secs", REST_SECS);
    if (period < 1) period = 1;
    elapsed = now - since;
    if (elapsed <= 0) return;
    gained = elapsed * d.maxbp / period;
    if (gained <= 0) return;               /* remainder kept: rest_since unchanged */
    bp += gained;
    if (bp >= d.maxbp) {           /* M40c: a rest that fills BP clears Crash */
        bp = d.maxbp;
        cs_seti(sheet, "crash", 0);
    }
    cs_seti(sheet, "bp", bp);
    consumed = gained * period / d.maxbp;  /* advance only by what was paid out */
    cs_seti(sheet, "rest_since", since + consumed);
}

/* --- M39c: standing decay, a reputation fading toward Neutral (Section 14) ---
 *
 * Standing drifts one step toward Neutral for each idle DECAY_SECS (tunable via
 * `#0.decay_secs`) that passes, so a reputation earned or lost fades if it is not
 * renewed. It is lazy: `decay_tick` marks when the current interval began, and a
 * standing read applies the whole steps elapsed to every faction, advancing the
 * tick by only what it consumed. The mirror of the cs_standing_add writer. */
#define DECAY_SECS 604800       /* one step toward Neutral per idle week */

static void
cs_decay(int sheet)
{
    char buf[128];
    struct cs_out o;
    int now = sys_now();
    int tick = cs_geti(sheet, "decay_tick", 0);
    int period, elapsed, steps, i = 0, n;

    if (tick == 0) { cs_seti(sheet, "decay_tick", now); return; }
    period = cs_geti(0, "decay_secs", DECAY_SECS);
    if (period < 1) period = 1;
    elapsed = now - tick;
    if (elapsed < period) return;
    steps = elapsed / period;
    n = sys_getprop(sheet, "standing", buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    o.len = 0;
    while (buf[i]) {                     /* copy each pair, drifting it toward 0 */
        char fac[32];
        int fj = 0, v = 0, sign = 1;

        while (buf[i] && buf[i] != ':' && buf[i] != ',' && fj < 31)
            fac[fj++] = buf[i++];
        fac[fj] = '\0';
        if (buf[i] == ':') {
            i++;
            if (buf[i] == '-') { sign = -1; i++; }
            while (buf[i] >= '0' && buf[i] <= '9') { v = v * 10 + (buf[i] - '0'); i++; }
        }
        v *= sign;
        if (v > 0) { v -= steps; if (v < 0) v = 0; }
        else if (v < 0) { v += steps; if (v > 0) v = 0; }
        if (fac[0]) {
            if (o.len) cs_s(&o, ",");
            cs_s(&o, fac); cs_s(&o, ":"); cs_i(&o, v);
        }
        while (buf[i] && buf[i] != ',') i++;
        if (buf[i] == ',') i++;
    }
    sys_setprop(sheet, "standing", cs_cstr(&o));
    cs_seti(sheet, "decay_tick", tick + steps * period);
}

/* The sheet holding a combatant's stats: a player object points at its
 * persistent sheet through `charid`; an NPC object is its own sheet. */
static int
cs_sheet(int obj)
{
    int c = cs_geti(obj, "charid", -1);

    return c >= 0 ? c : obj;
}

/* --- M25g: body slots and inventory (chromesix-smolmoo.md Section 9) ---
 *
 * A creature's anatomy is a comma-separated list of slot tags on the creature
 * object, each a part name plus an index (hand0, torso0, tentacle3). Items in
 * the creature's contents occupy a slot by naming its tag in a `slot` prop.
 * The sheet's wielded/worn/armor props stay the combat source of truth; these
 * helpers add the physical slot frame the inventory view and gear verbs read. */

/* The frame a creature falls back to when it declares no anatomy. */
#define CS_ANATOMY_HUMAN \
    "head0,torso0,arm0,arm1,hand0,hand1,leg0,leg1,foot0,foot1,back0,waist0"

/* Read a creature's anatomy into dst, defaulting to a standard human. */
static void
cs_anatomy(int creature, char *dst, int sz)
{
    int n = sys_getprop(creature, "anatomy", dst, sz - 1);
    const char *h = CS_ANATOMY_HUMAN;
    int i;

    if (n > 0) { dst[n] = '\0'; return; }
    for (i = 0; h[i] && i < sz - 1; i++) dst[i] = h[i];
    dst[i] = '\0';
}

/* The role of a slot tag, from its part name (the leading letters before the
 * index): 'h' hold (hand), 'd' dual (tentacle), 'w' wear (everything else). */
static char
cs_part_role(const char *tag)
{
    char part[12];
    int i = 0;

    while (tag[i] && (tag[i] < '0' || tag[i] > '9') && i < (int)sizeof(part) - 1)
        part[i] = tag[i], i++;
    part[i] = '\0';
    if (cs_streq(part, "hand")) return 'h';
    if (cs_streq(part, "tentacle")) return 'd';
    return 'w';
}

/* The slot role an item needs: 'h' for a weapon (it deals damage, so it takes
 * a hold slot), 'w' otherwise (armor, clothing, and worn containers). */
static char
cs_item_role(int item)
{
    return cs_geti(item, "dmg", 0) > 0 ? 'h' : 'w';
}

/* The item occupying `tag` in the creature's contents, or 0. Walks contents
 * with sys_next; an item occupies a slot when its `slot` prop names the tag. */
static int
cs_slot_holder(int creature, const char *tag)
{
    char s[16];
    int it = 0;

    while ((it = sys_next(creature, it)) != 0) {
        cs_getstr(it, "slot", s, sizeof(s));
        if (cs_streq(s, tag)) return it;
    }
    return 0;
}

/* Find a free slot that accepts an item of role `need` ('h' or 'w'). A hold
 * item takes a free hand or tentacle, a wear item a free wear part or
 * tentacle. When `pref` is a non-empty part name the search is limited to that
 * part (a satchel to back, a vest to torso), falling through to any matching
 * slot when the preferred part is full or absent. Copies the tag into out and
 * returns 1, or returns 0 when no slot of that kind is open. */
static int
cs_find_free_slot(int creature, char need, const char *pref, char *out, int sz)
{
    char an[256], tag[16], part[12];
    char role;
    int pass, i, j, k;

    cs_anatomy(creature, an, sizeof(an));
    /* pass 0 honours the part preference; pass 1 accepts any matching slot */
    for (pass = (pref && pref[0]) ? 0 : 1; pass < 2; pass++) {
        i = 0;
        while (an[i]) {
            while (an[i] == ',') i++;
            j = 0;
            while (an[i] && an[i] != ',' && j < (int)sizeof(tag) - 1)
                tag[j++] = an[i++];
            tag[j] = '\0';
            if (j == 0) continue;
            role = cs_part_role(tag);
            if (role != need && role != 'd') continue;
            if (cs_slot_holder(creature, tag) != 0) continue;
            if (pass == 0) {
                int m = 0;
                while (tag[m] && (tag[m] < '0' || tag[m] > '9') &&
                       m < (int)sizeof(part) - 1)
                    part[m] = tag[m], m++;
                part[m] = '\0';
                if (!cs_streq(part, pref)) continue;
            }
            for (k = 0; k < j && k < sz - 1; k++) out[k] = tag[k];
            out[k] = '\0';
            return 1;
        }
    }
    return 0;
}

/* --- M25g slice 2: ammunition (rules Section 6, MUD-mode) ---
 *
 * A ranged weapon carries a magazine directly: `clip` is its capacity and
 * `ammo` the rounds left. A weapon with no `clip` (a blade, bare hands) never
 * runs dry. Firing depletes one round; an empty weapon must reload first. */

/* Does this weapon draw from a magazine (has a clip capacity)? */
static int
cs_uses_ammo(int weapon)
{
    return cs_geti(weapon, "clip", 0) > 0;
}

/* Is this an ammo weapon with no rounds left? */
static int
cs_weapon_empty(int weapon)
{
    return cs_uses_ammo(weapon) && cs_geti(weapon, "ammo", 0) <= 0;
}

/* Spend one round; returns the rounds left, or -1 for a weapon that uses no
 * ammo. Clamped at 0. */
static int
cs_fire(int weapon)
{
    int a;

    if (!cs_uses_ammo(weapon)) return -1;
    a = cs_geti(weapon, "ammo", 0) - 1;
    if (a < 0) a = 0;
    cs_seti(weapon, "ammo", a);
    return a;
}

/* Refill the magazine to its clip capacity; returns the new count. */
static int
cs_reload_weapon(int weapon)
{
    int clip = cs_geti(weapon, "clip", 0);

    cs_seti(weapon, "ammo", clip);
    return clip;
}

/* --- M25g slice 2: weight and encumbrance (rules Section 6) --- */

/* Carry rating: (Might dice x 2) + Might pips, in load units. The weight a
 * creature bears before it slows down. */
static int
cs_carry(int sheet)
{
    int mp, md = cs_divmod(cs_geti(sheet, "mig", 0), 3, &mp);

    return (md << 1) + mp;
}

/* Total weight carried: the `weight` summed over everything in the creature's
 * containment subtree. The host caches this rollup and recomputes it lazily
 * when contents move, so this is a cache read, not a walk (see sys_rollup). */
static int
cs_load(int creature)
{
    return sys_rollup(creature, "weight");
}

/* Number of items a container holds, the count its `cap` limits. The host
 * caches it (see sys_rollup); it is the whole containment subtree, which equals
 * the direct contents while containers do not nest (Section 9). */
static int
cs_count(int container)
{
    return sys_rollup(container, "");
}

/* Encumbrance tier: 0 unburdened, 1 encumbered (Slowed in a fight, halved
 * travel out of it), 2 overloaded (movement locked until weight is shed).
 * Load against the carry rating and twice it (rules Section 6). */
static int
cs_encumbrance(int creature)
{
    int cap = cs_carry(cs_sheet(creature));
    int load = cs_load(creature);

    if (load > cap * 2) return 2;
    if (load > cap) return 1;
    return 0;
}

/* Parse the b2 hash out of a verb object's `elf` property, which the host
 * stores as "[mode,b2:HASH]". Returns OK/-1. Used to sys_spawn a verb. */
static int
cs_elf_hash(int verbobj, char *out, int outsz)
{
    char buf[128];
    int n = sys_getprop(verbobj, "elf", buf, sizeof(buf) - 1);
    int i = 0, j = 0;

    out[0] = '\0';
    if (n <= 0) return -1;
    buf[n] = '\0';
    while (buf[i]) {
        if (buf[i] == 'b' && buf[i + 1] == '2' && buf[i + 2] == ':') {
            i += 3;
            break;
        }
        i++;
    }
    if (!buf[i]) return -1;
    while (buf[i] && buf[i] != ']' && j < outsz - 1)
        out[j++] = buf[i++];
    out[j] = '\0';
    return 0;
}

/* Descriptive Condition scale (rules Section 15): a word for remaining BP
 * plus a wound descriptor. Hides raw numbers in prose. */
static void
cs_condition(int cur, int maxbp, int wounds, struct cs_out *o)
{
    int r, pct;

    if (maxbp < 1) maxbp = 1;
    pct = cs_divmod(cur * 100, maxbp, &r);
    if (cur <= 0) cs_s(o, "down");
    else if (pct >= 100) cs_s(o, "unhurt");
    else if (pct >= 75) cs_s(o, "scuffed");
    else if (pct >= 50) cs_s(o, "bloodied");
    else if (pct >= 25) cs_s(o, "badly hurt");
    else cs_s(o, "barely standing");
    if (wounds == 1) cs_s(o, ", favoring an injury");
    else if (wounds == 2) cs_s(o, ", visibly wounded");
    else if (wounds >= 3) cs_s(o, ", crippled");
}

/* Append the active combat conditions on `sheet` to o, comma separated (M40). A
 * condition is a sheet prop, in the idiom of downed and wounds; this names the
 * ones set, for the sheet's status view. Leaves o untouched when none are set. */
static void
cs_cond_list(int sheet, struct cs_out *o)
{
    int n = 0;

    if (cs_geti(sheet, "prone", 0))   cs_s(o, n++ ? ", prone" : "prone");
    if (cs_geti(sheet, "stunned", 0)) cs_s(o, n++ ? ", stunned" : "stunned");
    if (cs_geti(sheet, "shaken", 0))  cs_s(o, n++ ? ", shaken" : "shaken");
    if (cs_geti(sheet, "bleed", 0))   cs_s(o, n++ ? ", bleeding" : "bleeding");
    if (cs_geti(sheet, "suppress", 0)) cs_s(o, n++ ? ", suppressed" : "suppressed");
    if (cs_geti(sheet, "exposed", 0)) cs_s(o, n++ ? ", exposed" : "exposed");
    if (cs_geti(sheet, "marked_by", 0)) cs_s(o, n++ ? ", marked" : "marked");
}

/* Apply the start-of-turn ticks. Ongoing damage (`bleed`, M40c) deals its set
 * amount as direct, un-soaked damage and downs the victim at 0 BP, running until
 * an action or save ends it. Timed self-buffs (M41a) count down one round and
 * lapse at zero. */
static void
cs_cond_tick(int sheet, int room)
{
    struct cs_out o;
    char nm[32];
    int bleed, bp, v;

    /* M41a: timed self-buffs count down one round at the start of the holder's
     * turn and lapse at zero. Their effect is read live in cs_recalc. */
    if ((v = cs_geti(sheet, "shield", 0)) > 0) cs_seti(sheet, "shield", v - 1);
    if ((v = cs_geti(sheet, "mesh", 0)) > 0)   cs_seti(sheet, "mesh", v - 1);

    bleed = cs_geti(sheet, "bleed", 0);
    if (bleed <= 0) return;
    bp = cs_geti(sheet, "bp", 0) - bleed;
    cs_getstr(sheet, "name", nm, sizeof(nm));
    o.len = 0;
    cs_s(&o, nm);
    cs_s(&o, " bleeds for ");
    cs_i(&o, bleed);
    if (bp <= 0) {
        cs_seti(sheet, "bp", 0);
        cs_seti(sheet, "downed", 1);
        cs_s(&o, " and bleeds out. Downed!");
    } else {
        cs_seti(sheet, "bp", bp);
        cs_s(&o, ".");
    }
    sys_broadcast(room, cs_cstr(&o));
}

/* A shaken combatant tries to rally at the end of its turn: a Wit roll against
 * Moderate (10) shakes off the fear (rules Section 8, M40b). */
static void
cs_rally(int sheet, int room)
{
    struct cs_out o;
    char nm[32];
    int wild;

    if (!cs_geti(sheet, "shaken", 0)) return;
    if (cs_roll(cs_geti(sheet, "wit", 0), &wild) < 10) return;
    cs_seti(sheet, "shaken", 0);
    cs_getstr(sheet, "name", nm, sizeof(nm));
    o.len = 0;
    cs_s(&o, nm);
    cs_s(&o, " shakes off the fear.");
    sys_broadcast(room, cs_cstr(&o));
}

/* Descriptive Difficulty scale (rules Section 15): a word for a TN. */
static void
cs_difficulty(int tn, struct cs_out *o)
{
    if (tn <= 6) cs_s(o, "trivial");
    else if (tn <= 10) cs_s(o, "fair");
    else if (tn <= 14) cs_s(o, "hard");
    else if (tn <= 18) cs_s(o, "formidable");
    else if (tn <= 22) cs_s(o, "heroic");
    else cs_s(o, "legendary");
}

/* Render a ten-cell bar meter, e.g. "HP [#####-----] 18/21". */
static void
cs_meter(struct cs_out *o, const char *label, int cur, int max)
{
    int r, filled, i;

    if (max < 1) max = 1;
    if (cur < 0) cur = 0;
    filled = cs_divmod(cur * 10, max, &r);
    if (filled > 10) filled = 10;
    cs_s(o, label);
    cs_s(o, " [");
    for (i = 0; i < 10; i++)
        cs_s(o, i < filled ? "#" : "-");
    cs_s(o, "] ");
    cs_i(o, cur);
    cs_s(o, "/");
    cs_i(o, max);
}

/* Total transient cover bonus to Passive Defense: the positional tier
 * (0/2/4, set by taking cover) plus a temporary Cover-support bonus. */
static int
cs_cover(int sheet)
{
    return cs_geti(sheet, "cover", 0) + cs_geti(sheet, "cover_bonus", 0);
}

/* Range bands: 0 Engaged, 1 Short, 2 Long. A weapon's reach is how far it
 * carries in bands: engaged 0, short 1, long 2. */
static int
cs_reach(int weapon)
{
    char r[12];

    cs_getstr(weapon, "range", r, sizeof(r));
    if (cs_streq(r, "long")) return 2;
    if (cs_streq(r, "short")) return 1;
    return 0;
}

static const char *
cs_band_word(int band)
{
    if (band <= 0) return "engaged";
    if (band == 1) return "short";
    return "long";
}

/* Room span: the deepest range band a fight can open to inside one room
 * (M25h). An explicit `span` prop wins; otherwise an outdoor room reaches Long
 * (2) and an indoor room only Short (1). Clamped to 0..2. A span 0 closet
 * pins everyone at Engaged. This bounds how far `retreat` can back off without
 * leaving the room; crossing to an adjacent zone is `go`, not a band. */
static int
cs_room_span(int room)
{
    int span = cs_geti(room, "span", -1);

    if (span < 0) span = cs_geti(room, "outdoor", 0) ? 2 : 1;
    if (span > 2) span = 2;
    if (span < 0) span = 0;
    return span;
}

/* Parse the "id,id,..." combat roster in cb_roster, filling out[] with the
 * ids that are not `self` (the opponents). Returns the count. */
static int
cs_roster_foes(int room, int self, int *out, int max)
{
    char buf[64];
    int n = sys_getprop(room, "cb_roster", buf, sizeof(buf) - 1);
    int v = 0, have = 0, i, c = 0;

    if (n <= 0) return 0;
    buf[n] = '\0';
    for (i = 0; ; i++) {
        char ch = buf[i];
        if (ch >= '0' && ch <= '9') {
            v = (v << 3) + (v << 1) + (ch - '0');
            have = 1;
        } else {
            if (have && v != self && c < max) out[c++] = v;
            v = 0;
            have = 0;
            if (!ch) break;
        }
    }
    return c;
}

/* Number of live foes Engaged (band 0) with `self`. Frontage caps this. */
static int
cs_engaged_count(int room, int self)
{
    int foes[8], k = cs_roster_foes(room, self, foes, 8), c = 0, i;

    for (i = 0; i < k; i++) {
        int s = cs_sheet(foes[i]);
        if (!cs_geti(s, "downed", 0) && cs_geti(s, "band", 0) == 0) c++;
    }
    return c;
}

/* Melee frontage cap on a target: two attackers, or four if the target is
 * Large or bigger (size step >= 1). */
static int
cs_frontage(int sheet)
{
    return cs_geti(sheet, "size", 0) >= 1 ? 4 : 2;
}

/* Push a roll (rules Section 10): spend n Grit for +nD, capped at +2D.
 * Returns the pool bonus in points (3 per die), 0 for no push, or -1 when the
 * sheet cannot afford it (nothing is spent). The grit property is the store
 * of record; the host status gauge mirrors it. */
static int
cs_grit_push(int sheet, int n)
{
    int grit;

    if (n <= 0) return 0;
    if (n > 2) n = 2;
    grit = cs_geti(sheet, "grit", 0);
    if (grit < n) return -1;
    cs_seti(sheet, "grit", grit - n);
    return n * 3;
}

/* Use one carried stim (rules Section 8): restore 2 Grit (capped at Max Grit),
 * spend a dose, and bump the stim load. Each dose past the safe limit (Might
 * dice) stacks Crash, a -1D on all the character's pools that clears only on a
 * full short rest. Returns 1 if this dose inflicted a fresh Crash stack, 0 for
 * a safe dose, or -1 when no stim is carried (nothing changes). */
static int
cs_stim_use(int sheet)
{
    struct cs_derived d;
    int stims = cs_geti(sheet, "stims", 0);
    int grit, load, mdice, r, crashed = 0;

    if (stims <= 0) return -1;
    cs_recalc(sheet, &d);
    cs_seti(sheet, "stims", stims - 1);

    grit = cs_geti(sheet, "grit", 0) + 2;
    if (grit > d.maxgrit) grit = d.maxgrit;
    cs_seti(sheet, "grit", grit);

    load = cs_geti(sheet, "stim_load", 0) + 1;
    cs_seti(sheet, "stim_load", load);
    mdice = cs_divmod(cs_geti(sheet, "mig", 0), 3, &r);
    if (load > mdice) {
        cs_seti(sheet, "crash", cs_geti(sheet, "crash", 0) + 1);
        crashed = 1;
    }
    return crashed;
}

/* The Crash pool penalty in points: -3 (one die) per stacked Crash. */
static int
cs_crash_penalty(int sheet)
{
    return cs_geti(sheet, "crash", 0) * 3;
}

/* Spend a flat n Grit. Returns 0 on success, -1 when the sheet is short (and
 * nothing is spent). Unlike cs_grit_push this is not capped: maneuvers set
 * their own cost. */
static int
cs_grit_spend(int sheet, int n)
{
    int grit = cs_geti(sheet, "grit", 0);

    if (grit < n) return -1;
    cs_seti(sheet, "grit", grit - n);
    return 0;
}

/* Is unlock `name` present in the sheet's comma-separated `maneuvers` list?
 * Matches a whole token so "smart" never matches "smartlink". */
static int
cs_has_unlock(int sheet, const char *name)
{
    char list[128];
    int n = sys_getprop(sheet, "maneuvers", list, sizeof(list) - 1);
    const char *p;
    int i;

    if (n < 0) n = 0;
    list[n] = '\0';
    for (p = list; *p; ) {
        while (*p == ',' || *p == ' ') p++;
        for (i = 0; name[i] && p[i] && p[i] != ',' && name[i] == p[i]; i++)
            ;
        if (name[i] == '\0' && (p[i] == '\0' || p[i] == ','))
            return 1;
        while (*p && *p != ',') p++;
    }
    return 0;
}

/* An unlock's active effect (M36b), the tag `verb_use` switches on to apply it.
 * UEF_PASSIVE means it is not activated as an action; its effect is read where
 * its stat applies (dermal in cs_recalc), so `use` refuses it. */
#define UEF_PASSIVE 0
#define UEF_AIM     1       /* smartlink: next attack ignores cover, -2 PD */
#define UEF_SHIELD  2       /* mana shield: +3 Passive Defense while active (M41a) */
#define UEF_MESH    3       /* dermal wire mesh: +Wit dice Soak while active (M41a) */
#define UEF_SHOCK   4       /* static shock: a Wit+Spellcasting attack (M41b) */
#define UEF_SUPPRESS 5      /* suppressive fire: Suppress the roster foes (M41b) */
#define UEF_INJECT  6       /* biomedical injector: restore BP to self or ally (M41c) */
#define UEF_EXPOSE  7       /* tactical co-processor: mark a target Exposed (M45b) */
#define UEF_MARK    8       /* threat-assessment optics: mark a target for +1D (M45b) */

/* The 5 CP unlock catalog (M35c): each acquirable maneuver, cyberware mod, or
 * spell as one id, with its CP cost, the hook it requires ("" for any), whether
 * it occupies a cyberware graft slot, and, for an active unlock, the Grit its
 * use costs and its effect tag (M36b). `learn` reads the cost and gates; `use`
 * reads the Grit cost and tag. The effect of each unlock lives with its use. */
struct cs_unlock {
    const char *id; int cp; const char *hook; int graft; int grit; int effect;
};
static const struct cs_unlock cs_unlocks[] = {
    { "smartlink", 5, "",         0, 2, UEF_AIM },     /* aim, granted at start */
    { "dermal",    5, "cyber",    1, 0, UEF_PASSIVE }, /* +2 Soak, one graft */
    { "reflex",    5, "cyber",    1, 0, UEF_PASSIVE }, /* +2 Defense, one graft */
    { "vigor",     5, "awakened", 0, 0, UEF_PASSIVE }, /* +3 Max Grit */
    { "shield",    5, "awakened", 0, 1, UEF_SHIELD },  /* +3 PD, timed (M41a) */
    { "mesh",      5, "cyber",    1, 1, UEF_MESH },     /* +Wit Soak, timed (M41a) */
    { "shock",     5, "awakened", 0, 1, UEF_SHOCK },    /* 4D spell attack (M41b) */
    { "suppress",  5, "",         0, 1, UEF_SUPPRESS }, /* suppress the foes (M41b) */
    { "inject",    5, "cyber",    1, 1, UEF_INJECT },   /* restore BP, self/ally (M41c) */
    { "governor",  5, "cyber",    1, 0, UEF_PASSIVE },  /* reaction: cut a hit (M42a) */
    { "riposte",   5, "",         0, 0, UEF_PASSIVE },  /* reaction: strike a big miss (M42b) */
    { "defib",     5, "cyber",    1, 0, UEF_PASSIVE },  /* reaction: cheat death once (M42b) */
    { "aegis",     5, "awakened", 0, 0, UEF_PASSIVE },  /* reaction: shield an ally (M42c) */
    { "kinetic",   5, "cyber",    1, 0, UEF_PASSIVE },  /* +1 PD, -1 Max Grit (M42c) */
    { "wired",     5, "cyber",    1, 0, UEF_PASSIVE },  /* +initiative bonus (M45a) */
    { "coproc",    5, "cyber",    1, 1, UEF_EXPOSE },   /* Expose a target (M45b) */
    { "optics",    5, "cyber",    1, 1, UEF_MARK },     /* mark a target, +1D (M45b) */
};
#define CS_NUNLOCKS ((int)(sizeof(cs_unlocks) / sizeof(cs_unlocks[0])))
#define CS_GRAFT_CAP 2      /* cyberware graft slots (Cyber-Augmented hook) */

/* catalog index of unlock `id`, or -1 if it is not a known unlock */
static int
cs_unlock_find(const char *id)
{
    int i;

    for (i = 0; i < CS_NUNLOCKS; i++)
        if (cs_streq(id, cs_unlocks[i].id)) return i;
    return -1;
}

/* how many learned unlocks occupy a graft slot, against CS_GRAFT_CAP */
static int
cs_graft_count(int sheet)
{
    int i, c = 0;

    for (i = 0; i < CS_NUNLOCKS; i++)
        if (cs_unlocks[i].graft && cs_has_unlock(sheet, cs_unlocks[i].id))
            c++;
    return c;
}

/* Split an optional trailing "push N" off a command argument. Truncates arg
 * at the push token so what remains is the target, and returns N clamped to
 * 0..2 (a bare "push" means 1, "push 0" means no push). */
static int
cs_parse_push(char *arg)
{
    int i;

    for (i = 0; arg[i]; i++) {
        if ((i == 0 || arg[i - 1] == ' ') &&
            arg[i] == 'p' && arg[i + 1] == 'u' && arg[i + 2] == 's' &&
            arg[i + 3] == 'h' && (arg[i + 4] == ' ' || arg[i + 4] == '\0')) {
            int j = i, k = i + 4, n;

            while (j > 0 && arg[j - 1] == ' ') j--;
            arg[j] = '\0';                       /* trim "push ..." off target */
            while (arg[k] == ' ') k++;
            n = (arg[k] >= '0' && arg[k] <= '9') ? cs_atoi(arg + k) : 1;
            if (n < 1) return 0;
            return n > 2 ? 2 : n;
        }
    }
    return 0;
}

/* Assess an attack across `band` with `weapon`. Sets *pool_mod to the
 * attack-pool adjustment (-3 for a long shot, one band past reach) and
 * returns 1 if the target is reachable at all, 0 if out of reach. */
static int
cs_range_check(int weapon, int band, int *pool_mod)
{
    int gap = band - cs_reach(weapon);

    *pool_mod = 0;
    if (gap <= 0) return 1;
    if (gap == 1) { *pool_mod = -3; return 1; }
    return 0;
}

/* Resolve one attack: atk and def are sheets, weapon is the wielded item.
 * pool_mod adjusts the attack pool (a negative value is a long shot);
 * pd_bonus adds to the defender's Passive Defense (the +2 for a ranged
 * target that moved). Appends a third-person combat line to o, writes
 * BP/wounds/downed onto the defender sheet, and sets *out_down when the
 * defender is Downed. When allow_react is set, the defender's readied
 * guard (a held reaction) braces for +2 Passive Defense against this hit.
 * Returns the Net Damage dealt. */
static int
cs_attack_resolve(int atk, int def, int weapon, int pool_mod, int pd_bonus,
                  int allow_react, struct cs_out *o, int *out_down, int *out_edge,
                  int room)
{
    struct cs_derived dd;
    char an[32], dn[32], wn[24], skill[16], grade[12], skprop[40], stance[12];
    int sidx, apool, aroll, awild, dmgpts, droll, dwild, net, bp, wounds, wh, pd;
    int guarded = 0, rl, supp;

    *out_down = 0;
    if (out_edge) *out_edge = 0;
    supp = cs_geti(atk, "suppress", 0);   /* M41b: read before it is spent */
    cs_getstr(atk, "name", an, sizeof(an));
    cs_getstr(def, "name", dn, sizeof(dn));
    cs_getstr(weapon, "name", wn, sizeof(wn));
    cs_getstr(weapon, "skill", skill, sizeof(skill));

    sidx = cs_skill_idx(skill);
    if (sidx < 0) sidx = cs_skill_idx("brawl");
    cs_skname(skprop, cs_skills[sidx].name);
    apool = cs_geti(atk, cs_skills[sidx].attr, 0) + cs_geti(atk, skprop, 0)
            + pool_mod;
    /* M40a: Prone shifts the odds. A prone attacker is at -1D; a prone defender
     * is +1D to melee but -1D to ranged fire (rules Section 8). */
    {
        int ranged = cs_streq(skill, "firearms") || cs_streq(skill, "heavy");

        if (cs_geti(atk, "prone", 0)) apool -= 3;
        if (cs_geti(def, "prone", 0)) apool += ranged ? -3 : 3;
        if (cs_geti(atk, "shaken", 0)) apool -= 3;   /* M40b: Shaken is -1D */
        /* M41b: Suppressed is -1D on the attacker's next attack, then spent. */
        if (supp) { apool -= 3; cs_seti(atk, "suppress", 0); }
        /* M45b: Threat-Assessment Optics -- the marker gains +1D against a target
         * it has marked (the mark is stored on the target as the marker's sheet). */
        if (cs_geti(def, "marked_by", 0) == atk) apool += 3;
    }
    if (apool < 1) apool = 1;
    aroll = cs_roll(apool, &awild);

    cs_recalc(def, &dd);
    pd = dd.pd + cs_cover(def) + pd_bonus;
    /* M45b: Exposed is a tracked -2 Passive Defense on the target (Tactical
     * Co-Processor sets it; the transient ambush strike still uses pd_bonus). */
    if (cs_geti(def, "exposed", 0)) pd -= 2;

    /* a readied guard spends the defender's reaction to brace this hit */
    if (allow_react) {
        rl = cs_geti(def, "react_left", 0);
        cs_getstr(def, "stance", stance, sizeof(stance));
        if (rl > 0 && cs_streq(stance, "guard")) {
            pd += 2;
            cs_seti(def, "react_left", rl - 1);
            guarded = 1;
        }
    }

    cs_s(o, an);
    cs_s(o, " attacks ");
    cs_s(o, dn);
    cs_s(o, " with ");
    cs_s(o, wn[0] ? wn : "bare hands");
    if (pool_mod < 0) cs_s(o, " at long range");
    if (guarded) cs_s(o, " (guarded)");
    if (cs_geti(atk, "prone", 0)) cs_s(o, " (prone)");
    if (cs_geti(def, "prone", 0)) cs_s(o, " (target prone)");
    if (cs_geti(atk, "shaken", 0)) cs_s(o, " (shaken)");
    if (supp) cs_s(o, " (suppressed)");
    if (cs_geti(def, "exposed", 0)) cs_s(o, " (target exposed)");
    if (cs_geti(def, "marked_by", 0) == atk) cs_s(o, " (marked)");

    if (aroll < pd) {
        cs_s(o, " and misses.");
        if (awild < 0) cs_s(o, " [GLITCH]");
        else if (awild > 0) cs_s(o, " [EDGE]");
        /* M42b: Riposte -- a miss by 4 or more lets the defender spend a reaction
         * and 1 Grit for an immediate free strike at base weapon damage. The
         * strike disallows reactions so it cannot recurse. */
        if (allow_react && (pd - aroll) >= 4 && cs_has_unlock(def, "riposte")
            && cs_geti(def, "react_left", 0) > 0 && cs_grit_spend(def, 1) >= 0) {
            int rdown, rw = cs_geti(def, "wielded", 305);

            cs_seti(def, "react_left", cs_geti(def, "react_left", 0) - 1);
            cs_s(o, " [riposte!] ");
            cs_attack_resolve(def, atk, rw, 0, 0, 0, o, &rdown, 0, room);
        }
        return 0;
    }
    if (out_edge && awild > 0) *out_edge = 1;   /* M41b: a Tactical Edge on a hit */

    dmgpts = cs_geti(weapon, "dmg", 6);
    droll = cs_roll(dmgpts, &dwild);
    {
        /* M42c: Empathic Aegis. A bystanding ally in the room may spend a
         * reaction and 1 Grit to add its Charm dice to this hit's Soak. It reads
         * only when reactions are allowed, so a reaction's own strike is exempt. */
        int soak = dd.soak;

        if (allow_react && room > 0) {
            int a = 0;

            while ((a = sys_next(room, a)) != 0) {
                int ash = cs_sheet(a);

                if (ash == def || ash == atk || cs_geti(ash, "downed", 0)) continue;
                if (!cs_has_unlock(ash, "aegis")) continue;
                if (cs_geti(ash, "react_left", 0) <= 0) continue;
                if (cs_grit_spend(ash, 1) < 0) continue;
                cs_seti(ash, "react_left", cs_geti(ash, "react_left", 0) - 1);
                soak += cs_geti(ash, "cha", 0) / 3;
                cs_s(o, " [aegis]");
                break;                  /* one ally shields per hit */
            }
        }
        net = droll - soak;
    }
    if (net < 0) net = 0;

    /* M42a: Reflex Governor reaction. On a hit, the defender may spend a reaction
     * and 2 Grit to roll Wit+Cybertech against the attack total, cutting this
     * hit's Net Damage by its Wit dice on a success. Fires only when reactions
     * are allowed, so a reaction's own strike cannot trigger it. */
    if (allow_react && net > 0 && cs_has_unlock(def, "governor")
        && cs_geti(def, "react_left", 0) > 0) {
        int rp = cs_geti(def, "wit", 0) + cs_geti(def, "sk_cybertech", 0), rw, rr;

        if (cs_grit_spend(def, 2) >= 0) {
            cs_seti(def, "react_left", cs_geti(def, "react_left", 0) - 1);
            if (rp < 1) rp = 1;
            rr = cs_roll(rp, &rw);
            if (rr >= aroll) {
                int wd = cs_geti(def, "wit", 0) / 3;

                net -= wd;
                if (net < 0) net = 0;
                cs_s(o, " [governor -"); cs_i(o, wd); cs_s(o, "]");
            } else {
                cs_s(o, " [governor failed]");
            }
        }
    }

    cs_getstr(def, "grade", grade, sizeof(grade));
    if (cs_streq(grade, "mook") && net > 0) {
        sys_setprop(def, "downed", "1");
        *out_down = 1;
        cs_s(o, " and cuts it down. It drops.");
        return net;
    }
    if (net == 0) {
        cs_s(o, " but the armor turns it.");
        return 0;
    }

    wh = (net * 2 >= dd.maxbp);
    wounds = cs_geti(def, "wounds", 0);
    if (wh) {
        wounds++;
        if (wounds > 3) wounds = 3;
        cs_seti(def, "wounds", wounds);
    }

    bp = cs_geti(def, "bp", dd.maxbp) - net;
    cs_s(o, " for ");
    cs_i(o, net);
    if (bp <= 0 && cs_has_unlock(def, "defib") && !cs_geti(def, "defib_used", 0)) {
        /* M42b: Emergency Defibrillator -- a hit that would drop the defender to 0
         * BP leaves them at 1 instead, once, until a downtime repair clears the
         * burnout. The burnout flag is the limiter, so it always saves once. */
        cs_seti(def, "defib_used", 1);
        cs_seti(def, "bp", 1);
        cs_s(o, ". The defibrillator jolts them back to 1 BP! [defib]");
    } else if (bp <= 0) {
        cs_seti(def, "bp", 0);
        sys_setprop(def, "downed", "1");
        *out_down = 1;
        cs_s(o, ". Downed!");
    } else {
        cs_seti(def, "bp", bp);
        cs_s(o, " (");
        cs_condition(bp, dd.maxbp, wounds, o);
        cs_s(o, ")");
        if (wh) cs_s(o, " [WOUND]");
    }
    if (awild > 0 || dwild > 0) cs_s(o, " [EDGE]");
    else if (awild < 0 || dwild < 0) cs_s(o, " [GLITCH]");
    return net;
}

/* A leaving-melee reaction: an Engaged foe that still holds a reaction spends
 * it to strike a combatant withdrawing from melee (retreat or flight). */
static void
cs_free_strike(int foe, int target_sh, int room)
{
    int fsh = cs_sheet(foe);
    int rl = cs_geti(fsh, "react_left", 0);

    if (rl > 0 && !cs_geti(fsh, "downed", 0)) {
        struct cs_out f;
        int down, fw = cs_geti(fsh, "wielded", 305);

        cs_seti(fsh, "react_left", rl - 1);
        f.len = 0;
        cs_s(&f, "Free strike! ");
        cs_attack_resolve(fsh, target_sh, fw, 0, 0, 0, &f, &down, 0, room);
        sys_broadcast(room, cs_cstr(&f));
    }
}

/* A room's active scene is a fight by default; a social scene sets cb_mode to
 * "social". An unset flag reads as a fight, so plain combat needs no change. */
static int
cs_scene_social(int room)
{
    char m[12];

    cs_getstr(room, "cb_mode", m, sizeof(m));
    return cs_streq(m, "social");
}

/* End the actor's turn: mark it acted and wake the turn loop at once. The
 * __combat task records its own id in the room's cb_task prop; waking it by id
 * (rather than listening on the room) means combat never intercepts other
 * commands. An action resolves the moment it is taken, with no polling. */
static void
cs_end_turn(int room)
{
    cs_seti(room, "cb_acted", 1);
    sys_post(cs_geti(room, "cb_task", -1));
}

/* Refuse to act in the wrong kind of scene. `want_social` is what the caller
 * runs. When a scene of the other kind is live it prints a line and returns 1;
 * the caller then exits. No active scene, or a matching one, returns 0. */
static int
cs_wrong_mode(int room, int want_social)
{
    if (cs_geti(room, "cb_active", 0) != 1) return 0;
    if (cs_scene_social(room) == (want_social ? 1 : 0)) return 0;
    puts(want_social ? "This is no time for talk -- they're trading blows."
                     : "You are in the middle of an argument, not a fight.");
    return 1;
}

/* Resolve one social push (rules Section 16): src and tgt are sheets, skill is
 * the Charm approach ("command"/"negotiate"/"con"/"perform"), pool_mod adjusts
 * the push pool. Roll Charm + skill against the target's Passive Resolve; on a
 * hit roll the target's leverage pool against Composure and cut the remainder
 * from Resolve. Appends a third-person line to o, writes resolve/yielded onto
 * the target sheet, and sets *out_yield when the target reaches 0 Resolve.
 * Returns the Resolve lost. */
static int
cs_push_resolve(int src, int tgt, const char *skill, int pool_mod,
                struct cs_out *o, int *out_yield)
{
    char sn[32], tn[32], skprop[40];
    int sidx, pool, roll, wild, pr, lev, lroll, lwild, loss, res, maxr;

    *out_yield = 0;
    cs_getstr(src, "name", sn, sizeof(sn));
    cs_getstr(tgt, "name", tn, sizeof(tn));

    sidx = cs_skill_idx(skill);
    if (sidx < 0) sidx = cs_skill_idx("command");
    cs_skname(skprop, cs_skills[sidx].name);
    pool = cs_geti(src, "cha", 0) + cs_geti(src, skprop, 0) + pool_mod;
    if (pool < 1) pool = 1;
    roll = cs_roll(pool, &wild);

    pr = cs_passresolve(tgt);

    cs_s(o, sn);
    cs_s(o, " leans on ");
    cs_s(o, tn);
    cs_s(o, " (");
    cs_s(o, cs_skills[sidx].name);
    cs_s(o, ")");

    if (roll < pr) {
        cs_s(o, " but fails to move them.");
        if (wild < 0) cs_s(o, " [GLITCH]");
        else if (wild > 0) cs_s(o, " [EDGE]");
        return 0;
    }

    lev = cs_geti(tgt, "leverage", 9);
    lroll = cs_roll(lev, &lwild);
    loss = lroll - cs_composure(tgt);
    if (loss < 0) loss = 0;

    if (loss == 0) {
        cs_s(o, " but their poise holds.");
        if (wild > 0 || lwild > 0) cs_s(o, " [EDGE]");
        else if (wild < 0 || lwild < 0) cs_s(o, " [GLITCH]");
        return 0;
    }

    maxr = cs_max_resolve(tgt);
    res = cs_geti(tgt, "resolve", maxr) - loss;
    cs_s(o, " for ");
    cs_i(o, loss);
    if (res <= 0) {
        cs_seti(tgt, "resolve", 0);
        sys_setprop(tgt, "yielded", "1");
        *out_yield = 1;
        cs_s(o, ". They yield!");
    } else {
        cs_seti(tgt, "resolve", res);
        cs_s(o, " (");
        cs_i(o, res);
        cs_s(o, "/");
        cs_i(o, maxr);
        cs_s(o, " resolve)");
    }
    if (wild > 0 || lwild > 0) cs_s(o, " [EDGE]");
    else if (wild < 0 || lwild < 0) cs_s(o, " [GLITCH]");
    return loss;
}

#endif
