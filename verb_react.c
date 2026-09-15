/* verb_react.c : ready a between-turns reaction (ChromeSix).
 *   guard         ready the defensive brace: +2 Passive Defense against the
 *                 next attack before your next turn
 *   ready <name>  arm a named reaction
 * Readying is free and does not spend your action. Only guard is wired in
 * this pass; the Grit-costed reactions arrive with the Grit gauge. */

#include "chromesix_verb.h"

void
_start(void)
{
    int self = vm_args->player;
    int room = vm_args->room;
    int sh = cs_sheet(self);
    char *verb = (char *)vm_args->verb;
    char *arg = (char *)vm_args->dobjstr;

    if (cs_wrong_mode(room, 0)) _exit(0);
    if (cs_geti(room, "cb_active", 0) != 1) { puts("You are not in a fight."); _exit(0); }

    if (!cs_streq(verb, "guard")) {
        while (arg && *arg == ' ') arg++;
        if (!arg || !*arg) { puts("Ready which reaction?"); _exit(0); }
        if (!cs_streq(arg, "guard")) {
            puts("You cannot ready that reaction yet.");
            _exit(0);
        }
    }

    sys_setprop(sh, "stance", "guard");
    puts("You set yourself to guard, ready to brace the next blow.");
    _exit(0);
}
