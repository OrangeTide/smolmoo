/* verb_gear.c : wield a weapon, wear armor, or remove either (ChromeSix) */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

void
_start(void)
{
    struct cs_out o;
    int me = vm_args->player;
    int sh = cs_sheet(me);
    int item = vm_args->dobj;
    char *verb = (char *)vm_args->verb;
    char nm[32], tag[16], pref[12];

    o.len = 0;
    if (item <= 0) { puts("You do not have that."); _exit(0); }
    cs_getstr(item, "name", nm, sizeof(nm));

    if (cs_streq(verb, "remove")) {
        char slot[16];

        cs_getstr(item, "slot", slot, sizeof(slot));
        if (slot[0] == '\0') { puts("You are not using that."); _exit(0); }
        sys_setprop(item, "slot", "");
        if (item == cs_geti(sh, "worn", 0)) {
            sys_setprop(sh, "worn", "0");
            sys_setprop(sh, "armor", "0");
            cs_s(&o, "You take off the ");
        } else if (item == cs_geti(sh, "wielded", 0)) {
            cs_seti(sh, "wielded", 305);        /* back to bare hands */
            cs_s(&o, "You put away the ");
        } else {
            cs_s(&o, "You stow the ");
        }
    } else if (cs_streq(verb, "wear")) {
        if (cs_item_role(item) != 'w') {
            puts("You cannot wear that.");
            _exit(0);
        }
        cs_getstr(item, "part", pref, sizeof(pref));
        if (!cs_find_free_slot(me, 'w', pref, tag, sizeof(tag))) {
            puts("You have nowhere to wear that.");
            _exit(0);
        }
        sys_move(item, me);
        sys_setprop(item, "slot", tag);
        cs_seti(sh, "worn", item);
        cs_seti(sh, "armor", cs_geti(item, "soak", 0));
        cs_s(&o, "You put on the ");
    } else {
        if (cs_item_role(item) != 'h') {
            puts("You cannot wield that.");
            _exit(0);
        }
        cs_getstr(item, "part", pref, sizeof(pref));
        if (!cs_find_free_slot(me, 'h', pref, tag, sizeof(tag))) {
            puts("You have no free hand.");
            _exit(0);
        }
        sys_move(item, me);
        sys_setprop(item, "slot", tag);
        cs_seti(sh, "wielded", item);
        cs_s(&o, "You ready the ");
    }
    cs_s(&o, nm);
    cs_s(&o, ".\n");
    cs_flush(&o);
    _exit(0);
}
