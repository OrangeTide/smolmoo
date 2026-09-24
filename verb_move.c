/* verb_move.c : ChromeSix combat movement across range bands. Each foe
 * has its own band to the player.
 *   close        move one band toward the nearest foe (toward Engaged)
 *   engage <foe> close straight into melee with one foe
 *   retreat      fall back one band from every foe (draws free Strikes)
 *   disengage    leave melee with all Engaged foes, untouched
 * Each is your action for the turn and grants +2 ranged Passive Defense
 * until your next turn. */

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
    int foes[8], k, i, enc;

    grant_accept();   /* writes room combat state and foe sheets */

    o.len = 0;
    if (cs_wrong_mode(room, 0)) _exit(0);
    if (cs_geti(room, "cb_active", 0) != 1) { puts("You are not in a fight."); _exit(0); }
    if (cs_geti(room, "cb_turn", -1) != self) { puts("It is not your turn."); _exit(0); }
    if (cs_geti(room, "cb_acted", 0) == 1) {
        puts("You have already acted this turn.");
        _exit(0);
    }
    /* M45c: a Held combatant cannot change bands until it breaks free. */
    if (cs_geti(sh, "held", 0)) {
        puts("You are held fast; break free first.");
        _exit(0);
    }

    k = cs_roster_foes(room, self, foes, 8);
    cs_getstr(sh, "name", nm, sizeof(nm));

    /* Weight (rules Section 6): overloaded past twice the carry rating locks
     * movement; encumbered over it is Slowed, so a band costs two Moves. One
     * Move arrives per turn, so a Slowed band takes two turns: the first spends
     * the turn on a half step, the second completes it. */
    enc = cs_encumbrance(self);
    if (enc >= 2) {
        puts("You are too loaded down to move.");
        _exit(0);
    }
    if (enc == 1) {
        int bank = cs_geti(sh, "move_bank", 0) + 1;

        if (bank < 2) {
            cs_seti(sh, "move_bank", bank);
            cs_seti(sh, "moved", 1);
            cs_s(&o, nm);
            cs_s(&o, " strains under the load, half a step.");
            sys_broadcast(room, cs_cstr(&o));
            cs_end_turn(room);
            _exit(0);
        }
        cs_seti(sh, "move_bank", 0);    /* the second Move completes the band */
    }

    if (cs_streq(verb, "retreat")) {
        int span = cs_room_span(room);     /* how far back the room allows */

        if (span < 1) {
            puts("There is no room to fall back here.");
            _exit(0);
        }
        for (i = 0; i < k; i++) {
            int fsh = cs_sheet(foes[i]), b;

            if (cs_geti(fsh, "downed", 0)) continue;
            b = cs_geti(fsh, "band", 0);
            cs_seti(fsh, "band", b < span ? b + 1 : span);
            if (b == 0) cs_free_strike(foes[i], sh, room);
        }
        cs_seti(sh, "moved", 1);
        cs_s(&o, nm);
        cs_s(&o, " falls back a step.");
    } else if (cs_streq(verb, "disengage")) {
        int any = 0;

        for (i = 0; i < k; i++) {
            int fsh = cs_sheet(foes[i]);

            if (!cs_geti(fsh, "downed", 0) && cs_geti(fsh, "band", 0) == 0) {
                cs_seti(fsh, "band", 1);
                any = 1;
            }
        }
        if (!any) { puts("You are not in melee."); _exit(0); }
        cs_seti(sh, "moved", 1);
        cs_s(&o, nm);
        cs_s(&o, " disengages to short range.");
    } else {
        /* close or engage: pick the named foe, else the nearest live one */
        int foe = 0, nb;

        while (arg && *arg == ' ') arg++;
        if (arg && *arg) foe = sys_objfind(arg);
        if (foe <= 0)
            for (i = 0; i < k; i++)
                if (!cs_geti(cs_sheet(foes[i]), "downed", 0)) { foe = foes[i]; break; }
        if (foe <= 0) { puts("There is no one to close on."); _exit(0); }

        {
            int fsh = cs_sheet(foe), b = cs_geti(fsh, "band", 0);
            char fn[32];

            nb = cs_streq(verb, "engage") ? 0 : (b > 0 ? b - 1 : 0);
            cs_seti(fsh, "band", nb);
            cs_seti(sh, "moved", 1);
            cs_getstr(fsh, "name", fn, sizeof(fn));
            cs_s(&o, nm);
            cs_s(&o, nb == 0 ? " closes into melee with " : " advances on ");
            cs_s(&o, fn);
            cs_s(&o, ".");
        }
    }

    sys_broadcast(room, cs_cstr(&o));
    cs_end_turn(room);
    _exit(0);
}
