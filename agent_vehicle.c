/* agent_vehicle.c : a vehicle that advances along a route (OLC-7). A vehicle is
 * a room object (marked `vehicle`=1) whose own `location` is its current stop
 * and whose riders have `location` = the vehicle. Woken as an agent (@wake with
 * brain = this verb), it moves itself between stops, carrying its riders:
 *   train / carriage -- self-paced: `dwell` ms > 0 makes each EV_TIMER advance
 *                       one stop along `route`, wrapping at the end. It runs its
 *                       own route and refuses rider floor requests.
 *   elevator         -- on command: leave `dwell` unset (<= 0) so it never ticks
 *                       and only moves when the `floor` verb sends an EV_USER
 *                       carrying the requested stop (a 1-based index into
 *                       `route`) through sys_notify.
 * `route` is a comma-separated list of stop room ids; `stop_idx` tracks the
 * current stop. Each move announces to the riders and to both platforms.
 * Setuid: moving the vehicle needs the owner's authority. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* parse a comma-separated list of ids into out[], returning the count */
static int
parse_ids(const char *s, int *out, int max)
{
    int n = 0, v = 0, seen = 0, i;

    for (i = 0; n < max; i++) {
        char c = s[i];

        if (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
            seen = 1;
        } else {
            if (seen)
                out[n++] = v;
            v = 0;
            seen = 0;
            if (c == '\0')
                break;
        }
    }
    return n;
}

/* the tick period: > 0 for a self-paced train, <= 0 (default) for an
   on-command elevator that only moves on an EV_USER request */
int
verb_dwell(void)
{
    return cs_geti(vm_args->this_obj, "dwell", -1);
}

/* relocate the vehicle to `stop`, announcing to riders and both platforms */
static void
arrive(int self, int stop)
{
    struct cs_out o;
    char vn[32], sn[32];
    int from = sys_getobj(self, "location");

    if (stop <= 0 || stop == from)
        return;
    cs_getstr(self, "name", vn, sizeof(vn));
    cs_getstr(stop, "name", sn, sizeof(sn));

    grant_accept();
    o.len = 0;                                  /* riders: departure */
    cs_s(&o, "The ");
    cs_s(&o, vn);
    cs_s(&o, " lurches into motion.");
    sys_broadcast(self, cs_cstr(&o));

    o.len = 0;                                  /* old platform */
    cs_s(&o, "The ");
    cs_s(&o, vn);
    cs_s(&o, " departs.");
    sys_broadcast(from, cs_cstr(&o));

    sys_move(self, stop);

    o.len = 0;                                  /* new platform */
    cs_s(&o, "The ");
    cs_s(&o, vn);
    cs_s(&o, " pulls in.");
    sys_broadcast(stop, cs_cstr(&o));

    o.len = 0;                                  /* riders: arrival */
    cs_s(&o, "The ");
    cs_s(&o, vn);
    cs_s(&o, " arrives at ");
    cs_s(&o, sn);
    cs_s(&o, ".");
    sys_broadcast(self, cs_cstr(&o));
}

/* broadcast a short refusal to the riders */
static void
say(int self, const char *tail)
{
    struct cs_out o;
    char vn[32];

    cs_getstr(self, "name", vn, sizeof(vn));
    o.len = 0;
    cs_s(&o, "The ");
    cs_s(&o, vn);
    cs_s(&o, tail);
    sys_broadcast(self, cs_cstr(&o));
}

void
on_event(const struct verb_event *m)
{
    int self = vm_args->this_obj;
    int stops[16], n, idx;
    char route[128];

    cs_getstr(self, "route", route, sizeof(route));
    n = parse_ids(route, stops, 16);
    if (n == 0)
        return;

    if (m->type == EV_TIMER) {                  /* train: next stop each tick */
        idx = cs_geti(self, "stop_idx", 0) + 1;
        if (idx >= n)
            idx = 0;
        cs_seti(self, "stop_idx", idx);
        arrive(self, stops[idx]);
    } else if (m->type == EV_USER) {            /* a stop request (the `floor`
                                                   verb sends the stop number in
                                                   argstr) */
        int f = cs_atoi((const char *)m->argstr);

        /* A self-paced train runs its own route and takes no requests. */
        if (cs_geti(self, "dwell", -1) > 0) {
            say(self, " runs a fixed route.");
            return;
        }
        if (f >= 1 && f <= n) {
            cs_seti(self, "stop_idx", f - 1);
            arrive(self, stops[f - 1]);
        } else {
            say(self, " has no such floor.");
        }
    }
}
