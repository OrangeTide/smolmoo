/* verb_learn.c : spend Character Points to acquire an unlock (M35c). Every
 * maneuver, cyberware mod, and spell is one id in the sheet's `maneuvers` list.
 * `learn` adds one from the catalog (chromesix_verb.h) after checking its CP
 * cost, the hook it requires, and, for cyberware, the graft-slot cap. The effect
 * of each unlock lives with its use, not here. The player owns the sheet, so
 * this runs at caller authority like chargen, not setuid. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int ch = cs_geti(self, "charid", -1);
    char *arg = (char *)vm_args->dobjstr;
    char id[32], have[160], hook[16];
    int i = 0, u, cp, n;

    o.len = 0;
    if (ch < 0) { puts("You have no character sheet."); _exit(0); }
    if (!arg || !*arg) { puts("Usage: learn <unlock>"); _exit(0); }

    while (*arg == ' ') arg++;
    while (arg[i] && arg[i] != ' ' && i < 31) { id[i] = arg[i]; i++; }
    id[i] = '\0';

    u = cs_unlock_find(id);
    if (u < 0) { puts("No such unlock."); _exit(0); }

    if (cs_has_unlock(ch, id)) {
        cs_s(&o, "You already know "); cs_s(&o, id); cs_s(&o, ".\n");
        cs_flush(&o); _exit(0);
    }

    /* a cyberware or spell id needs the matching hook */
    if (cs_unlocks[u].hook[0]) {
        n = sys_getprop(ch, "hook", hook, sizeof(hook) - 1);
        if (n < 0) n = 0;
        hook[n] = '\0';
        if (!cs_streq(hook, cs_unlocks[u].hook)) {
            cs_s(&o, id); cs_s(&o, " needs the ");
            cs_s(&o, cs_unlocks[u].hook); cs_s(&o, " hook.\n");
            cs_flush(&o); _exit(0);
        }
    }

    /* cyberware occupies a graft slot; refuse when they are full */
    if (cs_unlocks[u].graft && cs_graft_count(ch) >= CS_GRAFT_CAP) {
        puts("Your graft slots are full.");
        _exit(0);
    }

    cp = cs_geti(ch, "cp", 0);
    if (cp < cs_unlocks[u].cp) {
        cs_s(&o, "That costs "); cs_i(&o, cs_unlocks[u].cp);
        cs_s(&o, " CP; you have "); cs_i(&o, cp); cs_s(&o, ".\n");
        cs_flush(&o); _exit(0);
    }

    /* append the id to the maneuvers list, then charge the CP */
    n = sys_getprop(ch, "maneuvers", have, sizeof(have) - 1);
    if (n < 0) n = 0;
    have[n] = '\0';
    o.len = 0;
    if (have[0]) { cs_s(&o, have); cs_s(&o, ","); }
    cs_s(&o, id);
    sys_setprop(ch, "maneuvers", cs_cstr(&o));
    cs_seti(ch, "cp", cp - cs_unlocks[u].cp);

    o.len = 0;
    cs_s(&o, "You learn "); cs_s(&o, id);
    cs_s(&o, " for "); cs_i(&o, cs_unlocks[u].cp);
    cs_s(&o, " CP ("); cs_i(&o, cp - cs_unlocks[u].cp); cs_s(&o, " left).\n");
    cs_flush(&o);
    _exit(0);
}
