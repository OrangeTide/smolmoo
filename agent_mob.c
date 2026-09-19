/* agent_mob.c : proactive NPC behavior (OLC-6). A mob becomes proactive by
 * being woken as an agent (@wake #N with #N.brain naming this verb). It runs
 * under the system session, so it acts with no player present:
 *   wander  -- on each tick, step through a random exit of its current room
 *   patrol  -- on each tick, advance along a fixed route (the `route` prop, a
 *              comma-separated list of room ids)
 * Both are driven by the same comma-separated `behavior` prop as the reactive
 * verbs; patrol takes precedence over wander when both are set. On EV_ENTER the
 * agent still greets/aggros through mob_react, so waking a mob does not cost it
 * the OLC-4 reactivity that the host would otherwise route to on_enter.
 * The tick period is the `dwell` prop in ms (default 5000). Setuid: moving the
 * mob and opening a fight both need the owner's authority. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mob_behavior.h"

/* the tick period, read once at wake time from the mob's `dwell` prop */
int
verb_dwell(void)
{
    return cs_geti(vm_args->this_obj, "dwell", 5000);
}

/* relocate the mob from `from` to `to`, announcing the departure and arrival so
 * a proactive move is visible to players in either room. */
static void
mob_move(int self, int from, int to)
{
    struct cs_out o;
    char nm[32];

    if (to <= 0 || to == from)
        return;
    cs_getstr(self, "name", nm, sizeof(nm));

    grant_accept();
    o.len = 0;
    cs_s(&o, nm);
    cs_s(&o, " leaves.");
    sys_broadcast(from, cs_cstr(&o));

    sys_move(self, to);

    o.len = 0;
    cs_s(&o, nm);
    cs_s(&o, " arrives.");
    sys_broadcast(to, cs_cstr(&o));
}

/* step through a random exit of `room` (an exit is any object located here that
 * carries a `dest` property; see the go verb). */
static void
do_wander(int self, int room)
{
    int exits[16], n = 0, e = 0;
    char d[16];

    while ((e = sys_next(room, e)) != 0 && n < 16) {
        cs_getstr(e, "dest", d, sizeof(d));
        if (d[0])
            exits[n++] = cs_atoi(d);
    }
    if (n > 0)
        mob_move(self, room, exits[sys_random(n)]);
}

/* advance along the `route` prop, a comma-separated list of room ids; the
 * `patrol_idx` prop tracks the current stop and wraps at the end. */
static void
do_patrol(int self, int room)
{
    char route[128];
    int stops[16], n = 0, i = 0, v = 0, seen = 0, idx;

    cs_getstr(self, "route", route, sizeof(route));
    for (i = 0; n < 16; i++) {
        char c = route[i];

        if (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
            seen = 1;
        } else {
            if (seen)
                stops[n++] = v;
            v = 0;
            seen = 0;
            if (c == '\0')
                break;
        }
    }
    if (n == 0)
        return;

    idx = cs_geti(self, "patrol_idx", 0) + 1;
    if (idx >= n)
        idx = 0;
    cs_seti(self, "patrol_idx", idx);
    mob_move(self, room, stops[idx]);
}

void
on_event(const struct verb_event *m)
{
    int self = vm_args->this_obj;

    if (m->type == EV_ENTER) {
        mob_react(self, m->player, m->room);
        return;
    }
    if (m->type == EV_TIMER) {
        int mobsh = cs_sheet(self);
        int room;
        char beh[48];

        if (cs_geti(mobsh, "downed", 0) || cs_geti(mobsh, "dead", 0))
            return;
        room = sys_getobj(self, "location");
        if (room <= 0)
            return;
        if (cs_geti(room, "cb_active", 0) == 1)     /* hold position in a fight */
            return;
        cs_getstr(self, "behavior", beh, sizeof(beh));
        if (has_token(beh, "patrol"))
            do_patrol(self, room);
        else if (has_token(beh, "wander"))
            do_wander(self, room);
    }
}
