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
 * A train may also carry a `path` (M34 slice 2): a comma-separated list of every
 * room it traverses in order, the stops among them plus the pass-through rooms
 * between (tunnels, track). With a `path` the train walks one room per tick and
 * really occupies each, so its riders and anyone standing in a pass-through room
 * see one another go by; `path_idx` tracks its place along it. A pass-through
 * room may also carry an `observe` list of rooms visible from it (M34 slice 2b),
 * so a rider glimpses a platform the train skips and its people see it pass in
 * the distance.
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

/* The tick period. While `moving` (between stops, M34) it is `transit`, the
   travel time, so the loop wakes on arrival. At a stop it is `dwell`: > 0 for a
   self-paced train, <= 0 (default) for an on-command elevator that only moves on
   an EV_USER request. verbmain re-reads this each cycle, so it may change with
   the vehicle's state. */
int
verb_dwell(void)
{
    int self = vm_args->this_obj;

    if (cs_geti(self, "moving", 0))
        return cs_geti(self, "transit", 0);
    return cs_geti(self, "dwell", -1);
}

/* announce departure to the riders and the origin platform */
static void
announce_depart(int self, int from)
{
    struct cs_out o;
    char vn[32];

    cs_getstr(self, "name", vn, sizeof(vn));
    o.len = 0;                                  /* riders */
    cs_s(&o, "The ");
    cs_s(&o, vn);
    cs_s(&o, " lurches into motion.");
    sys_broadcast(self, cs_cstr(&o));
    o.len = 0;                                  /* origin platform */
    cs_s(&o, "The ");
    cs_s(&o, vn);
    cs_s(&o, " departs.");
    sys_broadcast(from, cs_cstr(&o));
}

/* announce arrival to the destination platform and the riders */
static void
announce_arrive(int self, int stop)
{
    struct cs_out o;
    char vn[32], sn[32];

    cs_getstr(self, "name", vn, sizeof(vn));
    cs_getstr(stop, "name", sn, sizeof(sn));
    o.len = 0;                                  /* destination platform */
    cs_s(&o, "The ");
    cs_s(&o, vn);
    cs_s(&o, " pulls in.");
    sys_broadcast(stop, cs_cstr(&o));
    o.len = 0;                                  /* riders */
    cs_s(&o, "The ");
    cs_s(&o, vn);
    cs_s(&o, " arrives at ");
    cs_s(&o, sn);
    cs_s(&o, ".");
    sys_broadcast(self, cs_cstr(&o));
}

/* begin a move to `stop`. With a positive `transit` the vehicle departs, rides
   to the stop with its doors shut (`moving` set), and opens on the next tick;
   otherwise it is the old instant hop. */
static void
go_to(int self, int stop)
{
    int from = sys_getobj(self, "location");

    if (stop <= 0 || stop == from)
        return;
    grant_accept();
    announce_depart(self, from);
    sys_move(self, stop);
    if (cs_geti(self, "transit", 0) > 0)
        cs_seti(self, "moving", 1);             /* doors shut; open next tick */
    else
        announce_arrive(self, stop);            /* instant: open now */
}

