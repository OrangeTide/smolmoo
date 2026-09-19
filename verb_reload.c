/* verb_reload.c : refill the wielded weapon's magazine from supplies. In a
 * fight it is your action for the turn; out of one it is free. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int sh = cs_sheet(self);
    int weapon = cs_geti(sh, "wielded", 305);
    int fighting = cs_geti(room, "cb_active", 0) == 1;
    char nm[32];

    grant_accept();   /* writes the weapon's ammo and combat state on the room */

    o.len = 0;
    if (!cs_uses_ammo(weapon)) {
        puts("That weapon takes no ammunition.");
        _exit(0);
    }
    if (fighting) {
        if (cs_geti(room, "cb_turn", -1) != self) {
            puts("It is not your turn.");
            _exit(0);
        }
        if (cs_geti(room, "cb_acted", 0) == 1) {
            puts("You have already acted this turn.");
            _exit(0);
        }
    }
    if (cs_geti(weapon, "ammo", 0) >= cs_geti(weapon, "clip", 0)) {
        puts("It is already loaded.");
        _exit(0);
    }
    cs_reload_weapon(weapon);
    cs_getstr(weapon, "name", nm, sizeof(nm));
    cs_s(&o, "You reload the ");
    cs_s(&o, nm);
    cs_s(&o, ".\n");
    cs_flush(&o);
    if (fighting) cs_seti(room, "cb_acted", 1);
    _exit(0);
}
