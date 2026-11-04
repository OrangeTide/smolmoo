/* verb_combat.c : spawned turn-based combat task (ChromeSix).
 * Not typed by players; verb_attack.c spawns it by ELF hash. It owns the
 * round loop: initiative, the turn prompt, the action timeout, and NPC
 * turns. Shared state lives in cb_* props on the room object; each foe's
 * range band to the player lives in a `band` prop on the foe. The roster
 * is re-read every round so foes drawn in mid-fight join the order. */

#include "chromesix_verb.h"

#define MAX_CB      8
#define TIMEOUT_MS  30000
#define NPC_PACE_MS 400
#define CP_AWARD_BASE 1     /* CP a defeated foe is worth without a cp_award */
#define WIRED_INIT  6       /* flat initiative bonus from Wired Reflexes */

/* parse the "id,id,..." roster into ids[], returning the count */
static int
parse_roster(int room, int *ids, int max)
{
    char buf[64];
    int n = sys_getprop(room, "cb_roster", buf, sizeof(buf) - 1);
    int c = 0, v = 0, have = 0, i;

    if (n <= 0) return 0;
    buf[n] = '\0';
    for (i = 0; ; i++) {
        char ch = buf[i];
        if (ch >= '0' && ch <= '9') {
            v = (v << 3) + (v << 1) + (ch - '0');
            have = 1;
        } else {
            if (have && c < max) ids[c++] = v;
            v = 0;
            have = 0;
            if (!ch) break;
        }
    }
    return c;
}

static void
write_roster(int room, int *ids, int n)
{
    struct cs_out o;
    int k;

    o.len = 0;
    for (k = 0; k < n; k++) {
        if (k) cs_s(&o, ",");
        cs_i(&o, ids[k]);
    }
    sys_setprop(room, "cb_roster", cs_cstr(&o));
}

/* A combatant is player-driven when its body resolves to a live session. An
 * empty tell is a pure probe, so this neither writes nor disturbs output. The
 * single `player` the task was spawned for is the opener; every other connected
 * combatant is a player too, which is how a second attacker is told apart from
 * an NPC. */
static int
is_pc(int obj)
{
    return sys_tell(obj, "");
}

/* count foes (the non-player side) still in the scene: not downed and not
 * yielded. In a fight nobody yields, so this reduces to "still standing"; in a
 * social scene nobody is downed, so it reduces to "has not conceded". */
static int
foes_up(int *ids, int n)
{
    int c = 0;

    for (int i = 0; i < n; i++) {
        int sh = cs_sheet(ids[i]);
        if (!is_pc(ids[i]) && !cs_geti(sh, "downed", 0)
            && !cs_geti(sh, "yielded", 0))
            c++;
    }
    return c;
}

/* count the player side still in the scene, by the same rule. The fight (or
 * scene) is lost when this reaches zero: every player is down or has conceded. */
static int
pcs_up(int *ids, int n)
{
    int c = 0;

    for (int i = 0; i < n; i++) {
        int sh = cs_sheet(ids[i]);
        if (is_pc(ids[i]) && !cs_geti(sh, "downed", 0)
            && !cs_geti(sh, "yielded", 0))
            c++;
    }
    return c;
}

/* On a won scene, grant Character Points summed over the defeated foes. Each
 * foe is worth its `cp_award`, or a base default when it names none. Fellow
 * players are skipped, so beating an ally earns nothing. The award goes to the
 * opener (the combatant the task was spawned for), who books the contract;
 * splitting it among several players is a rules call left for later. Called
 * once at the resolver's win point, so a settled scene grants nothing more. The
 * player sheet is written under the task's grant. */
