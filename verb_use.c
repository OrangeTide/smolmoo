/* verb_use.c : ChromeSix consumables and maneuvers. Covers stims (restore
 * Grit, stack Crash past the safe limit) and the CP-unlock maneuvers a
 * character has learned, each spending its Grit cost as the turn's action. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* Gate a combat action to the actor's own turn. Returns 1 if allowed to act
 * (a fight is running and it is the actor's unspent turn), else prints why
 * and returns 0. Sets *infight to whether a fight is running at all. */
static int
turn_ok(int room, int self, int *infight)
{
    *infight = (cs_geti(room, "cb_active", 0) == 1);
    if (!*infight) return 0;
    if (cs_geti(room, "cb_turn", -1) != self) {
        puts("It is not your turn.");
        return 0;
    }
    if (cs_geti(room, "cb_acted", 0) == 1) {
        puts("You have already acted this turn.");
        return 0;
    }
    return 1;
}

void
_start(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int sh = cs_sheet(self);
    char *arg = (char *)vm_args->dobjstr;
    char word[16], nm[32];
    int i = 0, infight, crashed;

    grant_accept();   /* writes combat state on the room */

    o.len = 0;
    while (arg && *arg == ' ') arg++;
    if (!arg || !*arg) { puts("Use what?"); _exit(0); }
    while (arg[i] && arg[i] != ' ' && i < 15) { word[i] = arg[i]; i++; }
    word[i] = '\0';

    if (cs_streq(word, "stim") || cs_streq(word, "stims")) {
        /* Using a stim in a fight spends your action, so it is gated like any
         * turn action. Out of combat it is a free bookkeeping move. */
        infight = (cs_geti(room, "cb_active", 0) == 1);
        if (infight && !turn_ok(room, self, &infight)) _exit(0);

        crashed = cs_stim_use(sh);
        if (crashed < 0) { puts("You have no stims left."); _exit(0); }

        cs_getstr(sh, "name", nm, sizeof(nm));
        if (infight) {
            cs_s(&o, nm);
            cs_s(&o, " jabs a stim -- Grit surges back.");
            if (crashed) cs_s(&o, " The overload bites: Crash worsens.");
            sys_broadcast(room, cs_cstr(&o));
            cs_seti(room, "cb_acted", 1);
        } else {
            cs_s(&o, "You jab a stim. Grit surges back (+2).");
            if (crashed)
                cs_s(&o, "\nThat was one too many -- Crash stacks (-1D on all "
                         "pools until you rest).");
            cs_s(&o, "\n");
            cs_flush(&o);
        }
        _exit(0);
    }

    if (cs_streq(word, "smartlink")) {
        /* Smartlink: 2 Grit + your action. Take aim so the next attack ignores
         * the target's cover and lowers its Passive Defense by 2 (applied and
         * cleared in verb_attack.c). A combat-only maneuver. */
        if (!cs_has_unlock(sh, "smartlink")) {
            puts("You have not learned that maneuver.");
            _exit(0);
        }
        if (!turn_ok(room, self, &infight)) {
            if (!infight) puts("You are not in a fight.");
            _exit(0);
        }
        if (cs_grit_spend(sh, 2) < 0) {
            puts("Not enough Grit to run the smartlink (2 Grit).");
            _exit(0);
        }
        cs_seti(sh, "aim", 1);
        cs_getstr(sh, "name", nm, sizeof(nm));
        cs_s(&o, nm);
        cs_s(&o, " paints a target through the smartlink and takes aim.");
        sys_broadcast(room, cs_cstr(&o));
        cs_seti(room, "cb_acted", 1);
        _exit(0);
    }

    puts("You can't use that.");
    _exit(0);
}
