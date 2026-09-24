/* verb_use.c : ChromeSix consumables and maneuvers. Covers stims (restore
 * Grit, stack Crash past the safe limit) and the CP-unlock maneuvers a
 * character has learned, each spending its Grit cost as the turn's action. An
 * active unlock dispatches through the cs_unlocks catalog (M36b): its Grit cost
 * and effect tag are table data, so a new maneuver is a row, not a new branch.
 * Timed self-buffs (M41a: shield, mesh), an offensive spell (M41b: shock), an
 * area debuff (M41b: suppress), and an aid action (M41c: inject) all dispatch
 * the same way. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

#define SHIELD_ROUNDS 2         /* mana shield duration; re-use refreshes it */
#define MESH_ROUNDS   3         /* dermal wire mesh duration (rules Section 11) */
#define SHOCK_FOCUS   314       /* the Static Shock spell's damage profile (world seed) */
#define INJECT_TN     10        /* biomedical injector: Moderate Wit+Medicine check */

/* is `foe` on the room's combat roster? */
static int
on_roster(int room, int foe)
{
    int ids[8], k = cs_roster_foes(room, 0, ids, 8), i;

    for (i = 0; i < k; i++)
        if (ids[i] == foe) return 1;
    return 0;
}

/* Gate a combat action to the actor's own turn. Returns 1 if allowed to act
 * (a fight is running and it is the actor's unspent turn), else prints why
 * and returns 0. Sets *infight to whether a fight is running at all. */