static void
award_cp(int player, int *ids, int n)
{
    int psh = cs_sheet(player);
    int total = 0, i;
    struct cs_out o;

    for (i = 0; i < n; i++) {
        if (is_pc(ids[i])) continue;
        total += cs_geti(cs_sheet(ids[i]), "cp_award", CP_AWARD_BASE);
    }
    if (total <= 0) return;
    cs_seti(psh, "cp", cs_geti(psh, "cp", 0) + total);
    o.len = 0;
    cs_s(&o, "You gain ");
    cs_i(&o, total);
    cs_s(&o, total == 1 ? " Character Point." : " Character Points.");
    puts(cs_cstr(&o));
}

/* If the player holds a bounty and a defeated foe matches its target, mark
 * the job done so it can be turned in. The target is a creature name named on the
 * job giver's `job_target`; the giver's id is on the player sheet's `job_giver`.
 * Called once at the win point, where the roster is already in hand. */
static void
mark_job_done(int player, int *ids, int n)
{
    int psh = cs_sheet(player);
    int giver = cs_geti(psh, "job_giver", 0);
    char target[32], nm[32];
    int i;

    if (giver <= 0 || cs_geti(psh, "job_done", 0))
        return;
    cs_getstr(giver, "job_target", target, sizeof(target));
    if (!target[0])
        return;
    for (i = 0; i < n; i++) {
        if (is_pc(ids[i])) continue;
        cs_getstr(cs_sheet(ids[i]), "name", nm, sizeof(nm));
        if (cs_streq(nm, target)) {
            cs_seti(psh, "job_done", 1);
            return;
        }
    }
}

/* Fire a yielded NPC's on_yield hook once (rules Section 16 outcome seam).
 * The NPC sheet names a verb object in its `on_yield` prop; spawn it by ELF
 * hash, passing the NPC id as the argument. The `yield_done` flag guards
 * against re-firing. NPCs that script no hook rely on cs_push_resolve's
 * broadcast concession, so nothing more is owed here. */
static void
fire_yield_hooks(int *ids, int n)
{
    for (int i = 0; i < n; i++) {
        int sh = cs_sheet(ids[i]);
        int verb;

        if (is_pc(ids[i])) continue;
        if (!cs_geti(sh, "yielded", 0) || cs_geti(sh, "yield_done", 0)) continue;
        cs_seti(sh, "yield_done", 1);
        verb = cs_geti(sh, "on_yield", -1);
        if (verb > 0) {
            char h[72], arg[12];

            cs_itoa(arg, ids[i]);
            if (cs_elf_hash(verb, h, sizeof(h)) == 0)
                sys_spawn(h, 0, arg);
        }
    }
}

/* The Downed dying track (rules Section 7). Three cling-to-life rolls of
 * 1d6 + Might dice; 6+ stabilizes, a natural 6 stands up at 1 BP, else the
 * character dies. On death only the `dead` flag is written here; the host owns
 * the body-and-recovery track from there (Section 11, death sweep and revive). */
static void
run_dying(int sh, int room)
{
    struct cs_out o;
    char nm[32];
    int r, md = cs_divmod(cs_geti(sh, "mig", 0), 3, &r);

    cs_getstr(sh, "name", nm, sizeof(nm));
    for (int i = 0; i < 3; i++) {
        int die = cs_d6();

        o.len = 0;
        cs_s(&o, nm);
        if (die + md >= 6) {
            if (die == 6) {
                cs_seti(sh, "bp", 1);
                sys_setprop(sh, "downed", "0");
                cs_s(&o, " rallies and staggers up at 1 BP!");
            } else {
                sys_setprop(sh, "dying", "0");
                cs_s(&o, " stabilizes, unconscious but alive.");
            }
            sys_broadcast(room, cs_cstr(&o));
            return;
        }
        cs_s(&o, " is bleeding out...");
        sys_broadcast(room, cs_cstr(&o));
        sys_suspend(NPC_PACE_MS);
    }
    o.len = 0;
    cs_s(&o, nm);
    cs_s(&o, " has died.");
    sys_broadcast(room, cs_cstr(&o));
    sys_setprop(sh, "dead", "1");
}

/* the player's turn prompt in a social scene: Resolve meter, each opponent's
 * remaining Resolve, and the four Charm approaches */
