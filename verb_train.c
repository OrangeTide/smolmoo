/* verb_train.c : spend Character Points to raise a skill (M35b). A skill is
 * stored as sk_<name> points on the sheet, and its rating is the governing
 * attribute plus those points. Raising it one pip costs CP equal to the skill's
 * current rating in dice (at least one), the OpenD6 advancement rule, and is
 * capped like creation at attribute + 6 skill points. The player owns the sheet,
 * so this runs at caller authority like chargen, not setuid. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int ch = cs_geti(self, "charid", -1);
    char *arg = (char *)vm_args->dobjstr;
    char skill[32], skprop[40];
    int i = 0, sk, cur, attr, dice, r, cost, cp;

    o.len = 0;
    if (ch < 0) { puts("You have no character sheet."); _exit(0); }
    if (!arg || !*arg) { puts("Usage: train <skill>"); _exit(0); }

    while (*arg == ' ') arg++;
    while (arg[i] && arg[i] != ' ' && i < 31) { skill[i] = arg[i]; i++; }
    skill[i] = '\0';

    sk = cs_skill_idx(skill);
    if (sk < 0) { puts("Unknown skill."); _exit(0); }

    cs_skname(skprop, skill);
    cur = cs_geti(ch, skprop, 0);
    attr = cs_geti(ch, cs_skills[sk].attr, 0);
    if (cur + 1 > attr + 6) {
        cs_s(&o, "Your "); cs_s(&o, skill);
        cs_s(&o, " is as high as training can take it.\n");
        cs_flush(&o);
        _exit(0);
    }

    dice = cs_divmod(attr + cur, 3, &r);        /* current rating in dice */
    cost = dice < 1 ? 1 : dice;
    cp = cs_geti(ch, "cp", 0);
    if (cp < cost) {
        cs_s(&o, "That costs "); cs_i(&o, cost);
        cs_s(&o, " CP; you have "); cs_i(&o, cp); cs_s(&o, ".\n");
        cs_flush(&o);
        _exit(0);
    }

    cs_seti(ch, "cp", cp - cost);
    cs_seti(ch, skprop, cur + 1);

    cs_s(&o, "You train "); cs_s(&o, skill); cs_s(&o, " to ");
    cs_dice(&o, attr + cur + 1);
    cs_s(&o, " for "); cs_i(&o, cost); cs_s(&o, " CP (");
    cs_i(&o, cp - cost); cs_s(&o, " left).\n");
    cs_flush(&o);
    _exit(0);
}
