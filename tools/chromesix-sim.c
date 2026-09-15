/* ChromeSix balance simulator.
 *
 * Models the core resolution loop from chromesix.md to check the first-pass
 * numbers: hit rates, time-to-down, the Passive Defense dial, lethality of
 * the dying window, and the stim money sink. Monte Carlo over many trials.
 *
 * A standalone analysis tool, independent of the MUD server build. Rerun it
 * after changing a rules number to see the effect.
 *
 *   cc -O2 -o chromesix-sim chromesix-sim.c && ./chromesix-sim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRIALS 300000

static unsigned long rng_state = 0x2545F4914F6CDD1DULL;
static int d6(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return 1 + (int)(rng_state % 6);
}

/* Roll a pool of `points` (3 pts = 1D + pips). One die is the Wild Die.
 * Returns total; sets *wild to +1 (Edge, face 6), -1 (Glitch, face 1), else 0.
 * The wild face is always added (no explode, no removal). */
static int roll_pool(int points, int *wild) {
    if (points < 3) points = 3;              /* floor at 1D */
    int dice = points / 3, pips = points % 3;
    int w = d6();
    if (wild) *wild = (w == 6) ? 1 : (w == 1) ? -1 : 0;
    int total = w + pips;
    for (int i = 1; i < dice; i++) total += d6();
    return total;
}

typedef struct {
    const char *name;
    int atk_pool;      /* points: governing attribute + skill */
    int pd;            /* Passive Defense TN */
    int soak;          /* Soak TN */
    int dmg_pool;      /* weapon damage rating in points */
    int max_bp;
    int mook;          /* 1 = downs on first net-damage hit */
    int init_pool;     /* Wit pool points */
    int might_pool;    /* Might pool for dying survival */
} Combatant;

/* One attack from a on d, given a to-hit modifier. Returns net damage (>=0)
 * and reports whether it hit via *hit. Applies the Edges that change a single
 * exchange (Armor Gap, Tactical Opening, Overcharge). */
static int attack_once(const Combatant *a, const Combatant *d, int atk_mod, int *hit) {
    int wild;
    int pd = d->pd;
    int atk = roll_pool(a->atk_pool + atk_mod, &wild);
    int ignore_soak = 0, dmg_bonus = 0;
    if (wild == 1) {
        int g = 1 + rand() % 4;              /* Glitch: cross-turn, minor here */
        (void)g;
    } else if (wild == 6) {
        int e = 1 + rand() % 4;              /* Edge */
        if (e == 1) ignore_soak = 1;         /* Armor Gap */
        else if (e == 2) pd -= 3;            /* Tactical Opening */
        else if (e == 3) dmg_bonus = 3;      /* Overcharge +1D */
        /* e == 4 Adrenal Rush: attacker self-heal, not modeled here */
    }
    if (atk < pd) { *hit = 0; return 0; }    /* miss; ties go to attacker */
    *hit = 1;
    int soak = ignore_soak ? 0 : d->soak;
    int dmg = roll_pool(a->dmg_pool + dmg_bonus, NULL);
    int net = dmg - soak;
    return net > 0 ? net : 0;
}

/* Wound penalty in points from wound level (each level -1D = -3 pts). */
static int wound_pen(int wl) { return -3 * wl; }

/* Simulate a fight to the death between two combatants. Returns 1 if `a`
 * wins (downs d), 0 if a is downed. Rounds counted in *rounds. Applies the
 * wound ladder and its dice penalties. Initiative rolled once at start. */
static int fight(const Combatant *A, const Combatant *B, int *rounds) {
    int abp = A->max_bp, bbp = B->max_bp;
    int awl = 0, bwl = 0;                     /* wound levels */
    int r;
    /* initiative once */
    int ai = roll_pool(A->init_pool, NULL);
    int bi = roll_pool(B->init_pool, NULL);
    int a_first = (ai >= bi);
    for (r = 1; r <= 200; r++) {
        const Combatant *first = a_first ? A : B;
        const Combatant *second = a_first ? B : A;
        int *fbp = a_first ? &abp : &bbp;
        int *sbp = a_first ? &bbp : &abp;
        int *fwl = a_first ? &awl : &bwl;
        int *swl = a_first ? &bwl : &awl;
        const Combatant *fatt = first, *fdef = second;
        const Combatant *satt = second, *sdef = first;

        /* first acts (if still up) */
        if (*fbp > 0) {
            int hit;
            int net = attack_once(fatt, fdef, wound_pen(*fwl), &hit);
            if (net > 0) {
                if (net >= (sdef->max_bp + 1) / 2 && !sdef->mook && *swl < 3) (*swl)++;
                *sbp -= net;
                if (sdef->mook) *sbp = 0;    /* mook downs on first net hit */
            }
        }
        /* second acts (if still up) */
        if (*sbp > 0) {
            int hit;
            int net = attack_once(satt, sdef, wound_pen(*swl), &hit);
            if (net > 0) {
                if (net >= (fdef->max_bp + 1) / 2 && !fdef->mook && *fwl < 3) (*fwl)++;
                *fbp -= net;
                if (fdef->mook) *fbp = 0;
            }
        }
        if (abp <= 0 || bbp <= 0) break;
    }
    *rounds = r;
    if (abp <= 0 && bbp <= 0) return -1;      /* both down (rare) */
    return abp > 0 ? 1 : 0;
}