static void
print_social_prompt(int round, int pc, int *ids, int n)
{
    struct cs_out o;
    int sh = cs_sheet(pc);
    int maxr = cs_max_resolve(sh);

    o.len = 0;
    cs_s(&o, "-- round ");
    cs_i(&o, round);
    cs_s(&o, ", your turn --\n");
    cs_meter(&o, "RESOLVE", cs_geti(sh, "resolve", maxr), maxr);
    cs_s(&o, "\n");
    for (int k = 0; k < n; k++) {
        int fsh, fmax;
        char fn[32];

        if (ids[k] == pc) continue;
        fsh = cs_sheet(ids[k]);
        if (cs_geti(fsh, "yielded", 0)) continue;
        fmax = cs_max_resolve(fsh);
        cs_getstr(fsh, "name", fn, sizeof(fn));
        cs_s(&o, "  ");
        cs_s(&o, fn);
        cs_s(&o, " (");
        cs_i(&o, cs_geti(fsh, "resolve", fmax));
        cs_s(&o, "/");
        cs_i(&o, fmax);
        cs_s(&o, " resolve, ");
        cs_difficulty(cs_passresolve(fsh), &o);
        cs_s(&o, " to sway)\n");
    }
    cs_s(&o, "Actions: command, negotiate, con, perform, flee, hold\n");
    sys_tell(pc, cs_cstr(&o));
}

static void
print_prompt(int round, int pc, int *ids, int n)
{
    struct cs_out o;
    struct cs_derived d;
    int sh = cs_sheet(pc);
    int cover = cs_geti(sh, "cover", 0);

    cs_recalc(sh, &d);
    o.len = 0;
    cs_s(&o, "-- round ");
    cs_i(&o, round);
    cs_s(&o, ", your turn --\n");
    cs_meter(&o, "HP", cs_geti(sh, "bp", d.maxbp), d.maxbp);
    cs_s(&o, "  ");
    cs_meter(&o, "GRIT", cs_geti(sh, "grit", d.maxgrit), d.maxgrit);
    cs_s(&o, "\n");
    if (cover >= 4) cs_s(&o, "You are in heavy cover.\n");
    else if (cover >= 2) cs_s(&o, "You are in light cover.\n");
    for (int k = 0; k < n; k++) {
        int fsh;
        struct cs_derived fd;
        char fn[32];

        if (ids[k] == pc) continue;
        fsh = cs_sheet(ids[k]);
        if (cs_geti(fsh, "downed", 0)) continue;
        cs_getstr(fsh, "name", fn, sizeof(fn));
        cs_recalc(fsh, &fd);
        cs_s(&o, "  ");
        cs_s(&o, fn);
        cs_s(&o, " at ");
        cs_s(&o, cs_band_word(cs_geti(fsh, "band", 0)));
        cs_s(&o, " range (");
        cs_condition(cs_geti(fsh, "bp", fd.maxbp), fd.maxbp,
                     cs_geti(fsh, "wounds", 0), &o);
        cs_s(&o, ", ");
        cs_difficulty(fd.pd + cs_cover(fsh), &o);
        cs_s(&o, " to hit)\n");
    }
    cs_s(&o, "Actions: attack, cover, close, engage, retreat, "
            "disengage, go, hold\n");
    sys_tell(pc, cs_cstr(&o));
}

/* The player-driven combatant an NPC turns on: the first on the roster still
 * standing, so with one player it is that player and with several the foes
 * focus the first in initiative order still up. Zero when no player is up. */
static int
pick_target(int *ids, int n)
{
    for (int i = 0; i < n; i++) {
        int sh = cs_sheet(ids[i]);

        if (is_pc(ids[i]) && !cs_geti(sh, "downed", 0)
            && !cs_geti(sh, "yielded", 0))
            return ids[i];
    }
    return 0;
}

/* One NPC's turn: close toward its target when out of reach (blocked by
 * frontage when the melee is full), otherwise attack. */
