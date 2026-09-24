/* verb_stand.c : recover from a condition on yourself. `stand` (M40a) clears the
 * `prone` condition (you stand with a Move). `staunch` (M40c) ends Ongoing
 * damage, clearing the `bleed` condition. `break` (M45c) frees you from a grapple,
 * clearing the `held` condition. In a fight any of them is your action for the
 * turn and ends it; out of a fight it just gets you sorted. It writes the
 * room turn state when in combat, so it is setuid and elevates through
 * grant_accept. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int sh = cs_sheet(self);
    int infight = cs_geti(room, "cb_active", 0) == 1;
    int staunch = cs_streq((char *)vm_args->verb, "staunch");
    int breakfree = cs_streq((char *)vm_args->verb, "break");
    char nm[32];

    grant_accept();

    if (staunch) {
        if (!cs_geti(sh, "bleed", 0)) { puts("You are not bleeding."); _exit(0); }
    } else if (breakfree) {
        if (!cs_geti(sh, "held", 0)) { puts("You are not held."); _exit(0); }
    } else {
        if (!cs_geti(sh, "prone", 0)) { puts("You are not prone."); _exit(0); }
    }
    if (infight) {
        if (cs_geti(room, "cb_turn", -1) != self) {
            puts("It is not your turn.");
            _exit(0);
        }
        if (cs_geti(room, "cb_acted", 0) == 1) {
            puts("You have already acted this turn.");
            _exit(0);
        }
    }
    cs_getstr(sh, "name", nm, sizeof(nm));
    o.len = 0;
    cs_s(&o, nm);
    if (staunch) {
        cs_seti(sh, "bleed", 0);
        cs_s(&o, " staunches the bleeding.");
    } else if (breakfree) {
        cs_seti(sh, "held", 0);
        cs_s(&o, " breaks free of the hold.");
    } else {
        cs_seti(sh, "prone", 0);
        cs_s(&o, " gets to their feet.");
    }
    sys_broadcast(room, cs_cstr(&o));
    if (infight) cs_end_turn(room);
    _exit(0);
}