/* the arrival tick: open the doors at the current stop and leave transit */
static void
open_doors(int self)
{
    grant_accept();
    cs_seti(self, "moving", 0);
    announce_arrive(self, sys_getobj(self, "location"));
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

/* the window (M34 slice 2): show a room the vehicle can see, both ways. `room` is
   the vehicle's own position when `distant` is 0, or a room it only sees into
   (M34 slice 2b, named in the position's `observe` list) when `distant` is 1.
   The room sees the vehicle go by; the riders see the room and glimpse anyone in
   it. A player avatar carries a `charid`, which tells it apart from the vehicle
   itself and from loose items. */
static void
glimpse(int self, int room, int distant)
{
    struct cs_out o;
    char vn[32], rn[32], nm[32];
    int e = 0;

    cs_getstr(self, "name", vn, sizeof(vn));
    cs_getstr(room, "name", rn, sizeof(rn));

    o.len = 0;                                  /* the room sees us pass */
    cs_s(&o, "The ");
    cs_s(&o, vn);
    cs_s(&o, distant ? " passes in the distance." : " rushes past.");
    sys_broadcast(room, cs_cstr(&o));

    o.len = 0;                                  /* riders see the room */
    cs_s(&o, distant ? "In the distance: " : "Through the window: ");
    cs_s(&o, rn);
    cs_s(&o, ".");
    sys_broadcast(self, cs_cstr(&o));

    while ((e = sys_next(room, e)) != 0) {      /* and glimpse who is in it */
        if (cs_geti(e, "charid", -1) < 0)
            continue;
        cs_getstr(cs_sheet(e), "name", nm, sizeof(nm));
        o.len = 0;
        cs_s(&o, "You glimpse ");
        cs_s(&o, nm);
        cs_s(&o, distant ? " in the distance." : " outside.");
        sys_broadcast(self, cs_cstr(&o));
    }
}

/* the window on a pass-through room: the room the vehicle occupies, plus any
   rooms that position can see into. A pass-through room names those in an
   `observe` list (M34 slice 2b), so a rider glimpses a platform the train skips
   and the people on it, and they see it pass in the distance. */
static void
window(int self, int room)
{
    int obs[8], nobs, i;
    char obuf[128];

    glimpse(self, room, 0);
    cs_getstr(room, "observe", obuf, sizeof(obuf));
    nobs = parse_ids(obuf, obs, 8);
    for (i = 0; i < nobs; i++)
        glimpse(self, obs[i], 1);
}

/* advance one step along `path` (M34 slice 2): the vehicle walks each room in
   turn, not just stop to stop, so it has a real position. A room that is also a
   stop opens the doors; any other room is a pass-through window. The current
   position is taken from the vehicle's actual location so it stays in step even
   if it was placed by hand. */
static void
advance_path(int self, int *path, int plen, int *stops, int nstops)
{
    int here = sys_getobj(self, "location");
    int cur = cs_geti(self, "path_idx", 0);
    int next, i, at_stop;

    for (i = 0; i < plen; i++)
        if (path[i] == here) { cur = i; break; }
    next = cur + 1 >= plen ? 0 : cur + 1;

    grant_accept();
    for (i = 0; i < nstops; i++)                /* announce leaving a station */
        if (path[cur] == stops[i]) { announce_depart(self, path[cur]); break; }

    sys_move(self, path[next]);
    cs_seti(self, "path_idx", next);

    at_stop = -1;
    for (i = 0; i < nstops; i++)
        if (path[next] == stops[i]) { at_stop = i; break; }
    if (at_stop >= 0) {
        cs_seti(self, "stop_idx", at_stop);
        cs_seti(self, "moving", 0);             /* doors open at the stop */
        announce_arrive(self, path[next]);
    } else {
        cs_seti(self, "moving", 1);             /* still under way */
        window(self, path[next]);
    }
}

void
on_event(const struct verb_event *m)
{
    int self = vm_args->this_obj;
    int stops[16], n, idx;
    int path[32], plen;
    char route[128], pathbuf[256];

    cs_getstr(self, "route", route, sizeof(route));
    n = parse_ids(route, stops, 16);
    if (n == 0)
        return;

    /* A `path` (M34 slice 2) makes the vehicle walk every room in turn, so it
       has a real position and its riders and the rooms it passes see each other.
       Only a self-paced train follows one; an on-command elevator still hops. */
    plen = 0;
    if (cs_geti(self, "dwell", -1) > 0) {
        cs_getstr(self, "path", pathbuf, sizeof(pathbuf));
        plen = parse_ids(pathbuf, path, 32);
    }
    if (plen > 1) {
        if (m->type == EV_TIMER)
            advance_path(self, path, plen, stops, n);
        else if (m->type == EV_USER)
            say(self, " runs a fixed route.");
        return;
    }

    /* Under way: the only thing that acts is the arrival tick; a request waits
       for the doors. */
    if (cs_geti(self, "moving", 0)) {
        if (m->type == EV_TIMER)
            open_doors(self);
        else if (m->type == EV_USER)
            say(self, " is under way; the doors are closed.");
        return;
    }

    if (m->type == EV_TIMER) {                  /* train: next stop each tick */
        idx = cs_geti(self, "stop_idx", 0) + 1;
        if (idx >= n)
            idx = 0;
        cs_seti(self, "stop_idx", idx);
        go_to(self, stops[idx]);
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
            go_to(self, stops[f - 1]);
        } else {
            say(self, " has no such floor.");
        }
    }
}
