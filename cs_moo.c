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

/* Pay out any short-rest Body Points owed, before a readout shows them. This
 * writes the sheet, but a player owns their own sheet, so a non-setuid verb
 * may call it (as the C status verb does). */
void
cs_moo_rest(const char *sheet, const char *room)
{
    cs_rest((int)sheet, (int)room);
}
