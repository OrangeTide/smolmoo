/* verb_ambush.c : ChromeSix surprise opener. Only when no fight is running:
 * roll the attacker's Stealth (Agility + Stealth) against the target's
 * Passive Perception. On a win the ambusher lands one free strike before
 * initiative, with the target Exposed (-2 Passive Defense, no reaction), and
 * the target then loses its first round's action. On a loss the target is
 * alert and the fight simply begins. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* object id of the __combat task verb (verbs.conf) */
#define COMBAT_VERB 420

void
_start(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int atk = cs_sheet(self);
    char *foename = (char *)vm_args->dobjstr;
    int foe, defsh, weapon, down, seen, stealth, wild;
    char roster[24], num[12];
    int i = 0, j;

    o.len = 0;
    while (foename && *foename == ' ') foename++;
    if (!foename || !*foename) { puts("Ambush whom?"); _exit(0); }
    if (cs_geti(room, "cb_active", 0) == 1) {
        puts("No time for stealth now -- the fight is already joined.");
        _exit(0);
    }
    foe = sys_objfind(foename);
    if (foe <= 0) { puts("You don't see that here."); _exit(0); }
    defsh = cs_sheet(foe);
    if (cs_geti(defsh, "downed", 0)) { puts("It is already down."); _exit(0); }

    /* Stealth vs Passive Perception decides whether the approach goes unseen */
    stealth = cs_geti(atk, "agi", 0) + cs_geti(atk, "sk_stealth", 0);
    if (stealth < 1) stealth = 1;
    seen = cs_roll(stealth, &wild) < cs_passperc(defsh);

    /* seed the shared combat record: the two of you, foe closed to melee */
    cs_itoa(num, self);
    for (j = 0; num[j]; j++) roster[i++] = num[j];
    roster[i++] = ',';
    cs_itoa(num, foe);
    for (j = 0; num[j]; j++) roster[i++] = num[j];
    roster[i] = '\0';

    sys_setprop(atk, "cover", "0");
    sys_setprop(atk, "cover_bonus", "0");
    sys_setprop(atk, "moved", "0");
    sys_setprop(atk, "stance", "");
    sys_setprop(defsh, "band", "0");
    sys_setprop(defsh, "surprised", seen ? "0" : "1");
    sys_setprop(room, "cb_mode", "fight");
    sys_setprop(room, "cb_roster", roster);
    cs_seti(room, "cb_round", 0);
    cs_seti(room, "cb_turn", 0);
    cs_seti(room, "cb_acted", 0);
    cs_seti(room, "cb_active", 1);

    if (seen) {
        cs_s(&o, "The ");
        cs_s(&o, foename);
        cs_s(&o, " spots you closing in -- no surprise!\n");
        cs_flush(&o);
    } else {
        /* free surprise strike: target Exposed, no reaction allowed */
        weapon = cs_geti(atk, "wielded", 305);
        cs_s(&o, "You strike from hiding! ");
        cs_attack_resolve(atk, defsh, weapon, 0, -2, 0, &o, &down);
        cs_s(&o, "\n");
        sys_broadcast(room, cs_cstr(&o));
    }

    {
        char h[72];
        if (cs_elf_hash(COMBAT_VERB, h, sizeof(h)) == 0)
            sys_spawn(h, 0, "");
    }
    _exit(0);
}
