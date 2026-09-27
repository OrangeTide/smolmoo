/* verb_sheet.c : display a ChromeSix character sheet */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

int
main(void)
{
    struct cs_out o;
    struct cs_derived d;
    int self = vm_args->player;
    int ch = cs_geti(self, "charid", -1);
    char nm[64];
    int any, n;

    o.len = 0;
    if (ch < 0) { puts("You have no character sheet."); _exit(0); }

    n = sys_getprop(ch, "name", nm, sizeof(nm) - 1);
    if (n < 0) n = 0;
    nm[n] = '\0';

    cs_rest(ch, vm_args->room);      /* pay out any short-rest BP before display */
    cs_recalc(ch, &d);

    cs_s(&o, "=== "); cs_s(&o, nm); cs_s(&o, " ===\n");
    cs_s(&o, "Agility "); cs_dice(&o, cs_geti(ch, "agi", 0));
    cs_s(&o, "  Might "); cs_dice(&o, cs_geti(ch, "mig", 0));
    cs_s(&o, "  Wit "); cs_dice(&o, cs_geti(ch, "wit", 0));
    cs_s(&o, "  Charm "); cs_dice(&o, cs_geti(ch, "cha", 0));
    cs_s(&o, "\n");
    cs_s(&o, "Defense "); cs_i(&o, d.pd);
    cs_s(&o, "  Soak "); cs_i(&o, d.soak);
    cs_s(&o, "  BP "); cs_i(&o, cs_geti(ch, "bp", d.maxbp));
    cs_s(&o, "/"); cs_i(&o, d.maxbp);
    cs_s(&o, "  Grit "); cs_i(&o, cs_geti(ch, "grit", d.maxgrit));
    cs_s(&o, "/"); cs_i(&o, d.maxgrit);
    cs_s(&o, "\n");

    cs_s(&o, "Creds "); cs_i(&o, cs_geti(ch, "money", 0));
    cs_s(&o, "  Stims "); cs_i(&o, cs_geti(ch, "stims", 0));
    cs_s(&o, "  CP "); cs_i(&o, cs_geti(ch, "cp", 0));
    n = cs_geti(ch, "crash", 0);
    if (n > 0) { cs_s(&o, "  Crash -"); cs_i(&o, n); cs_s(&o, "D"); }
    cs_s(&o, "\n");

    {
        int cap = cs_carry(ch), load = cs_load(self);

        cs_s(&o, "Load "); cs_i(&o, load); cs_s(&o, "/"); cs_i(&o, cap);
        if (load > cap * 2) cs_s(&o, " (overloaded)");
        else if (load > cap) cs_s(&o, " (encumbered)");
        cs_s(&o, "\n");
    }

    {                           /* active combat conditions, when any (M40) */
        struct cs_out c;

        c.len = 0;
        cs_cond_list(ch, &c);
        if (c.len) {
            cs_s(&o, "Conditions: ");
            cs_s(&o, cs_cstr(&c));
            cs_s(&o, "\n");
        }
    }

    {
        char mv[128];
        int mn = sys_getprop(ch, "maneuvers", mv, sizeof(mv) - 1);
        if (mn > 0) {
            mv[mn] = '\0';
            cs_s(&o, "Maneuvers: "); cs_s(&o, mv); cs_s(&o, "\n");
        }
    }

    any = 0;
    for (int i = 0; i < 20; i++) {
        char pn[40];
        int v;
        cs_skname(pn, cs_skills[i].name);
        v = cs_geti(ch, pn, 0);
        if (v > 0) {
            if (!any) { cs_s(&o, "Skills:"); any = 1; }
            cs_s(&o, "  "); cs_s(&o, cs_skills[i].name); cs_s(&o, " ");
            cs_dice(&o, v);
        }
    }
    if (any) cs_s(&o, "\n");

    /* Faction standing (Section 14) and earned clearances (M51a), each a shared
     * line also used by the contracts view. Fade idle standing first (M39c). */
    cs_decay(ch);
    cs_standing_line(ch, &o);
    cs_clearances_line(ch, &o);

    cs_flush(&o);
    _exit(0);
}
