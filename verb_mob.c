/* verb_mob.c : reactive NPC behavior, run as the `on_enter` verb when a player
 * walks into the NPC's room (OLC-4). The host wakes every resident object that
 * carries a `behavior` property; this verb reads that comma-separated list and
 * acts on it:
 *   greet  -- say a line to the room (the NPC's `greeting` prop, or a default)
 *   aggro  -- open a fight on the newcomer, or join one already running here
 * Bindings: this = the NPC, player and dobj = the entering player. aggro reuses
 * the combat engine: it seeds the cb_* state the way verb_attack does and spawns
 * the __combat turn task, which then drives the NPC's turns. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

#define COMBAT_VERB 420

/* is token `t` a whole entry in the comma-separated list `s`? */
static int
has_token(const char *s, const char *t)
{
    int i;

    while (*s) {
        while (*s == ',' || *s == ' ') s++;
        for (i = 0; t[i] && s[i] && s[i] != ',' && t[i] == s[i]; i++)
            ;
        if (t[i] == '\0' && (s[i] == '\0' || s[i] == ','))
            return 1;
        while (*s && *s != ',') s++;
    }
    return 0;
}

/* append `id` to the room's cb_roster if it is not already a combatant */
static void
roster_add(int room, int id)
{
    char buf[64], num[12];
    int ids[8], c = cs_roster_foes(room, 0, ids, 8);
    int n, j, k;

    for (j = 0; j < c; j++)
        if (ids[j] == id) return;
    n = sys_getprop(room, "cb_roster", buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    cs_itoa(num, id);
    if (n > 0 && n < (int)sizeof(buf) - 14) {
        buf[n++] = ',';
        for (k = 0; num[k] && n < (int)sizeof(buf) - 1; k++)
            buf[n++] = num[k];
        buf[n] = '\0';
        sys_setprop(room, "cb_roster", buf);
    }
}

int
main(void)
{
    struct cs_out o;
    int mob = vm_args->this_obj;
    int foe = vm_args->player;                 /* the entering player */
    int room = cs_geti(mob, "location", vm_args->room);
    int mobsh = cs_sheet(mob);
    char beh[48];

    o.len = 0;
    cs_getstr(mob, "behavior", beh, sizeof(beh));

    /* a downed or dead NPC does not react */
    if (cs_geti(mobsh, "downed", 0) || cs_geti(mobsh, "dead", 0))
        _exit(0);

    if (has_token(beh, "greet")) {
        char g[160];
        int gn = sys_getprop(mob, "greeting", g, sizeof(g) - 1);

        if (gn > 0) {
            g[gn] = '\0';
            sys_broadcast(room, g);
        } else {
            char nm[32], fn[32];

            cs_getstr(mob, "name", nm, sizeof(nm));
            cs_getstr(cs_sheet(foe), "name", fn, sizeof(fn));
            o.len = 0;
            cs_s(&o, nm);
            cs_s(&o, " nods to ");
            cs_s(&o, fn);
            cs_s(&o, ".");
            sys_broadcast(room, cs_cstr(&o));
        }
    }

    if (has_token(beh, "aggro")) {
        int fsh = cs_sheet(foe);
        char nm[32], fn[32], roster[24], num[12];
        int i = 0, j;

        if (cs_geti(fsh, "downed", 0) || cs_geti(fsh, "dead", 0))
            _exit(0);
        grant_accept();     /* writes room combat state and both sheets */

        /* a fight is already running here: fall in rather than reopen it */
        if (cs_geti(room, "cb_active", 0) == 1) {
            if (!cs_scene_social(room))
                roster_add(room, mob);
            _exit(0);
        }

        /* open a fresh fight, this NPC vs the newcomer (roster "mob,foe") */
        cs_itoa(num, mob);
        for (j = 0; num[j]; j++) roster[i++] = num[j];
        roster[i++] = ',';
        cs_itoa(num, foe);
        for (j = 0; num[j]; j++) roster[i++] = num[j];
        roster[i] = '\0';

        sys_setprop(mobsh, "cover", "0");
        sys_setprop(mobsh, "cover_bonus", "0");
        sys_setprop(mobsh, "moved", "0");
        sys_setprop(mobsh, "move_bank", "0");
        sys_setprop(mobsh, "stance", "");
        sys_setprop(mobsh, "band", "0");
        sys_setprop(fsh, "band", "0");
        sys_setprop(room, "cb_mode", "fight");
        sys_setprop(room, "cb_roster", roster);
        cs_seti(room, "cb_round", 0);
        cs_seti(room, "cb_turn", 0);
        cs_seti(room, "cb_acted", 0);
        cs_seti(room, "cb_active", 1);

        cs_getstr(mobsh, "name", nm, sizeof(nm));
        cs_getstr(fsh, "name", fn, sizeof(fn));
        o.len = 0;
        cs_s(&o, nm);
        cs_s(&o, " turns on ");
        cs_s(&o, fn);
        cs_s(&o, " -- combat begins!");
        sys_broadcast(room, cs_cstr(&o));

        {
            char h[72];

            if (cs_elf_hash(COMBAT_VERB, h, sizeof(h)) == 0)
                sys_spawn(h, 0, "");
        }
    }
    _exit(0);
}
