/* verb_treat.c : mend a wound at a clinic (M39b). Wounds do not clear on a short
 * rest (M39a); a living character needs care, and the death clinic's `recover`
 * only serves the dead. A clinic is a builder object in the room marked
 * `clinic`=1, carrying an optional `treat_fee` (default TREAT_FEE). `treat` spends
 * the fee, then rolls the better of the patient's Medicine or Cybertech against a
 * TN that climbs with the wound, and on success clears one `wounds` step. It
 * refuses with no clinic, no wound, or too few creds. The patient owns the sheet,
 * so this runs at caller authority, not setuid. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

#define TREAT_FEE 40            /* default clinic fee per wound step */

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int ch = cs_geti(self, "charid", -1);
    int clinic = 0, e = 0, wounds, fee, money, pool, tn, roll, wild;
    int mi = cs_skill_idx("medicine"), ci = cs_skill_idx("cybertech");
    int med, cyb, best;
    char skm[40], skc[40];

    o.len = 0;
    if (ch < 0) { puts("You have no character sheet."); _exit(0); }

    while ((e = sys_next(room, e)) != 0) {
        if (cs_geti(e, "clinic", 0)) { clinic = e; break; }
    }
    if (!clinic) { puts("There is no clinic here."); _exit(0); }

    wounds = cs_geti(ch, "wounds", 0);
    if (wounds <= 0) { puts("You have no wounds to treat."); _exit(0); }

    fee = cs_geti(clinic, "treat_fee", TREAT_FEE);
    money = cs_geti(ch, "money", 0);
    if (money < fee) {
        cs_s(&o, "Treatment costs ");
        cs_i(&o, fee);
        cs_s(&o, " creds; you have ");
        cs_i(&o, money);
        cs_s(&o, ".\n");
        cs_flush(&o);
        _exit(0);
    }

    /* pay the clinic, then attempt the mend: the better of Medicine or Cybertech
     * (both Wit skills) against a TN that climbs with the wound. */
    cs_seti(ch, "money", money - fee);
    cs_skname(skm, "medicine");
    cs_skname(skc, "cybertech");
    med = cs_geti(ch, cs_skills[mi].attr, 0) + cs_geti(ch, skm, 0);
    cyb = cs_geti(ch, cs_skills[ci].attr, 0) + cs_geti(ch, skc, 0);
    best = med > cyb ? med : cyb;
    pool = best - cs_crash_penalty(ch);
    tn = 8 + wounds * 4;                 /* 12 / 16 / 20 for a 1 / 2 / 3 wound */
    roll = cs_roll(pool, &wild);

    if (roll >= tn) {
        cs_seti(ch, "wounds", wounds - 1);
        cs_s(&o, "The clinic treats your wound (");
        cs_i(&o, wounds - 1);
        cs_s(&o, " left).\n");
    } else {
        cs_s(&o, "The treatment does not take; the wound holds.\n");
    }
    cs_flush(&o);
    _exit(0);
}
