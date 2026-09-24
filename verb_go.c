/* verb_go.c : move through an exit to an adjacent room. Out of a fight it is
 * plain travel. In a fight it is the Move-to-adjacent-zone that breaks you off
 * from the fight: each Engaged foe still holding a reaction gets a parting
 * free Strike, and leaving the room ends your part in the combat. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* private look: show the destination room's name and description to the mover */
static void
show_room(int room)
{
    struct cs_out o;
    char buf[200];

    o.len = 0;
    cs_getstr(room, "name", buf, sizeof(buf));
    cs_s(&o, "=== ");
    cs_s(&o, buf);
    cs_s(&o, " ===\n");
    cs_getstr(room, "description", buf, sizeof(buf));
    cs_s(&o, buf);
    cs_flush(&o);
}

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int sh = cs_sheet(self);
    int exit = vm_args->dobj;
    int dest;
    char nm[32], en[24], dbuf[16];

    grant_accept();   /* clears combat state on the room when fleeing */

    cs_rest(sh, room);   /* bank any short-rest BP before leaving the room (M39a) */

    if (exit <= 0) { puts("Go where?"); _exit(0); }
    cs_getstr(exit, "dest", dbuf, sizeof(dbuf));
    dest = cs_atoi(dbuf);
    if (dest <= 0) { puts("That way is blocked."); _exit(0); }
    cs_getstr(sh, "name", nm, sizeof(nm));
    cs_getstr(exit, "name", en, sizeof(en));

    /* Overloaded past twice the carry rating, you cannot leave the room until
     * you shed weight (rules Section 6), in a fight or out of it. */
    if (cs_encumbrance(self) >= 2) {
        puts("You are too loaded down to move.");
        _exit(0);
    }

    if (cs_geti(room, "cb_active", 0) == 1) {
        int foes[8], k, i;

        if (cs_geti(room, "cb_turn", -1) != self) {
            puts("It is not your turn.");
            _exit(0);
        }
        if (cs_geti(room, "cb_acted", 0) == 1) {
            puts("You have already acted this turn.");
            _exit(0);
        }
        /* M45c: a Held combatant cannot flee the room until it breaks free. */
        if (cs_geti(sh, "held", 0)) {
            puts("You are held fast; break free first.");
            _exit(0);
        }

        o.len = 0;
        cs_s(&o, nm);
        cs_s(&o, " breaks for the ");
        cs_s(&o, en);
        cs_s(&o, "!");
        sys_broadcast(room, cs_cstr(&o));

        /* every Engaged foe that still holds a reaction gets one parting hit;
         * a social scene has no blows to trade, so walking out is unopposed */
        if (!cs_scene_social(room)) {
            k = cs_roster_foes(room, self, foes, 8);
            for (i = 0; i < k; i++) {
                int fsh = cs_sheet(foes[i]);

                if (!cs_geti(fsh, "downed", 0) && cs_geti(fsh, "band", 0) == 0)
                    cs_free_strike(foes[i], sh, room);
            }
        }
        cs_end_turn(room);

        /* cut down while breaking off: stay put, the task runs the dying track */
        if (cs_geti(sh, "downed", 0)) _exit(0);

        sys_move(self, dest);
        cs_seti(room, "cb_fled", 1);
        puts("You slip away from the fight.");
    } else {
        o.len = 0;
        cs_s(&o, nm);
        cs_s(&o, " heads out through the ");
        cs_s(&o, en);
        cs_s(&o, ".");
        sys_broadcast(room, cs_cstr(&o));
        sys_move(self, dest);
    }

    /* Drag along a body we are carrying (M25i ally slice): it travels with us
     * room to room until we release or revive it. */
    {
        int body = cs_geti(self, "carrying", 0);

        if (body > 0) {
            sys_move(body, dest);
            o.len = 0;
            cs_s(&o, nm);
            cs_s(&o, " hauls a body along.");
            sys_broadcast(dest, cs_cstr(&o));
        }
    }

    /* M37c: arriving at a courier job's destination completes it. The giver
     * names the destination room in `job_dest`; grant_accept above lets us set
     * the flag on the mover's own sheet. */
    {
        int giver = cs_geti(sh, "job_giver", 0);
        char db[16];

        if (giver > 0 && !cs_geti(sh, "job_done", 0)) {
            cs_getstr(giver, "job_dest", db, sizeof(db));
            if (db[0] && cs_atoi(db) == dest)
                cs_seti(sh, "job_done", 1);
        }
    }

    show_room(dest);
    _exit(0);
}
