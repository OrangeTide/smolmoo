/* verb_inventory.c : the flatten view of body slots and their contents. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* copy the part name (the leading letters, before the index) of a tag */
static void
part_name(const char *tag, char *dst, int sz)
{
    int i = 0;

    while (tag[i] && (tag[i] < '0' || tag[i] > '9') && i < sz - 1)
        dst[i] = tag[i], i++;
    dst[i] = '\0';
}

/* copy the next comma-separated tag out of an anatomy string starting at *pi,
 * returning its length (0 at the end). */
static int
next_tag(const char *an, int *pi, char *tag, int sz)
{
    int i = *pi, j = 0;

    while (an[i] == ',') i++;
    while (an[i] && an[i] != ',' && j < sz - 1)
        tag[j++] = an[i++];
    tag[j] = '\0';
    *pi = i;
    return j;
}

int
main(void)
{
    struct cs_out o;
    int me = vm_args->player;
    char an[256], tag[16], nm[32];
    char pn[16][12];
    int ptot[16], pocc[16], nparts = 0;
    int i, it;

    o.len = 0;
    cs_anatomy(me, an, sizeof(an));

    /* aggregate per-part totals and occupancy */
    i = 0;
    while (next_tag(an, &i, tag, sizeof(tag))) {
        char part[12];
        int occupied = cs_slot_holder(me, tag) != 0;
        int k, found = -1;

        part_name(tag, part, sizeof(part));
        for (k = 0; k < nparts; k++)
            if (cs_streq(pn[k], part)) { found = k; break; }
        if (found < 0 && nparts < 16) {
            found = nparts++;
            for (k = 0; part[k] && k < 11; k++) pn[found][k] = part[k];
            pn[found][k] = '\0';
            ptot[found] = 0;
            pocc[found] = 0;
        }
        if (found >= 0) {
            ptot[found]++;
            if (occupied) pocc[found]++;
        }
    }

    /* summary line: each part as occupied/total */
    cs_s(&o, "slots:");
    for (i = 0; i < nparts; i++) {
        cs_s(&o, " ");
        cs_s(&o, pn[i]);
        cs_s(&o, " ");
        cs_i(&o, pocc[i]);
        cs_s(&o, "/");
        cs_i(&o, ptot[i]);
    }
    cs_s(&o, "\n");
    cs_flush(&o);

    /* one line per occupied slot, in anatomy order; a container lists its
     * contents against its capacity */
    i = 0;
    while (next_tag(an, &i, tag, sizeof(tag))) {
        int cap, child, count, first;

        it = cs_slot_holder(me, tag);
        if (!it) continue;
        o.len = 0;
        cs_getstr(it, "name", nm, sizeof(nm));
        cs_s(&o, tag);
        cs_s(&o, " ");
        cs_s(&o, nm);
        if (cs_uses_ammo(it)) {
            cs_s(&o, " [");
            cs_i(&o, cs_geti(it, "ammo", 0));
            cs_s(&o, "/");
            cs_i(&o, cs_geti(it, "clip", 0));
            cs_s(&o, "]");
        }
        cap = cs_geti(it, "cap", 0);
        if (cap > 0) {
            count = cs_count(it);
            cs_s(&o, " (");
            cs_i(&o, count);
            cs_s(&o, "/");
            cs_i(&o, cap);
            cs_s(&o, ")");
            first = 1;
            child = 0;
            while ((child = sys_next(it, child)) != 0) {
                char cnm[32];

                cs_getstr(child, "name", cnm, sizeof(cnm));
                cs_s(&o, first ? ": " : ", ");
                first = 0;
                cs_s(&o, cnm);
            }
        }
        cs_s(&o, "\n");
        cs_flush(&o);
    }
    _exit(0);
}
