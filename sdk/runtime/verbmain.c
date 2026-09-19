/* verbmain.c : the generic event-loop main() for an agent (OLC-5). A program
 * becomes a long-lived agent by linking this (the verbs.conf `agent_*` source
 * prefix, or `@program ... agent`) instead of supplying its own main(). The
 * program provides:
 *
 *     void on_event(const struct verb_event *m);  handle one event
 *     int  verb_dwell(void);   tick period in ms, or <= 0 to block with no ticks
 *
 * The loop registers the agent as the handler for its own object, then blocks
 * for events. A real event (EV_ENTER, EV_USER, ...) is delivered from the
 * mailbox; when the dwell elapses with an empty mailbox, an EV_TIMER is
 * synthesized, giving an agent its periodic tick (a wandering NPC, an advancing
 * train). Delivery is cooperative: events are only observed here, at the block
 * point, never as an interrupt. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

/* The agent's own object supplies memset/memcpy/memmove/memcmp; suppress this
 * object's copies so the two link without duplicate symbols. */
#define MULIBC_NO_IMPL
#include "mulibc.h"

extern void on_event(const struct verb_event *m);
extern int  verb_dwell(void);

int
main(void)
{
    struct verb_event m;
    int dwell = verb_dwell();

    /* A dwell of 0 would busy-spin (sys_suspend(0) yields but stays runnable),
       so treat "no positive period" as block-until-event, the same as a
       negative return. */
    if (dwell == 0)
        dwell = -1;
    sys_listen(vm_args->this_obj);      /* receive this object's events */
    for (;;) {
        if (sys_getmsg(&m, sizeof(m)) >= 0) {
            on_event(&m);
            continue;
        }
        /* mailbox empty: block until an event arrives or the dwell elapses,
           then drain one event or fire a tick */
        sys_suspend(dwell);
        if (sys_getmsg(&m, sizeof(m)) >= 0) {
            on_event(&m);
        } else {
            m.type = EV_TIMER;
            m.tag = 0;
            on_event(&m);
        }
    }
    return 0;
}
