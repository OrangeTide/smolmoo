/* cs_moo.c : ChromeSix gameplay-library bridge for MooScript verbs. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

/* The ChromeSix helpers in chromesix_verb.h are static C, inlined into each C
 * verb. A MooScript verb cannot include a C header, so this file compiles the
 * subset it needs into the MooScript runtime (MOO_RT_OBJS) and exposes thin,
 * non-static wrappers a verb binds with `extern`. Each wrapper converts the
 * MooScript ABI (obj = integer id carried as a pointer, str = length-prefixed
 * moo_str) into the plain C the helpers expect. Only these wrappers and the
 * static helpers they reach land in the object; the compiler drops the rest of
 * the header, so a verb that binds none of this pays nothing. */

#include "chromesix_verb.h"

struct moo_str {
    int len;
    const char *data;
};

extern void *__moo_arena_alloc(int size);

static void
moo_cstr(struct moo_str *s, char *buf, int bufsz)
{
    int n = s->len;

    if (n >= bufsz)
        n = bufsz - 1;
    for (int i = 0; i < n; i++)
        buf[i] = s->data[i];
    buf[n] = 0;
}

/* Copy a host byte range into an arena-allocated MooScript string, for a
 * wrapper that hands a built line back to a verb. */
static struct moo_str *
moo_mkstr(const char *src, int len)
{
    struct moo_str *r = __moo_arena_alloc(8);
    char *b = __moo_arena_alloc(len + 1);
    int i;

    for (i = 0; i < len; i++)
        b[i] = src[i];
    b[len] = 0;
    r->len = len;
    r->data = b;
    return r;
}

/* Integer property read with a fallback, the MooScript counterpart of reading
 * obj.prop: a bare prop read yields 0 for a missing value, while this returns
 * `dflt`, which several readouts need (bp defaults to maxbp, and so on). */
int
cs_moo_geti(const char *obj, struct moo_str *name, int dflt)
{
    char buf[64];

    moo_cstr(name, buf, sizeof(buf));
    return cs_geti((int)obj, buf, dflt);
}

/* The four derived statistics, each recomputed from the sheet on demand.
 * MooScript has no C struct, so cs_recalc's cs_derived is unpacked into one
 * accessor per field: PD, Soak, max BP, max Grit (rules Section 4). */
int
cs_moo_pd(const char *obj)
{
    struct cs_derived d;

    cs_recalc((int)obj, &d);
    return d.pd;
}

int
cs_moo_soak(const char *obj)
{
    struct cs_derived d;

    cs_recalc((int)obj, &d);
    return d.soak;
}

int
cs_moo_maxbp(const char *obj)
{
    struct cs_derived d;

    cs_recalc((int)obj, &d);
    return d.maxbp;
}

int
cs_moo_maxgrit(const char *obj)
{
    struct cs_derived d;

    cs_recalc((int)obj, &d);
    return d.maxgrit;
}

/* Pay out any short-rest recovery owed, both Body Points and Resolve, before a
 * readout shows them. This writes the sheet, but a player owns their own sheet,
 * so a non-setuid verb may call it (as the C status verb does). */
void
cs_moo_rest(const char *sheet, const char *room)
{
    cs_rest((int)sheet, (int)room);
    cs_resolve_rest((int)sheet, (int)room);
}

/* --- sheet readout --- */

/* Carry rating (Might-based) and idle-standing decay, for the sheet. */
int
cs_moo_carry(const char *sheet)
{
    return cs_carry((int)sheet);
}

void
cs_moo_decay(const char *sheet)
{
    cs_decay((int)sheet);
}

/* The faction-standing and earned-clearance lines, each a composite the C
 * library already builds (cs_standing_line, cs_clearances_line) with its own
 * vocabulary (standing words, flag labels). Rather than reimplement that
 * vocabulary in MooScript, the wrapper runs the builder and returns the line as
 * a string, empty when the sheet has no standing or no flags. */