/* Adopted dying mechanic: a Downed PC with no ally (worst case; any ally
 * auto-stabilizes). Three rounds; once per round roll 1d6 + Might dice (flat
 * bonus, not a pool) and stabilize on 6+. Die if all three fail. */
static int dying_window(int might_pool, int *rounds_survived) {
    int md = might_pool / 3;
    for (int r = 1; r <= 3; r++) {
        if (d6() + md >= 6) { *rounds_survived = r; return 0; }  /* stabilized */
    }
    *rounds_survived = 3;
    return 1;                                                    /* dead */
}

static void run_matchup(const char *label, const Combatant *A, const Combatant *B) {
    long a_wins = 0;
    long long total_rounds = 0;
    for (long t = 0; t < TRIALS; t++) {
        int r;
        int res = fight(A, B, &r);
        if (res == 1) a_wins++;
        total_rounds += r;
    }
    printf("  %-28s PC-win %5.1f%%   avg rounds %.2f\n",
           label, 100.0 * a_wins / TRIALS, (double)total_rounds / TRIALS);
}

/* Report hit% and avg net damage for a single attacker vs a defender. */
static void run_hitrate(const char *label, const Combatant *A, const Combatant *B) {
    long hits = 0; long long net_sum = 0; long net_hits = 0;
    for (long t = 0; t < TRIALS; t++) {
        int hit;
        int net = attack_once(A, B, 0, &hit);
        if (hit) { hits++; net_sum += net; if (net > 0) net_hits++; }
    }
    printf("  %-28s hit %5.1f%%   avg net/hit %.2f\n",
           label, 100.0 * hits / TRIALS,
           net_hits ? (double)net_sum / net_hits : 0.0);
}

