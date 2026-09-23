/* verb_stand.c : stand up from Prone (M40a). Clears the `prone` condition. In a
 * fight standing is your action for the turn (you stand with a Move), so it ends
 * the turn; out of a fight it just gets you up. It writes the room turn state
 * when in combat, so it is setuid and elevates through grant_accept. */
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
    char nm[32];

    grant_accept();

    if (!cs_geti(sh, "prone", 0)) { puts("You are not prone."); _exit(0); }
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
    cs_seti(sh, "prone", 0);
    cs_getstr(sh, "name", nm, sizeof(nm));
    o.len = 0;
    cs_s(&o, nm);
    cs_s(&o, " gets to their feet.");
    sys_broadcast(room, cs_cstr(&o));
    if (infight) cs_end_turn(room);
    _exit(0);
}