struct moo_str *
cs_moo_standing_line(const char *sheet)
{
    struct cs_out o;

    o.len = 0;
    cs_standing_line((int)sheet, &o);
    if (o.len > 0 && o.buf[o.len - 1] == '\n')   /* the verb's tell adds one */
        o.len--;
    return moo_mkstr(o.buf, o.len);
}

struct moo_str *
cs_moo_clearances_line(const char *sheet)
{
    struct cs_out o;

    o.len = 0;
    cs_clearances_line((int)sheet, &o);
    if (o.len > 0 && o.buf[o.len - 1] == '\n')
        o.len--;
    return moo_mkstr(o.buf, o.len);
}

/* The skills list: each of the 20 skills the sheet carries a rank in, as
 * "Skills:  name ND  ...". The loop lived inline in verb_sheet.c; it uses the
 * cs_skills table and the sk_ property naming, both C-side, so it is kept here
 * rather than rebuilt in MooScript. Empty when the sheet has no ranked skill. */
struct moo_str *
cs_moo_skills_line(const char *sheet)
{
    struct cs_out o;
    int ch = (int)sheet;
    int any = 0;
    int i;

    o.len = 0;
    for (i = 0; i < 20; i++) {
        char pn[40];
        int v;

        cs_skname(pn, cs_skills[i].name);
        v = cs_geti(ch, pn, 0);
        if (v > 0) {
            if (!any) {
                cs_s(&o, "Skills:");
                any = 1;
            }
            cs_s(&o, "  ");
            cs_s(&o, cs_skills[i].name);
            cs_s(&o, " ");
            cs_dice(&o, v);
        }
    }
    return moo_mkstr(o.buf, o.len);
}

/* --- skill check --- */

/* Push notation parse: returns the pushed dice (1 or 2) a "push N" tail asks
 * for, 0 when absent. Non-mutating: cs_parse_push trims its argument, so it
 * runs on a copy here. */
int
cs_moo_parse_push(struct moo_str *arg)
{
    char buf[256];

    moo_cstr(arg, buf, sizeof(buf));
    return cs_parse_push(buf);
}

/* The skill's index in the catalogue, or -1 for an unknown name. */
int
cs_moo_skill_idx(struct moo_str *name)
{
    char nm[32];

    moo_cstr(name, nm, sizeof(nm));
    return cs_skill_idx(nm);
}

/* Spend Grit to push a roll: returns the extra pips (n*3) on success, or -1
 * when the sheet lacks the Grit. Writes the sheet, so the verb must own it. */
int
cs_moo_grit_push(const char *sheet, int n)
{
    return cs_grit_push((int)sheet, n);
}

/* The dice pool for a check: the skill's governing attribute plus the skill
 * rank, plus any push bonus, less the Crash penalty (rules Section 10). The
 * name must be a known skill (the verb checks cs_moo_skill_idx first). */
int
cs_moo_skill_pool(const char *sheet, struct moo_str *name, int push_bonus)
{
    char nm[32], skprop[40];
    int ch = (int)sheet;
    int sk;

    moo_cstr(name, nm, sizeof(nm));
    sk = cs_skill_idx(nm);
    if (sk < 0)
        return 0;
    cs_skname(skprop, nm);
    return cs_geti(ch, cs_skills[sk].attr, 0) + cs_geti(ch, skprop, 0)
           + push_bonus - cs_crash_penalty(ch);
}

/* Roll a pool and report the wild-die result alongside the total. cs_roll
 * returns the total and sets the wild flag through a pointer; MooScript has no
 * out-parameter, so the wild flag is stashed for cs_moo_wild to read on the
 * next call. Verb execution is single-threaded, so the pair is always read back
 * before another roll. */
static int cs_moo_last_wild;

int
cs_moo_roll(int pts)
{
    int wild;
    int total = cs_roll(pts, &wild);

    cs_moo_last_wild = wild;
    return total;
}

int
cs_moo_wild(void)
{
    return cs_moo_last_wild;
}
