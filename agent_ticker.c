/* agent_ticker.c : OLC-5 demo agent. Proves the event loop runs with no player
 * present: on each dwell it increments a `ticks` counter on its own object, and
 * it announces a newcomer on EV_ENTER. Launched with `@wake #N`, where #N.brain
 * names this verb; it runs under the system session, so the counter climbs even
 * in an empty room. Setuid, so grant_accept lets it write its object. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

int
verb_dwell(void)
{
    return 200;                         /* tick five times a second */
}

void
on_event(const struct verb_event *m)
{
    int self = vm_args->this_obj;

    if (m->type == EV_TIMER) {
        grant_accept();
        cs_seti(self, "ticks", cs_geti(self, "ticks", 0) + 1);
    } else if (m->type == EV_ENTER) {
        sys_broadcast(m->room, "The beacon registers a visitor.");
    }
}
