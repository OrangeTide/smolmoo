/* verb_check.c : roll a ChromeSix skill check against a TN */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

void
_start(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int ch = cs_geti(self, "charid", -1);
    char *arg = (char *)vm_args->dobjstr;
    char skill[32], skprop[40];
    int i = 0, sk, tn, pts, roll, wild;
    int push, push_bonus = 0;

    o.len = 0;
    if (ch < 0) { puts("You have no character sheet."); _exit(0); }
    if (!arg || !*arg) { puts("Usage: check <skill> [tn] [push N]"); _exit(0); }

    push = cs_parse_push(arg);          /* strips a trailing "push N" */
    while (*arg == ' ') arg++;
    while (arg[i] && arg[i] != ' ' && i < 31) { skill[i] = arg[i]; i++; }
    skill[i] = '\0';
    while (arg[i] == ' ') i++;
    tn = (arg[i] >= '0' && arg[i] <= '9') ? cs_atoi(arg + i) : 10;

    sk = cs_skill_idx(skill);
    if (sk < 0) { puts("Unknown skill."); _exit(0); }

    if (push > 0) {
        push_bonus = cs_grit_push(ch, push);
        if (push_bonus < 0) {
            puts("You don't have the Grit to push that hard.");
            _exit(0);
        }
    }

    cs_skname(skprop, skill);
    pts = cs_geti(ch, cs_skills[sk].attr, 0) + cs_geti(ch, skprop, 0)
          + push_bonus - cs_crash_penalty(ch);

    roll = cs_roll(pts, &wild);

    cs_s(&o, "CHECK "); cs_s(&o, skill); cs_s(&o, ": pool ");
    cs_dice(&o, pts);
    cs_s(&o, " roll "); cs_i(&o, roll);
    cs_s(&o, " vs "); cs_i(&o, tn);
    if (roll >= tn) {
        cs_s(&o, " -> SUCCESS (margin "); cs_i(&o, roll - tn); cs_s(&o, ")");
    } else {
        cs_s(&o, " -> FAIL (short "); cs_i(&o, tn - roll); cs_s(&o, ")");
    }
    if (wild > 0) cs_s(&o, " [EDGE]");
    else if (wild < 0) cs_s(&o, " [GLITCH]");
    if (push > 0) { cs_s(&o, " [pushed +"); cs_i(&o, push); cs_s(&o, "D]"); }
    cs_s(&o, "\n");

    cs_flush(&o);
    _exit(0);
}