static void
npc_turn(int me, int target, int room)
{
    struct cs_out o;
    int sh = cs_sheet(me), psh = cs_sheet(target);
    int wpn = cs_geti(sh, "wielded", 305);
    int band = cs_geti(sh, "band", 0);
    int reach = cs_reach(wpn), gap = band - reach;
    int down, pm, pdb;
    char nm[32];

    if (cs_geti(psh, "downed", 0)) return;
    o.len = 0;
    cs_getstr(sh, "name", nm, sizeof(nm));

    if (gap >= 2 || (gap >= 1 && reach == 0)) {
        int nb = band - 1;

        if (nb == 0 &&
            cs_engaged_count(room, target) >= cs_frontage(psh)) {
            cs_s(&o, nm);
            cs_s(&o, " cannot find room in the melee.");
            sys_broadcast(room, cs_cstr(&o));
            return;
        }
        cs_seti(sh, "band", nb);
        cs_seti(sh, "moved", 1);
        cs_s(&o, nm);
        cs_s(&o, " closes the distance.");
        sys_broadcast(room, cs_cstr(&o));
        return;
    }

    pm = (gap == 1) ? -3 : 0;
    pdb = (band > 0 && cs_geti(psh, "moved", 0)) ? 2 : 0;
    cs_attack_resolve(sh, psh, wpn, pm, pdb, 1, &o, &down, 0, room);
    cs_s(&o, "\n");
    sys_broadcast(room, cs_cstr(&o));
}

/* One NPC's social turn: push its target, draining their Resolve. The NPC's
 * approach is its `approach` prop (a Charm skill), defaulting to Command. */
static void
npc_social_turn(int me, int target, int room)
{
    struct cs_out o;
    int sh = cs_sheet(me), psh = cs_sheet(target);
    char skill[16];
    int yield;

    if (cs_geti(psh, "yielded", 0)) return;
    cs_getstr(sh, "approach", skill, sizeof(skill));
    if (!skill[0]) { skill[0] = 'c'; skill[1] = 'o'; skill[2] = 'm';
                     skill[3] = 'm'; skill[4] = 'a'; skill[5] = 'n';
                     skill[6] = 'd'; skill[7] = '\0'; }
    o.len = 0;
    cs_push_resolve(sh, psh, skill, -cs_crash_penalty(sh), &o, &yield);
    cs_s(&o, "\n");
    sys_broadcast(room, cs_cstr(&o));
}

