/* verb_vehicle.c : board and disembark a vehicle (OLC-7). A vehicle is a room
 * object marked with `vehicle`=1 whose own `location` is its current stop and
 * whose riders have `location` = the vehicle, so moving the vehicle carries
 * them. board puts the player aboard a vehicle that is present at their stop;
 * disembark steps them off to the vehicle's current stop. Setuid, like go: both
 * relocate the player. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* show the destination room's name and description to the mover */
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
    char *verb = (char *)vm_args->verb;
    char nm[32], vn[32];

    cs_getstr(cs_sheet(self), "name", nm, sizeof(nm));

    /* floor <n> : ask the vehicle you are aboard to go to a stop. The request
       routes to the vehicle's agent (sys_notify -> EV_USER), which owns the move
       and rejects an invalid floor. A plain verb: it steers nothing itself. */
    if (cs_streq(verb, "floor")) {
        int veh = sys_getobj(self, "location");
        char *arg = (char *)vm_args->argstr;

        if (veh <= 0 || !cs_geti(veh, "vehicle", 0)) {
            puts("You aren't aboard a vehicle.");
            _exit(0);
        }
        if (cs_atoi(arg) <= 0) {
            puts("Usage: floor <number>");
            _exit(0);
        }
        if (sys_notify(veh, arg) != 0)
            puts("Nothing responds.");
        _exit(0);
    }

    if (cs_streq(verb, "disembark")) {
        int veh = sys_getobj(self, "location");
        int stop;

        if (veh <= 0 || !cs_geti(veh, "vehicle", 0)) {
            puts("You aren't aboard anything.");
            _exit(0);
        }
        stop = sys_getobj(veh, "location");
        if (stop <= 0) {
            puts("There is nowhere to step off to.");
            _exit(0);
        }
        cs_getstr(veh, "name", vn, sizeof(vn));

        grant_accept();
        o.len = 0;
        cs_s(&o, nm);
        cs_s(&o, " steps off the ");
        cs_s(&o, vn);
        cs_s(&o, ".");
        sys_broadcast(veh, cs_cstr(&o));

        sys_move(self, stop);

        o.len = 0;
        cs_s(&o, nm);
        cs_s(&o, " arrives.");
        sys_broadcast(stop, cs_cstr(&o));
        show_room(stop);
        _exit(0);
    }

    /* board <vehicle> */
    {
        int veh = vm_args->dobj;
        int vloc;

        if (veh <= 0 || !cs_geti(veh, "vehicle", 0)) {
            puts("You can't board that.");
            _exit(0);
        }
        vloc = sys_getobj(veh, "location");
        if (vloc != room) {
            puts("It isn't here.");
            _exit(0);
        }
        cs_getstr(veh, "name", vn, sizeof(vn));

        grant_accept();
        o.len = 0;
        cs_s(&o, nm);
        cs_s(&o, " boards the ");
        cs_s(&o, vn);
        cs_s(&o, ".");
        sys_broadcast(room, cs_cstr(&o));

        sys_move(self, veh);

        o.len = 0;
        cs_s(&o, nm);
        cs_s(&o, " climbs aboard.");
        sys_broadcast(veh, cs_cstr(&o));
        show_room(veh);
        _exit(0);
    }
}
