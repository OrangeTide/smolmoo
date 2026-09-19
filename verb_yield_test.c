/* verb_yield_test.c : test fixture for the M25e on_yield hook. An NPC's
 * `on_yield` prop names this verb; verb_combat.c spawns it by ELF hash when
 * the NPC concedes, passing the NPC's object id as the argument string. It
 * reads that id back, looks up the NPC name, and broadcasts a concrete
 * concession, proving both that the hook fires and that the id is delivered. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

int
main(void)
{
    struct cs_out o;
    int room = vm_args->room;
    int npc = cs_atoi((char *)vm_args->argstr);
    char nm[32];

    cs_getstr(cs_sheet(npc), "name", nm, sizeof(nm));
    o.len = 0;
    cs_s(&o, "The ");
    cs_s(&o, nm[0] ? nm : "figure");
    cs_s(&o, " makes good and slides the datachip across.");
    sys_broadcast(room, cs_cstr(&o));
    _exit(0);
}
