/* verb_cover.c : ChromeSix cover actions.
 *   takecover [heavy]  positional cover on yourself (+2 light, +4 heavy)
 *   cover <ally>       the Cover support action, +2 Passive Defense to an
 *                      ally until that ally's next turn
 * Both spend your action for the turn. */

#include "chromesix_verb.h"

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int sh = cs_sheet(self);
    char *verb = (char *)vm_args->verb;
    char *arg = (char *)vm_args->dobjstr;
    char nm[32];

    grant_accept();   /* writes combat state on the room */

    o.len = 0;
    if (cs_wrong_mode(room, 0)) _exit(0);
    if (cs_geti(room, "cb_active", 0) != 1) { puts("You are not in a fight."); _exit(0); }
    if (cs_geti(room, "cb_turn", -1) != self) { puts("It is not your turn."); _exit(0); }
    if (cs_geti(room, "cb_acted", 0) == 1) {
        puts("You have already acted this turn.");
        _exit(0);
    }

    if (cs_streq(verb, "takecover")) {
        int heavy;

        while (arg && *arg == ' ') arg++;
        heavy = arg && (arg[0] == 'h' || arg[0] == 'H');
        cs_seti(sh, "cover", heavy ? 4 : 2);
        cs_getstr(sh, "name", nm, sizeof(nm));
        cs_s(&o, nm);
        cs_s(&o, heavy ? " digs into heavy cover." : " ducks into cover.");
    } else {
        int ally;

        while (arg && *arg == ' ') arg++;
        if (!arg || !*arg) { puts("Cover whom?"); _exit(0); }
        ally = sys_objfind(arg);
        if (ally <= 0) { puts("You don't see that here."); _exit(0); }
        if (ally == self || cs_geti(cs_sheet(ally), "charid", -1) < 0) {
            puts("You can only cover an ally.");
            _exit(0);
        }
        cs_seti(cs_sheet(ally), "cover_bonus", 2);
        cs_getstr(sh, "name", nm, sizeof(nm));
        cs_s(&o, nm);
        cs_s(&o, " lays down covering fire for ");
        cs_getstr(cs_sheet(ally), "name", nm, sizeof(nm));
        cs_s(&o, nm);
        cs_s(&o, ".");
    }

    sys_broadcast(room, cs_cstr(&o));
    cs_seti(room, "cb_acted", 1);
    _exit(0);
}