int main(void) {
    /* Archetypes built strictly from the creation rules (24 attr pts over a
     * 1D base, cap 3D; 12 skill pts, cap +2D; a Hook PD bonus).
     * pd = Agility dice*2 + Hook (+ armor gives soak, not pd). */

    /* Shooter: Agi 3D, Wit 2D, Mig 2D, Cha 1D; Firearms +2D; Cyber-Augmented +6 PD.
       atk = Agi3D + Firearms2D = 15 pts (5D). pd = 3*2 + 6 = 12. Medium armor +2:
       soak = Mig 2D*2 + 2 = 6. dmg = SMG 5D = 15. max_bp = 12 + 2*3 = 18. */
    Combatant shooter = {"Shooter (5D/SMG)", 15, 12, 6, 15, 18, 0, 6, 6};

    /* Bruiser: Mig 3D, Agi 2D, Cha 2D, Wit 1D; Brawl +2D; Cyber +6 PD.
       atk = Mig3D + Brawl2D = 15. pd = 2*2 + 6 = 10. Heavy armor +3:
       soak = Mig 3D*2 + 3 = 9. dmg = unarmed is weak; give a Katana 4D+1=13.
       max_bp = 12 + 3*3 = 21. */
    Combatant bruiser = {"Bruiser (5D/Katana)", 15, 10, 9, 13, 21, 0, 3, 9};

    /* NPC grades from the mapping's quick profiles. */
    Combatant mook  = {"Mook",  9, 10, 4, 12, 0,  1, 6, 6};   /* atk 3D, dmg pistol 4D */
    Combatant tough = {"Tough", 12, 12, 6, 12, 18, 0, 6, 6};  /* atk 4D, dmg 4D */
    Combatant elite = {"Elite", 15, 14, 9, 15, 33, 0, 9, 12}; /* atk 5D, dmg 5D, big BP, Mig 4D */

    printf("== Hit rate and net damage (single attack, no wounds) ==\n");
    run_hitrate("shooter -> tough (PD12)", &shooter, &tough);
    run_hitrate("shooter -> elite (PD14)", &shooter, &elite);
    run_hitrate("shooter -> mook  (PD10)", &shooter, &mook);
    run_hitrate("tough   -> shooter(PD12)", &tough, &shooter);
    run_hitrate("elite   -> shooter(PD12)", &elite, &shooter);
    run_hitrate("mook    -> shooter(PD12)", &mook, &shooter);

    printf("\n== 1v1 fights to the down (PC listed first) ==\n");
    run_matchup("shooter vs mook",  &shooter, &mook);
    run_matchup("shooter vs tough", &shooter, &tough);
    run_matchup("shooter vs elite", &shooter, &elite);
    run_matchup("bruiser vs tough", &bruiser, &tough);
    run_matchup("bruiser vs elite", &bruiser, &elite);

    printf("\n== Defense sweep: how hit%% responds to Passive Defense ==\n");
    {
        int pds[] = {8,10,12,14,16,18,20};
        for (int p = 0; p < 7; p++) {
            Combatant dd = {"", 0, pds[p], 0, 0, 0, 0, 0, 0};
            Combatant a5 = {"", 15, 0,0,0,0,0,0,0};  /* 5D */
            Combatant a6 = {"", 18, 0,0,0,0,0,0,0};  /* 6D */
            long h5=0,h6=0;
            for (long t=0;t<TRIALS;t++){int hit; attack_once(&a5,&dd,0,&hit); h5+=hit;}
            for (long t=0;t<TRIALS;t++){int hit; attack_once(&a6,&dd,0,&hit); h6+=hit;}
            printf("  PD %2d   5D pool hit %5.1f%%   6D pool hit %5.1f%%\n",
                   pds[p], 100.0*h5/TRIALS, 100.0*h6/TRIALS);
        }
    }

    printf("\n== What the new defensive layer buys (Agi 3D shooter vs 5D pool) ==\n");
    {
        struct { const char *desc; int pd; } build[] = {
            {"bare (Agi 3D, no Hook)",                 6},
            {"+ Cyber-Augmented Hook (+6)",           12},
            {"+ Kinetic Absorbers cyber (+1)",        13},
            {"+ Reflex Weave gear (+1)",              14},
            {"+ Vector Field Projector (+2)",         16},
            {"+ heavy cover (+4), at the cap",        20},
        };
        Combatant a5 = {"", 15, 0,0,0,0,0,0,0};
        for (int i = 0; i < 6; i++) {
            int pd = build[i].pd; if (pd > 20) pd = 20;
            Combatant dd = {"", 0, pd, 0,0,0,0,0,0};
            long h=0; for(long t=0;t<TRIALS;t++){int hit; attack_once(&a5,&dd,0,&hit); h+=hit;}
            printf("  PD %2d  hit %5.1f%%   %s\n", pd, 100.0*h/TRIALS, build[i].desc);
        }
    }

    printf("\n== Dying window (adopted: 1d6 + Might dice vs 6, 3 rounds, solo) ==\n");
    {
        int mps[] = {3, 6, 9, 12};   /* Might 1D..4D */
        for (int i = 0; i < 4; i++) {
            long dead = 0; long long rs = 0;
            for (long t = 0; t < TRIALS; t++) { int r; dead += dying_window(mps[i], &r); rs += r; }
            printf("  Might %dD   death %5.1f%%   avg rounds %.2f  (ally auto-saves)\n",
                   mps[i]/3, 100.0*dead/TRIALS, (double)rs/TRIALS);
        }
    }

    printf("\n== Stim money sink (stim 75 cr, +2 Grit, safe limit = Might dice) ==\n");
    {
        /* A combat PC burns some Grit per fight (push-a-roll + a maneuver) and
         * tops up with stims between fights. Show cost to sustain over a
         * 3-fight session and whether it forces the crash. Max Grit ~6. */
        int fights = 3;
        int burns[] = {3, 4, 5};              /* Grit spent per fight */
        int mds[] = {2, 3};                   /* Might 2D (shooter), 3D (bruiser) */
        printf("  session = %d fights, refill by stims between them:\n", fights);
        for (int m = 0; m < 2; m++) {
            for (int b = 0; b < 3; b++) {
                int burn = burns[b], md = mds[m];
                int grit_needed = burn * fights;          /* total Grit to replace */
                int stims = (grit_needed + 1) / 2;        /* 2 Grit per stim */
                int cost = stims * 75;
                /* doses beyond md between two rests inflict Crash; with a rest
                 * each fight the per-rest cap is md, so crash only if a single
                 * refill exceeds md doses. Per-fight refill = ceil(burn/2). */
                int per_fight_doses = (burn + 1) / 2;
                int crash = per_fight_doses > md ? per_fight_doses - md : 0;
                printf("    Might %dD, %d Grit/fight: %2d stims, %4d cr/session%s\n",
                       md, burn, stims, cost,
                       crash ? "   (forces the crash: -1D per extra dose)" : "");
            }
        }
        printf("  pay: odd job 100-400, solid contract 400-1500, major 2000+\n");
    }
    return 0;
}
