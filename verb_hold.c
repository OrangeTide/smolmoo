/* verb_hold.c : ChromeSix hold action -- pass your combat turn deliberately
 * instead of waiting out the turn timeout. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    char nm[32];

    /* Combat state lives on the room (owner #0); elevate to write it. */
    grant_accept();

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

    cs_getstr(cs_sheet(self), "name", nm, sizeof(nm));
    o.len = 0;
    cs_s(&o, nm);
    cs_s(&o, " holds, watching.");
    sys_broadcast(room, cs_cstr(&o));
    cs_end_turn(room);
    _exit(0);
}