static int
turn_ok(int room, int self, int *infight)
{
    *infight = (cs_geti(room, "cb_active", 0) == 1);
    if (!*infight) return 0;
    if (cs_geti(room, "cb_turn", -1) != self) {
        puts("It is not your turn.");
        return 0;
    }
    if (cs_geti(room, "cb_acted", 0) == 1) {
        puts("You have already acted this turn.");
        return 0;
    }
    return 1;
}

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int sh = cs_sheet(self);
    char *arg = (char *)vm_args->dobjstr;
    char word[16], nm[32];
    int i = 0, infight, crashed, foe = 0;

    grant_accept();   /* writes combat state on the room */

    o.len = 0;
    while (arg && *arg == ' ') arg++;
    if (!arg || !*arg) { puts("Use what?"); _exit(0); }
    while (arg[i] && arg[i] != ' ' && i < 15) { word[i] = arg[i]; i++; }
    word[i] = '\0';

    if (cs_streq(word, "stim") || cs_streq(word, "stims")) {
        /* Using a stim in a fight spends your action, so it is gated like any
         * turn action. Out of combat it is a free bookkeeping move. */
        infight = (cs_geti(room, "cb_active", 0) == 1);
        if (infight && !turn_ok(room, self, &infight)) _exit(0);

        crashed = cs_stim_use(sh);
        if (crashed < 0) { puts("You have no stims left."); _exit(0); }

        cs_getstr(sh, "name", nm, sizeof(nm));
        if (infight) {
            cs_s(&o, nm);
            cs_s(&o, " jabs a stim -- Grit surges back.");
            if (crashed) cs_s(&o, " The overload bites: Crash worsens.");
            sys_broadcast(room, cs_cstr(&o));
            cs_end_turn(room);
        } else {
            cs_s(&o, "You jab a stim. Grit surges back (+2).");
            if (crashed)
                cs_s(&o, "\nThat was one too many -- Crash stacks (-1D on all "
                         "pools until you rest).");
            cs_s(&o, "\n");
            cs_flush(&o);
        }
        _exit(0);
    }

    /* Any other word is an active unlock, dispatched through the catalog (M36b):
     * spend its Grit and, once it is your turn, apply its effect by tag. A
     * passive unlock (its effect read elsewhere) is not something you activate. */
    {
        int idx = cs_unlock_find(word);
        const struct cs_unlock *u;

        if (idx < 0) { puts("You can't use that."); _exit(0); }
        u = &cs_unlocks[idx];
        if (u->effect == UEF_PASSIVE) {
            puts("That works on its own; there is nothing to activate.");
            _exit(0);
        }
        if (!cs_has_unlock(sh, u->id)) {
            puts("You have not learned that.");
            _exit(0);
        }
        if (!turn_ok(room, self, &infight)) {
            if (!infight) puts("You are not in a fight.");
            _exit(0);
        }
        /* an offensive unlock resolves its target before it spends anything */
        if (u->effect == UEF_SHOCK || u->effect == UEF_EXPOSE
                || u->effect == UEF_MARK) {
            char *tgt = arg;

            while (*tgt && *tgt != ' ') tgt++;   /* skip the unlock word */
            while (*tgt == ' ') tgt++;
            if (!*tgt) { puts("On whom?"); _exit(0); }
            foe = sys_objfind(tgt);
            if (foe <= 0) { puts("You don't see that here."); _exit(0); }
            if (!on_roster(room, foe)) { puts("They are not in this fight."); _exit(0); }
            if (cs_geti(cs_sheet(foe), "downed", 0)) { puts("It is already down."); _exit(0); }
        }
        /* an aid unlock heals a named ally in reach, or the caller if unnamed */
        if (u->effect == UEF_INJECT) {
            char *tgt = arg;

            while (*tgt && *tgt != ' ') tgt++;
            while (*tgt == ' ') tgt++;
            if (*tgt) {
                foe = sys_objfind(tgt);
                if (foe <= 0) { puts("You don't see that here."); _exit(0); }
                if (on_roster(room, foe)) { puts("You can't inject an enemy."); _exit(0); }
            }
        }
        if (cs_grit_spend(sh, u->grit) < 0) {
            cs_s(&o, "Not enough Grit (need "); cs_i(&o, u->grit);
            cs_s(&o, ").");
            puts(cs_cstr(&o));
            _exit(0);
        }
        cs_getstr(sh, "name", nm, sizeof(nm));
        switch (u->effect) {
        case UEF_AIM:
            /* take aim: the next attack ignores the target's cover and lowers
             * its Passive Defense by 2 (applied and cleared in verb_attack.c) */
            cs_seti(sh, "aim", 1);
            cs_s(&o, nm);
            cs_s(&o, " paints a target through the smartlink and takes aim.");
            break;
        case UEF_SHIELD:
            /* mana shield: +3 Passive Defense for a few rounds (read in
             * cs_recalc, counted down in cs_cond_tick). Re-use refreshes it. */
            cs_seti(sh, "shield", SHIELD_ROUNDS);
            cs_s(&o, nm);
            cs_s(&o, " weaves a mana shield; a shimmering ward flares up.");
            break;
        case UEF_MESH:
            /* dermal wire mesh: +Wit dice Soak for a few rounds, same timing */
            cs_seti(sh, "mesh", MESH_ROUNDS);
            cs_s(&o, nm);
            cs_s(&o, "'s dermal wire mesh hardens under the skin.");
            break;
        case UEF_SHOCK: {
            /* static shock: a Wit+Spellcasting attack for 4D (the focus object's
             * damage), a Tactical Edge on the hit also leaving the target Stunned. */
            int fsh = cs_sheet(foe), down, edge;

            cs_attack_resolve(sh, fsh, SHOCK_FOCUS, -cs_crash_penalty(sh), 0, 1,
                              &o, &down, &edge);
            if (edge && !down) {
                cs_seti(fsh, "stunned", 1);
                cs_s(&o, " The current locks it up -- stunned!");
            }
            break;
        }
        case UEF_EXPOSE: {
            /* tactical co-processor: leave the target Exposed, a tracked -2
             * Passive Defense read in cs_attack_resolve (M45b). */
            char tn[32];

            cs_seti(cs_sheet(foe), "exposed", 1);
            cs_getstr(cs_sheet(foe), "name", tn, sizeof(tn));
            cs_s(&o, nm);
            cs_s(&o, "'s co-processor reads ");
            cs_s(&o, tn);
            cs_s(&o, "'s stance -- it is exposed.");
            break;
        }
        case UEF_MARK: {
            /* threat-assessment optics: mark the target so this attacker's
             * shots gain +1D against it (stored on the target as our sheet). */
            char tn[32];

            cs_seti(cs_sheet(foe), "marked_by", sh);
            cs_getstr(cs_sheet(foe), "name", tn, sizeof(tn));
            cs_s(&o, nm);
            cs_s(&o, "'s optics lock onto ");
            cs_s(&o, tn);
            cs_s(&o, " -- marked.");
            break;
        }
        case UEF_SUPPRESS: {
            /* suppressive fire: pin the standing foes, -1D on their next attack */
            int ids[8], k = cs_roster_foes(room, 0, ids, 8), j;

            for (j = 0; j < k; j++)
                if (!cs_geti(cs_sheet(ids[j]), "downed", 0))
                    cs_seti(cs_sheet(ids[j]), "suppress", 1);
            cs_s(&o, nm);
            cs_s(&o, " opens up with suppressive fire; the enemy is pinned.");
            break;
        }
        case UEF_INJECT: {
            /* biomedical injector: Wit+Medicine vs Moderate, restore BP by the
             * margin capped at the caller's Wit dice, to the caller or an ally. */
            int tsh = foe ? cs_sheet(foe) : sh;
            struct cs_derived dd;
            char tn[32];
            int wd = cs_geti(sh, "wit", 0) / 3;
            int pool = cs_geti(sh, "wit", 0) + cs_geti(sh, "sk_medicine", 0)
                       - cs_crash_penalty(sh);
            int wild, roll, heal, bp;

            if (pool < 1) pool = 1;
            roll = cs_roll(pool, &wild);
            heal = roll - INJECT_TN;
            if (heal > wd) heal = wd;
            cs_getstr(tsh, "name", tn, sizeof(tn));
            cs_s(&o, nm);
            if (heal <= 0) {
                cs_s(&o, " triggers a biomedical injector, but the dose does nothing.");
                break;
            }
            cs_recalc(tsh, &dd);
            bp = cs_geti(tsh, "bp", dd.maxbp) + heal;
            if (bp > dd.maxbp) bp = dd.maxbp;
            cs_seti(tsh, "bp", bp);
            cs_s(&o, " jacks a biomedical injector into ");
            cs_s(&o, foe ? tn : "themselves");
            cs_s(&o, " (+");
            cs_i(&o, heal);
            cs_s(&o, " BP).");
            break;
        }
        default:
            cs_s(&o, nm);
            cs_s(&o, " concentrates.");
            break;
        }
        sys_broadcast(room, cs_cstr(&o));
        cs_end_turn(room);
        _exit(0);
    }
}
