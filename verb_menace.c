/* verb_menace.c : two combat maneuvers that impose a condition on a foe (M40b).
 * `stun <foe>` spends a Grit on a stunning blow, an opposed Might-plus-Brawl roll
 * against an engaged foe that, on a win, leaves it Stunned (it loses its next
 * action). `menace <foe>` is an intimidation, Charm-plus-Command against the
 * foe's Wit that, on a win, leaves it Shaken (-1D until it rallies). Both spend
 * your action for the turn. Setuid: they write the room and the foe's sheet. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* is `foe` on the room's combat roster? */
static int
on_roster(int room, int foe)
{
    int ids[8], k = cs_roster_foes(room, 0, ids, 8), i;

    for (i = 0; i < k; i++)
        if (ids[i] == foe) return 1;
    return 0;
}

/* the higher of Might or Agility, in points */
static int
best_phys(int sheet)
{
    int m = cs_geti(sheet, "mig", 0), a = cs_geti(sheet, "agi", 0);

    return m > a ? m : a;
}

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int atk = cs_sheet(self);
    char *verb = (char *)vm_args->verb;
    char *foename = (char *)vm_args->dobjstr;
    int stun = cs_streq(verb, "stun");
    int foe, defsh, apool, dpool, aroll, droll, awild, dwild;
    char an[32], dn[32];

    grant_accept();

    while (foename && *foename == ' ') foename++;
    if (!foename || !*foename) { puts(stun ? "Stun whom?" : "Menace whom?"); _exit(0); }
    if (cs_wrong_mode(room, 0)) _exit(0);
    if (cs_geti(room, "cb_active", 0) != 1) {
        puts("You are not in a fight.");
        _exit(0);
    }
    if (cs_geti(room, "cb_turn", -1) != self) {
        puts("It is not your turn.");
        _exit(0);
    }
    if (cs_geti(room, "cb_acted", 0) == 1) {
        puts("You have already acted this turn.");
        _exit(0);
    }
    foe = sys_objfind(foename);
    if (foe <= 0) { puts("You don't see that here."); _exit(0); }
    defsh = cs_sheet(foe);
    if (cs_geti(defsh, "downed", 0)) { puts("It is already down."); _exit(0); }
    if (!on_roster(room, foe)) { puts("They are not in this fight."); _exit(0); }
    if (stun && cs_geti(defsh, "band", 0) != 0) {
        puts("You must be in melee to stun them.");
        _exit(0);
    }

    if (stun) {
        if (cs_grit_spend(atk, 1) < 0) {
            puts("You lack the Grit for a stunning blow.");
            _exit(0);
        }
        apool = cs_geti(atk, "mig", 0) + cs_geti(atk, "sk_brawl", 0)
                - cs_crash_penalty(atk);
        dpool = best_phys(defsh);
    } else {
        apool = cs_geti(atk, "cha", 0) + cs_geti(atk, "sk_command", 0)
                - cs_crash_penalty(atk);
        dpool = cs_geti(defsh, "wit", 0);
    }
    if (apool < 1) apool = 1;
    aroll = cs_roll(apool, &awild);
    droll = cs_roll(dpool, &dwild);

    cs_getstr(atk, "name", an, sizeof(an));
    cs_getstr(defsh, "name", dn, sizeof(dn));
    o.len = 0;
    cs_s(&o, an);
    if (aroll > droll) {
        if (stun) {
            cs_seti(defsh, "stunned", 1);
            cs_s(&o, " lands a stunning blow on ");
            cs_s(&o, dn);
            cs_s(&o, "; it reels, stunned.");
        } else {
            cs_seti(defsh, "shaken", 1);
            cs_s(&o, " menaces ");
            cs_s(&o, dn);
            cs_s(&o, "; it is shaken.");
        }
    } else {
        cs_s(&o, stun ? " swings to stun " : " menaces ");
        cs_s(&o, dn);
        cs_s(&o, stun ? ", but it shrugs it off." : ", but it holds its nerve.");
    }
    sys_broadcast(room, cs_cstr(&o));
    cs_end_turn(room);
    _exit(0);
}