int
main(void)
{
    int room = vm_args->room;
    int player = vm_args->player;
    int ids[MAX_CB], init[MAX_CB];
    int n, i, round;

    grant_accept();   /* the turn task writes the room and every combatant */

    /* guard: do nothing unless a fight is actually set up */
    if (cs_geti(room, "cb_active", 0) != 1) _exit(0);
    sys_suspend(0);

    n = parse_roster(room, ids, MAX_CB);
    if (n < 2) { sys_setprop(room, "cb_active", "0"); _exit(0); }

    /* initiative: a Wit pool per combatant, sorted descending, and written
     * back so the roster order is the turn order for every round. Wired Reflexes
     * adds a flat bonus to the roll, so its holder tends to act earlier. */
    for (i = 0; i < n; i++) {
        int w, s = cs_sheet(ids[i]);
        init[i] = cs_roll(cs_geti(s, "wit", 0), &w);
        if (cs_has_unlock(s, "wired")) init[i] += WIRED_INIT;
    }
    for (i = 1; i < n; i++) {
        int ki = init[i], kd = ids[i], j = i - 1;

        while (j >= 0 && init[j] < ki) {
            init[j + 1] = init[j];
            ids[j + 1] = ids[j];
            j--;
        }
        init[j + 1] = ki;
        ids[j + 1] = kd;
    }
    write_roster(room, ids, n);

    /* publish our task id so a combatant's action can wake us at once
     * (cs_end_turn -> sys_post by id), instead of the turn loop polling
     * cb_acted. We do not listen on the room, so combat never intercepts other
     * commands typed in the fight. */
    cs_seti(room, "cb_task", sys_taskid());

    /* everyone holds a reaction from the opening of combat (elite: two); an
     * ambushed combatant is caught flat-footed and holds none this round */
    for (i = 0; i < n; i++) {
        char g[12];
        int s = cs_sheet(ids[i]);
        cs_getstr(s, "grade", g, sizeof(g));
        cs_seti(s, "react_left",
                cs_geti(s, "surprised", 0) ? 0 : (cs_streq(g, "elite") ? 2 : 1));
    }

    for (round = 1; ; round++) {
        cs_seti(room, "cb_round", round);
        n = parse_roster(room, ids, MAX_CB);      /* pick up any joiners */
        for (i = 0; i < n; i++) {
            int me = ids[i];
            int sh = cs_sheet(me);

            if (cs_geti(sh, "downed", 0)) continue;
            /* Ongoing damage: bleed ticks at the start of the turn and
             * can drop the combatant, who then loses the turn. */
            cs_cond_tick(sh, room);
            if (cs_geti(sh, "downed", 0)) { sys_suspend(NPC_PACE_MS); continue; }
            /* an ambushed foe loses this one round's action, then recovers */
            if (!is_pc(me) && cs_geti(sh, "surprised", 0)) {
                struct cs_out o;
                char nm[32];

                cs_getstr(sh, "name", nm, sizeof(nm));
                cs_seti(sh, "react_left", 0);
                sys_setprop(sh, "surprised", "0");
                o.len = 0;
                cs_s(&o, nm);
                cs_s(&o, " is caught flat-footed and loses the round.");
                sys_broadcast(room, cs_cstr(&o));
                sys_suspend(NPC_PACE_MS);
                continue;
            }
            /* Stunned: lose this turn's action, though a reaction is still
             * allowed. Clears here, so it costs exactly one turn. */
            if (cs_geti(sh, "stunned", 0)) {
                struct cs_out o;
                char nm[32], g[12];

                cs_getstr(sh, "name", nm, sizeof(nm));
                cs_getstr(sh, "grade", g, sizeof(g));
                cs_seti(sh, "stunned", 0);
                cs_seti(sh, "react_left", cs_streq(g, "elite") ? 2 : 1);
                o.len = 0;
                cs_s(&o, nm);
                cs_s(&o, " is stunned and cannot act.");
                sys_broadcast(room, cs_cstr(&o));
                sys_suspend(NPC_PACE_MS);
                continue;
            }
            cs_seti(room, "cb_turn", me);
            cs_seti(room, "cb_acted", 0);
            /* the Cover-support bonus and the moved flag last only until
             * the combatant's own next turn; the reaction slot refreshes */
            sys_setprop(sh, "cover_bonus", "0");
            sys_setprop(sh, "moved", "0");
            {
                char g[12];
                cs_getstr(sh, "grade", g, sizeof(g));
                cs_seti(sh, "react_left", cs_streq(g, "elite") ? 2 : 1);
            }

            if (is_pc(me)) {
                int waited = 0, acted = 0;
                struct verb_event ev;

                if (cs_scene_social(room)) print_social_prompt(round, me, ids, n);
                else print_prompt(round, me, ids, n);
                /* Block for the whole turn timeout; the player's action verb
                 * (cs_end_turn) posts an EV_WAKE that resumes us immediately.
                 * A timeout wake carries no event and advances the clock; the
                 * action's wake carries one and does not, so a double-fired
                 * wake cannot burn the turn. */
                while (waited < TIMEOUT_MS) {
                    int chunk = TIMEOUT_MS - waited, woke = 0;

                    sys_suspend(chunk);
                    while (sys_getmsg(&ev, sizeof(ev)) >= 0) woke = 1;
                    if (cs_geti(room, "cb_active", 0) != 1) _exit(0);
                    if (cs_geti(room, "cb_acted", 0) == 1) { acted = 1; break; }
                    if (!woke) waited += chunk;
                }
                if (!acted) sys_tell(me, "You hesitate; the moment passes.");
            } else {
                int target = pick_target(ids, n);

                if (target) {
                    if (cs_scene_social(room)) npc_social_turn(me, target, room);
                    else npc_turn(me, target, room);
                }
                sys_suspend(NPC_PACE_MS);
            }

            cs_rally(sh, room);   /* A shaken combatant may shake it off */

            {
                int social = cs_scene_social(room);
                struct cs_out o;

                if (social) fire_yield_hooks(ids, n);

                if (cs_geti(room, "cb_fled", 0)) {
                    o.len = 0;
                    cs_s(&o, social ? "The two break off." : "The fight breaks off.");
                    sys_broadcast(room, cs_cstr(&o));
                    goto done;
                }

                /* Run the dying track for any player cut down this turn. A
                 * player who rallies to 1 BP fights on, so only the ones who
                 * stay down (dead or stabilized) are marked resolved; a player
                 * downed again after rallying runs it afresh. */
                if (!social) {
                    for (int j = 0; j < n; j++) {
                        int jsh = cs_sheet(ids[j]);

                        if (is_pc(ids[j]) && cs_geti(jsh, "downed", 0)
                            && !cs_geti(jsh, "dying_seen", 0)) {
                            run_dying(jsh, room);
                            if (cs_geti(jsh, "downed", 0))
                                cs_seti(jsh, "dying_seen", 1);
                        }
                    }
                }

                /* Win: the whole foe side is out. */
                if (foes_up(ids, n) == 0) {
                    o.len = 0;
                    cs_s(&o, social ? "The matter is settled." : "The fight is over.");
                    sys_broadcast(room, cs_cstr(&o));
                    award_cp(player, ids, n);   /* CP for the win */
                    mark_job_done(player, ids, n);  /* Bounty complete */
                    goto done;
                }
                /* Loss: every player is down (fight) or has conceded (social).
                 * In a solo scene this is the one player; in a multi-attacker
                 * fight it waits for the last one to fall. */
                if (pcs_up(ids, n) == 0) {
                    o.len = 0;
                    cs_s(&o, social
                         ? "The challengers all back down; the matter is lost for now."
                         : "The fighters are all down; the field is lost.");
                    sys_broadcast(room, cs_cstr(&o));
                    goto done;
                }
            }
        }
    }
done:
    n = parse_roster(room, ids, MAX_CB);
    for (i = 0; i < n; i++) {
        int sh = cs_sheet(ids[i]);
        sys_setprop(sh, "cover", "0");
        sys_setprop(sh, "cover_bonus", "0");
        sys_setprop(sh, "moved", "0");
        sys_setprop(sh, "stance", "");
        sys_setprop(sh, "react_left", "0");
        sys_setprop(sh, "band", "0");
        sys_setprop(sh, "surprised", "0");
        sys_setprop(sh, "move_bank", "0");     /* Slowed half-step progress */
        sys_setprop(sh, "dying_seen", "0");    /* per-down dying-track guard */
        /* a yield is for the scene (rules Section 16): clear it at teardown so
         * it neither blocks a later social scene nor, via foes_up, ends a later
         * fight early */
        sys_setprop(sh, "yielded", "0");
        sys_setprop(sh, "yield_done", "0");
        /* End the lingering conditions with the scene, so a reused sheet
         * does not carry Prone, Stun, bleed, Exposed, or Marked into its next
         * fight. Timed buffs keep their own countdown. */
        cs_clear_conditions(sh);
    }
    sys_setprop(room, "cb_active", "0");
    sys_setprop(room, "cb_turn", "0");
    sys_setprop(room, "cb_acted", "0");
    sys_setprop(room, "cb_fled", "0");
    sys_setprop(room, "cb_mode", "fight");
    sys_setprop(room, "cb_task", "-1");     /* no live turn task between fights */
    _exit(0);
}
