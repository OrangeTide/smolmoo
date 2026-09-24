/* verb_status.c : a one-line vitals readout (rules Section 12). The HP and Grit
 * bars, the wound ladder, and any active conditions, for a glance without opening
 * the full sheet. Reads only the caller's own sheet, so it is non-setuid. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

int
main(void)
{
    struct cs_out o, c;
    struct cs_derived d;
    int self = vm_args->player;
    int ch = cs_geti(self, "charid", -1);
    int wounds, i;

    o.len = 0;
    if (ch < 0) {
        puts("You have no character sheet. Type 'chargen' to create one.");
        _exit(0);
    }
    /* M43c: a sheet exists but chargen has not run (no `made` marker; the
     * prototype supplies default stats, so guide rather than show a default
     * readout as if it were the player's own character). */
    if (!cs_geti(ch, "made", 0)) {
        puts("You haven't set up your character yet. Type 'chargen' to begin.");
        _exit(0);
    }

    cs_rest(ch, vm_args->room);      /* pay out short-rest BP before the readout */
    cs_recalc(ch, &d);

    cs_meter(&o, "HP", cs_geti(ch, "bp", d.maxbp), d.maxbp);
    cs_s(&o, "  ");
    cs_meter(&o, "GRIT", cs_geti(ch, "grit", d.maxgrit), d.maxgrit);

    wounds = cs_geti(ch, "wounds", 0);
    if (wounds > 3) wounds = 3;
    cs_s(&o, "  WOUNDS ");
    for (i = 0; i < 3; i++) cs_s(&o, i < wounds ? "o" : ".");

    c.len = 0;
    cs_cond_list(ch, &c);
    if (c.len) { cs_s(&o, "  ("); cs_s(&o, cs_cstr(&c)); cs_s(&o, ")"); }
    cs_s(&o, "\n");

    cs_flush(&o);
    _exit(0);
}
