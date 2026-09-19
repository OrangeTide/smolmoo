/* verb_mob.c : reactive NPC behavior, run as the `on_enter` verb when a player
 * walks into the NPC's room (OLC-4). The host wakes every resident object that
 * carries a `behavior` property; this one-shot verb hands off to mob_react,
 * which reads that comma-separated list and acts on it (greet, aggro). The same
 * logic drives a woken mob agent's EV_ENTER (agent_mob.c), so the two paths
 * stay identical. Bindings: this = the NPC, player = the entering player. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mob_behavior.h"

int
main(void)
{
    int mob = vm_args->this_obj;
    int foe = vm_args->player;                 /* the entering player */
    int room = cs_geti(mob, "location", vm_args->room);

    mob_react(mob, foe, room);
    _exit(0);
}
