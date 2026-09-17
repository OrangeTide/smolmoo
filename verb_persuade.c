/* verb_persuade.c : ChromeSix social conflict (M25e). One program behind the
 * four Charm approaches -- command, negotiate, con, perform -- dispatched on
 * the verb name. Opens a social scene, draws a new target into an active one,
 * or resolves a push against a target already in the scene. Runs on the same
 * turn loop as combat (verb_combat.c), distinguished by cb_mode = "social". */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* object id of the __combat task verb (verbs.conf) */
#define COMBAT_VERB 420

/* the Charm skill a verb name drives; "perform" is the skill "performance" */
static const char *
approach_skill(const char *verb)
{
    if (cs_streq(verb, "perform")) return "performance";
    return verb;                 /* command, negotiate, con match directly */
}

/* start a combatant's social track: full Resolve, not yet yielded */
static void
init_resolve(int sh)
{
    cs_seti(sh, "resolve", cs_max_resolve(sh));
    sys_setprop(sh, "yielded", "0");
}

/* Is `who` already a combatant on the room's roster? */
static int
on_roster(int room, int who)
{
    int ids[8], k = cs_roster_foes(room, 0, ids, 8), i;

    for (i = 0; i < k; i++)
        if (ids[i] == who) return 1;
    return 0;
}

void
_start(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int src = cs_sheet(self);
    char *tgtname = (char *)vm_args->dobjstr;
    const char *skill = approach_skill((char *)vm_args->verb);
    int tgt, tsh, yield;

    grant_accept();   /* writes social-conflict state on the room and target */

    o.len = 0;
    while (tgtname && *tgtname == ' ') tgtname++;
    if (!tgtname || !*tgtname) { puts("Lean on whom?"); _exit(0); }

    /* a fight in progress blocks talk; talk is the only thing this scene runs */
    if (cs_wrong_mode(room, 1)) _exit(0);

    tgt = sys_objfind(tgtname);
    if (tgt <= 0) { puts("You don't see that here."); _exit(0); }
    tsh = cs_sheet(tgt);
    if (tsh == src) { puts("You cannot argue yourself into anything."); _exit(0); }
    if (cs_geti(tsh, "yielded", 0)) { puts("They have already conceded."); _exit(0); }

    if (cs_geti(room, "cb_active", 0) != 1) {
        char roster[24], num[12];
        int i = 0, j;

        cs_itoa(num, self);
        for (j = 0; num[j]; j++) roster[i++] = num[j];
        roster[i++] = ',';
        cs_itoa(num, tgt);
        for (j = 0; num[j]; j++) roster[i++] = num[j];
        roster[i] = '\0';

        init_resolve(src);
        init_resolve(tsh);
        sys_setprop(room, "cb_mode", "social");
        sys_setprop(room, "cb_roster", roster);
        cs_seti(room, "cb_round", 0);
        cs_seti(room, "cb_turn", 0);
        cs_seti(room, "cb_acted", 0);
        cs_seti(room, "cb_active", 1);

        char h[72];
        if (cs_elf_hash(COMBAT_VERB, h, sizeof(h)) == 0)
            sys_spawn(h, 0, "");
        cs_s(&o, "You square off with the ");
        cs_s(&o, tgtname);
        cs_s(&o, " -- the argument is joined!\n");
        cs_flush(&o);
        _exit(0);
    }

    if (cs_geti(room, "cb_turn", -1) != self) {
        puts("It is not your turn.");
        _exit(0);
    }
    if (cs_geti(room, "cb_acted", 0) == 1) {
        puts("You have already acted this turn.");
        _exit(0);
    }

    /* draw a fresh target into the scene at full Resolve */
    if (!on_roster(room, tgt)) {
        char rbuf[64], num[12];
        int rn = sys_getprop(room, "cb_roster", rbuf, sizeof(rbuf) - 1), j;

        init_resolve(tsh);
        if (rn < 0) rn = 0;
        rbuf[rn] = '\0';
        cs_itoa(num, tgt);
        if (rn > 0 && rn < (int)sizeof(rbuf) - 14) {
            rbuf[rn++] = ',';
            for (j = 0; num[j] && rn < (int)sizeof(rbuf) - 1; j++)
                rbuf[rn++] = num[j];
            rbuf[rn] = '\0';
            sys_setprop(room, "cb_roster", rbuf);
        }
    }

    cs_push_resolve(src, tsh, skill, -cs_crash_penalty(src), &o, &yield);
    cs_s(&o, "\n");
    sys_broadcast(room, cs_cstr(&o));
    cs_seti(room, "cb_acted", 1);
    _exit(0);
}
