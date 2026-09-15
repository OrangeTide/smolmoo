/* test_chromesix.c : native unit tests for the ChromeSix verb helpers. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */
/*
 * chromesix_verb.h is header-only and reaches the host only through a handful
 * of mulibc entry points. Defining MULIBC_H suppresses the real (m68k inline
 * asm) mulibc, and the stubs below stand in for it: an in-memory property
 * store for sys_getprop/sys_setprop and a scripted d6 stream for sys_random.
 * That lets the pure decision logic (derived stats, ranges, the resolution
 * math) run on the host, so the edges that test.sh can only reach by chance
 * become exact assertions. No production file is modified.
 */

#include "test.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- stand in for mulibc.h ------------------------------------------- */
#define MULIBC_H

struct kv { int obj; char name[40]; char val[64]; };
static struct kv g_props[512];
static int g_nprops;

static void
props_clear(void)
{
    g_nprops = 0;
}

static struct kv *
prop_find(int obj, const char *name)
{
    for (int i = 0; i < g_nprops; i++)
        if (g_props[i].obj == obj && strcmp(g_props[i].name, name) == 0)
            return &g_props[i];
    return NULL;
}

static int
sys_getprop(int obj, const char *name, char *buf, int bufsz)
{
    struct kv *p = prop_find(obj, name);
    int n;

    if (!p) return -1;                  /* property not found */
    n = (int)strlen(p->val);
    if (n > bufsz) n = bufsz;
    memcpy(buf, p->val, (size_t)n);
    return n;
}

static int
sys_setprop(int obj, const char *name, const char *val)
{
    struct kv *p = prop_find(obj, name);

    if (!p) {
        if (g_nprops >= (int)(sizeof(g_props) / sizeof(g_props[0]))) return -1;
        p = &g_props[g_nprops++];
        p->obj = obj;
        snprintf(p->name, sizeof(p->name), "%s", name);
    }
    snprintf(p->val, sizeof(p->val), "%s", val);
    return 0;
}

static int
sys_broadcast(int room, const char *msg)
{
    (void)room; (void)msg;
    return 0;
}

/* Contents iteration over the flat prop store: the next object whose
 * `location` names `container`, taking ids strictly greater than `after`. */
static int
sys_next(int container, int after)
{
    int best = 0;

    for (int i = 0; i < g_nprops; i++) {
        int id = g_props[i].obj;

        if (strcmp(g_props[i].name, "location") != 0) continue;
        if (atoi(g_props[i].val) != container) continue;
        if (id <= after) continue;
        if (best == 0 || id < best) best = id;
    }
    return best;
}

/* Sum `field` over the containment subtree of `obj` (its descendants by
 * `location`), or count items when `field` is empty, mirroring the host
 * rollup. No caching is needed in the test. */
static int
sys_rollup(int obj, const char *field)
{
    int sum = 0, child = 0;

    while ((child = sys_next(obj, child)) != 0) {
        if (*field) {
            struct kv *p = prop_find(child, field);

            if (p) sum += atoi(p->val);
        } else {
            sum += 1;
        }
        sum += sys_rollup(child, field);
    }
    return sum;
}

static int
write(int fd, const void *buf, unsigned n)
{
    (void)fd; (void)buf;
    return (int)n;
}

/* scripted d6: sys_random(6) yields face-1 for the next queued face, so
 * cs_d6() (1 + sys_random(6)) returns that face. Exhausted -> a mid roll. */
static int g_faces[128];
static int g_nfaces, g_fi;

static void
set_faces(const int *f, int n)
{
    memcpy(g_faces, f, (size_t)n * sizeof(int));
    g_nfaces = n;
    g_fi = 0;
}

static int
sys_random(int max)
{
    int face = (g_fi < g_nfaces) ? g_faces[g_fi++] : 3;

    if (max == 6) return face - 1;
    return (face - 1) % max;
}

/* Some chromesix_verb.h helpers report to the player through puts(). The
 * native harness has no player, and puts() would inject stray lines into the
 * TAP stream on stdout, so silence it for this translation unit. */
#define puts(s) ((void)(s))

#include "chromesix_verb.h"

/* ---- small test helpers ---------------------------------------------- */
static void
seti(int obj, const char *name, int v)
{
    char b[16];
    snprintf(b, sizeof(b), "%d", v);
    sys_setprop(obj, name, b);
}

static int
has(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

/* Common actor/defender/weapon ids for the resolution cases. */
enum { A = 100, D = 200, W = 300, ROOM = 400 };

static void
basic_fighters(void)
{
    props_clear();
    sys_setprop(A, "name", "A");
    seti(A, "agi", 6);
    seti(A, "sk_firearms", 6);
    sys_setprop(W, "name", "gun");
    sys_setprop(W, "skill", "firearms");
    seti(W, "dmg", 12);
    sys_setprop(D, "name", "D");
    seti(D, "agi", 6);          /* pd 4 (no hook) */
    seti(D, "mig", 6);          /* soak 4, maxbp 18 */
}

/* ---- derived stats --------------------------------------------------- */
static void
t_recalc(Test *t)
{
    struct cs_derived d;

    /* baseline 6/6/6/6, street hook (+2 pd) */
    props_clear();
    seti(1, "agi", 6); seti(1, "mig", 6); seti(1, "wit", 6); seti(1, "cha", 6);
    sys_setprop(1, "hook", "street");
    cs_recalc(1, &d);
    TAP_CHECKF(t, d.pd == 6, "pd=%d", d.pd);
    TAP_CHECKF(t, d.soak == 4, "soak=%d", d.soak);
    TAP_CHECKF(t, d.maxbp == 18, "maxbp=%d", d.maxbp);
    TAP_CHECKF(t, d.maxgrit == 7, "maxgrit=%d", d.maxgrit);

    /* the chargen build: 6/9/6/3, cyber hook (+6 pd) */
    props_clear();
    seti(2, "agi", 6); seti(2, "mig", 9); seti(2, "wit", 6); seti(2, "cha", 3);
    sys_setprop(2, "hook", "cyber");
    cs_recalc(2, &d);
    TAP_CHECKF(t, d.pd == 10, "pd=%d", d.pd);
    TAP_CHECKF(t, d.soak == 6, "soak=%d", d.soak);
    TAP_CHECKF(t, d.maxbp == 21, "maxbp=%d", d.maxbp);
    TAP_CHECKF(t, d.maxgrit == 6, "maxgrit=%d", d.maxgrit);

    /* pd caps at 20 */
    props_clear();
    seti(3, "agi", 30); sys_setprop(3, "hook", "cyber");
    cs_recalc(3, &d);
    TAP_CHECKF(t, d.pd == 20, "pd cap=%d", d.pd);

    /* pd floors at 0; size lowers pd and raises soak/maxbp */
    props_clear();
    seti(4, "agi", 0); seti(4, "mig", 6); seti(4, "armor", 3); seti(4, "size", 2);
    cs_recalc(4, &d);
    TAP_CHECKF(t, d.pd == 0, "pd floor=%d", d.pd);
    TAP_CHECKF(t, d.soak == 4 + 3 + 2, "soak size+armor=%d", d.soak);
    TAP_CHECKF(t, d.maxbp == 12 + 6 + 12, "maxbp size=%d", d.maxbp);

    /* pips count: agi 7 -> 2 dice + 1 pip -> pd contribution 5 */
    props_clear();
    seti(5, "agi", 7);
    cs_recalc(5, &d);
    TAP_CHECKF(t, d.pd == 5, "pd pips=%d", d.pd);
}

static void
t_passperc(Test *t)
{
    props_clear();
    seti(1, "wit", 6);                       /* 4 + 2*2 = 8 */
    TAP_CHECKF(t, cs_passperc(1) == 8, "pp=%d", cs_passperc(1));
    props_clear();
    seti(2, "wit", 0);                       /* floor 4 */
    TAP_CHECKF(t, cs_passperc(2) == 4, "pp0=%d", cs_passperc(2));
    props_clear();
    seti(3, "wit", 6); seti(3, "sk_perception", 6);   /* pool 12 -> 4 dice */
    TAP_CHECKF(t, cs_passperc(3) == 12, "pp skill=%d", cs_passperc(3));
    props_clear();
    seti(4, "wit", 7); seti(4, "sk_perception", 1);   /* pool 8 -> 2d + 2p */
    TAP_CHECKF(t, cs_passperc(4) == 10, "pp pips=%d", cs_passperc(4));
}

static void
t_range(Test *t)
{
    int pm;

    props_clear();
    sys_setprop(W, "range", "short");
    TAP_CHECK(t, cs_reach(W) == 1);
    TAP_CHECK(t, cs_range_check(W, 0, &pm) == 1 && pm == 0);   /* in reach */
    TAP_CHECK(t, cs_range_check(W, 1, &pm) == 1 && pm == 0);   /* at reach */
    TAP_CHECK(t, cs_range_check(W, 2, &pm) == 1 && pm == -3);  /* long shot */

    props_clear();                                            /* no range: reach 0 */
    TAP_CHECK(t, cs_reach(W) == 0);
    TAP_CHECK(t, cs_range_check(W, 2, &pm) == 0);              /* out of reach */

    props_clear();
    sys_setprop(W, "range", "long");
    TAP_CHECK(t, cs_reach(W) == 2);
    TAP_CHECK(t, cs_range_check(W, 2, &pm) == 1 && pm == 0);
}

static void
t_frontage(Test *t)
{
    props_clear();
    TAP_CHECK(t, cs_frontage(1) == 2);       /* size 0 */
    props_clear();
    seti(1, "size", 1);
    TAP_CHECK(t, cs_frontage(1) == 4);       /* Large */
    props_clear();
    seti(1, "size", 3);
    TAP_CHECK(t, cs_frontage(1) == 4);
}

static void
t_scales(Test *t)
{
    struct cs_out o;
    struct { int tn; const char *w; } diff[] = {
        {6, "trivial"}, {7, "fair"}, {10, "fair"}, {11, "hard"}, {14, "hard"},
        {15, "formidable"}, {18, "formidable"}, {19, "heroic"}, {22, "heroic"},
        {23, "legendary"},
    };

    for (unsigned i = 0; i < sizeof(diff) / sizeof(diff[0]); i++) {
        o.len = 0;
        cs_difficulty(diff[i].tn, &o);
        TAP_CHECKF(t, strcmp(cs_cstr(&o), diff[i].w) == 0,
                   "tn %d -> %s", diff[i].tn, cs_cstr(&o));
    }

    o.len = 0; cs_condition(18, 18, 0, &o);
    TAP_CHECK(t, strcmp(cs_cstr(&o), "unhurt") == 0);
    o.len = 0; cs_condition(0, 18, 0, &o);
    TAP_CHECK(t, strcmp(cs_cstr(&o), "down") == 0);
    o.len = 0; cs_condition(16, 21, 0, &o);      /* 76% */
    TAP_CHECK(t, strcmp(cs_cstr(&o), "scuffed") == 0);
    o.len = 0; cs_condition(11, 21, 0, &o);      /* 52% */
    TAP_CHECK(t, strcmp(cs_cstr(&o), "bloodied") == 0);
    o.len = 0; cs_condition(6, 21, 0, &o);       /* 28% */
    TAP_CHECK(t, strcmp(cs_cstr(&o), "badly hurt") == 0);
    o.len = 0; cs_condition(3, 21, 0, &o);       /* 14% */
    TAP_CHECK(t, strcmp(cs_cstr(&o), "barely standing") == 0);
    o.len = 0; cs_condition(18, 18, 1, &o);
    TAP_CHECK(t, has(cs_cstr(&o), "favoring an injury"));
    o.len = 0; cs_condition(18, 18, 3, &o);
    TAP_CHECK(t, has(cs_cstr(&o), "crippled"));

    TAP_CHECK(t, strcmp(cs_band_word(0), "engaged") == 0);
    TAP_CHECK(t, strcmp(cs_band_word(1), "short") == 0);
    TAP_CHECK(t, strcmp(cs_band_word(2), "long") == 0);

    o.len = 0; cs_meter(&o, "HP", 21, 21);
    TAP_CHECKF(t, strcmp(cs_cstr(&o), "HP [##########] 21/21") == 0,
               "full: %s", cs_cstr(&o));
    o.len = 0; cs_meter(&o, "HP", 0, 21);
    TAP_CHECKF(t, strcmp(cs_cstr(&o), "HP [----------] 0/21") == 0,
               "empty: %s", cs_cstr(&o));

    /* the pool-to-dice notation used on sheets and checks */
    o.len = 0; cs_dice(&o, 6);  TAP_CHECK(t, strcmp(cs_cstr(&o), "2D") == 0);
    o.len = 0; cs_dice(&o, 7);  TAP_CHECK(t, strcmp(cs_cstr(&o), "2D+1") == 0);
    o.len = 0; cs_dice(&o, 2);  TAP_CHECK(t, strcmp(cs_cstr(&o), "0D+2") == 0);

    /* cs_flush empties the buffer (its bytes go out through write()) */
    o.len = 0; cs_s(&o, "x"); cs_flush(&o);
    TAP_CHECK(t, o.len == 0);
}

/* Push a roll: spend Grit for +ND (capped +2D), and split "push N" off args. */
static void
t_push(Test *t)
{
    char buf[32];

    props_clear();
    seti(1, "grit", 6);
    TAP_CHECK(t, cs_grit_push(1, 1) == 3);              /* +1D = 3 points */
    TAP_CHECK(t, cs_geti(1, "grit", -1) == 5);
    TAP_CHECK(t, cs_grit_push(1, 2) == 6);              /* +2D */
    TAP_CHECK(t, cs_geti(1, "grit", -1) == 3);
    TAP_CHECK(t, cs_grit_push(1, 0) == 0);              /* no push */
    TAP_CHECK(t, cs_geti(1, "grit", -1) == 3);
    TAP_CHECK(t, cs_grit_push(1, 5) == 6);              /* capped at +2D */
    TAP_CHECK(t, cs_geti(1, "grit", -1) == 1);
    TAP_CHECK(t, cs_grit_push(1, 2) == -1);             /* cannot afford */
    TAP_CHECK(t, cs_geti(1, "grit", -1) == 1);          /* nothing spent */

    strcpy(buf, "raider push 1");
    TAP_CHECK(t, cs_parse_push(buf) == 1 && strcmp(buf, "raider") == 0);
    strcpy(buf, "raider push 2");
    TAP_CHECK(t, cs_parse_push(buf) == 2 && strcmp(buf, "raider") == 0);
    strcpy(buf, "raider");
    TAP_CHECK(t, cs_parse_push(buf) == 0 && strcmp(buf, "raider") == 0);
    strcpy(buf, "raider push");                         /* bare push == 1 */
    TAP_CHECK(t, cs_parse_push(buf) == 1 && strcmp(buf, "raider") == 0);
    strcpy(buf, "raider push 9");                       /* capped */
    TAP_CHECK(t, cs_parse_push(buf) == 2 && strcmp(buf, "raider") == 0);
    strcpy(buf, "raider push 0");                       /* explicit zero */
    TAP_CHECK(t, cs_parse_push(buf) == 0 && strcmp(buf, "raider") == 0);
}

/* Stims: restore 2 Grit capped at max, spend a dose, and stack Crash once
 * the dose count passes the safe limit (Might dice). */
static void
t_stim(Test *t)
{
    props_clear();
    /* Might 3 -> 1 die safe; Wit/Charm 0 -> maxgrit 3. */
    seti(1, "mig", 3);
    seti(1, "grit", 0);
    seti(1, "stims", 3);

    TAP_CHECK(t, cs_stim_use(1) == 0);                 /* dose 1: safe */
    TAP_CHECKF(t, cs_geti(1, "grit", -1) == 2, "grit=%d", cs_geti(1, "grit", -1));
    TAP_CHECK(t, cs_geti(1, "stims", -1) == 2);
    TAP_CHECK(t, cs_geti(1, "crash", 0) == 0);
    TAP_CHECK(t, cs_crash_penalty(1) == 0);

    TAP_CHECK(t, cs_stim_use(1) == 1);                 /* dose 2: past limit */
    TAP_CHECKF(t, cs_geti(1, "grit", -1) == 3, "cap=%d", cs_geti(1, "grit", -1));
    TAP_CHECK(t, cs_geti(1, "crash", 0) == 1);
    TAP_CHECK(t, cs_crash_penalty(1) == 3);            /* -1D */

    TAP_CHECK(t, cs_stim_use(1) == 1);                 /* dose 3: crash stacks */
    TAP_CHECK(t, cs_geti(1, "stims", -1) == 0);
    TAP_CHECK(t, cs_geti(1, "crash", 0) == 2);
    TAP_CHECK(t, cs_crash_penalty(1) == 6);            /* -2D */

    TAP_CHECK(t, cs_stim_use(1) == -1);                /* out of stims */
    TAP_CHECK(t, cs_geti(1, "crash", 0) == 2);         /* nothing changed */
    TAP_CHECK(t, cs_geti(1, "grit", -1) == 3);
}

/* CP unlocks: whole-token match in the maneuvers list, and a flat Grit spend. */
static void
t_unlock(Test *t)
{
    props_clear();
    sys_setprop(1, "maneuvers", "smartlink,riposte");
    TAP_CHECK(t, cs_has_unlock(1, "smartlink") == 1);
    TAP_CHECK(t, cs_has_unlock(1, "riposte") == 1);
    TAP_CHECK(t, cs_has_unlock(1, "smart") == 0);      /* no partial match */
    TAP_CHECK(t, cs_has_unlock(1, "wired") == 0);      /* not learned */

    props_clear();                                     /* empty list */
    TAP_CHECK(t, cs_has_unlock(1, "smartlink") == 0);

    props_clear();
    seti(1, "grit", 3);
    TAP_CHECK(t, cs_grit_spend(1, 2) == 0);            /* spends flat cost */
    TAP_CHECK(t, cs_geti(1, "grit", -1) == 1);
    TAP_CHECK(t, cs_grit_spend(1, 2) == -1);           /* short: refused */
    TAP_CHECK(t, cs_geti(1, "grit", -1) == 1);         /* nothing spent */
}

/* The leaving-melee reaction: it fires only when the foe still holds one. */
static void
t_free_strike(Test *t)
{
    /* no reaction left: the withdrawing target is untouched */
    basic_fighters();
    seti(A, "wielded", W);
    seti(A, "react_left", 0);
    cs_free_strike(A, D, ROOM);
    TAP_CHECK(t, cs_geti(D, "bp", -1) == -1);        /* never struck */
    TAP_CHECK(t, cs_geti(A, "react_left", -1) == 0);

    /* one reaction held: it strikes and is spent */
    {
        int faces[] = {6, 6, 6, 6, 3, 3, 3, 3};      /* hit; dmg 12 net 8 */
        basic_fighters();
        seti(A, "wielded", W);
        seti(A, "react_left", 1);
        set_faces(faces, 8);
        cs_free_strike(A, D, ROOM);
        TAP_CHECK(t, cs_geti(A, "react_left", -1) == 0);   /* spent */
        TAP_CHECKF(t, cs_geti(D, "bp", -1) == 10, "bp=%d",
                   cs_geti(D, "bp", -1));                  /* 18 - 8 */
    }
}

static void
t_cover_and_roster(Test *t)
{
    int foes[8], k;

    props_clear();
    TAP_CHECK(t, cs_cover(1) == 0);
    seti(1, "cover", 2); seti(1, "cover_bonus", 2);
    TAP_CHECK(t, cs_cover(1) == 4);

    /* roster "5,7,9", self 5 -> foes {7,9} */
    props_clear();
    sys_setprop(ROOM, "cb_roster", "5,7,9");
    k = cs_roster_foes(ROOM, 5, foes, 8);
    TAP_CHECKF(t, k == 2, "foes=%d", k);
    TAP_CHECK(t, foes[0] == 7 && foes[1] == 9);

    /* 7 engaged (band 0), 9 at short (band 1) -> engaged count 1 */
    seti(7, "band", 0); seti(9, "band", 1);
    TAP_CHECKF(t, cs_engaged_count(ROOM, 5) == 1, "eng=%d",
               cs_engaged_count(ROOM, 5));
    /* down the engaged one -> 0 */
    seti(7, "downed", 1);
    TAP_CHECK(t, cs_engaged_count(ROOM, 5) == 0);
}

static void
t_parse_format(Test *t)
{
    char b[16];
    int rem;

    TAP_CHECK(t, cs_divmod(17, 5, &rem) == 3 && rem == 2);
    TAP_CHECK(t, cs_divmod(100, 10, &rem) == 10 && rem == 0);
    TAP_CHECK(t, cs_divmod(0, 3, &rem) == 0 && rem == 0);
    TAP_CHECK(t, cs_divmod(5, 0, &rem) == 0 && rem == 0);   /* div by zero guard */

    TAP_CHECK(t, cs_atoi("123") == 123);
    TAP_CHECK(t, cs_atoi("0") == 0);
    TAP_CHECK(t, cs_atoi("42abc") == 42);                  /* stops at non-digit */
    TAP_CHECK(t, cs_atoi("") == 0);

    cs_itoa(b, 305); TAP_CHECK(t, strcmp(b, "305") == 0);
    cs_itoa(b, 0);   TAP_CHECK(t, strcmp(b, "0") == 0);

    TAP_CHECK(t, cs_streq("abc", "abc"));
    TAP_CHECK(t, !cs_streq("abc", "abd"));
    TAP_CHECK(t, !cs_streq("ab", "abc"));
}

static void
t_lookup(Test *t)
{
    char hash[72];

    props_clear();
    sys_setprop(1, "hook", "cyber");
    TAP_CHECK(t, cs_hook_bonus(1) == 6);
    sys_setprop(1, "hook", "awakened");
    TAP_CHECK(t, cs_hook_bonus(1) == 4);
    sys_setprop(1, "hook", "street");
    TAP_CHECK(t, cs_hook_bonus(1) == 2);
    sys_setprop(1, "hook", "none");
    TAP_CHECK(t, cs_hook_bonus(1) == 0);

    TAP_CHECK(t, cs_skill_idx("firearms") == 0);
    TAP_CHECK(t, cs_skill_idx("brawl") == 6);
    TAP_CHECK(t, cs_skill_idx("perception") == 13);
    TAP_CHECK(t, cs_skill_idx("nope") == -1);

    props_clear();
    sys_setprop(9, "elf", "[0755,b2:DEADBEEF]");
    TAP_CHECK(t, cs_elf_hash(9, hash, sizeof(hash)) == 0);
    TAP_CHECKF(t, strcmp(hash, "DEADBEEF") == 0, "hash=%s", hash);
    props_clear();
    TAP_CHECK(t, cs_elf_hash(9, hash, sizeof(hash)) == -1);   /* no elf prop */
}

static void
t_roll(Test *t)
{
    int wild;
    int f_edge[] = {6, 2, 2, 2};       /* 12 pts -> 4 dice, first 6 */
    int f_glitch[] = {1, 2, 2, 2};
    int f_clean[] = {3, 3};            /* 7 pts -> 2 dice + 1 pip */
    int f_one[] = {4};

    set_faces(f_edge, 4);
    TAP_CHECK(t, cs_roll(12, &wild) == 12 && wild == 1);
    set_faces(f_glitch, 4);
    TAP_CHECK(t, cs_roll(12, &wild) == 7 && wild == -1);
    set_faces(f_clean, 2);
    TAP_CHECK(t, cs_roll(7, &wild) == 7 && wild == 0);     /* 1 pip + 3 + 3 */
    set_faces(f_one, 1);
    TAP_CHECK(t, cs_roll(2, &wild) == 6 && wild == 0);     /* 2 pips + 1 die */
}

/* ---- the resolution engine (cs_attack_resolve) ----------------------- */
static void
t_attack_hit_wound(Test *t)
{
    struct cs_out o;
    int down = 0, net;
    int faces[] = {2, 2, 2, 2, 5, 3, 3, 3};   /* aroll 8 hit; dmg 14 net 10 */

    basic_fighters();
    set_faces(faces, 8);
    o.len = 0;
    net = cs_attack_resolve(A, D, W, 0, 0, 0, &o, &down);
    TAP_CHECKF(t, net == 10, "net=%d", net);
    TAP_CHECK(t, down == 0);
    TAP_CHECKF(t, cs_geti(D, "bp", -1) == 8, "bp=%d", cs_geti(D, "bp", -1));
    TAP_CHECKF(t, cs_geti(D, "wounds", 0) == 1, "wounds=%d",
               cs_geti(D, "wounds", 0));
    TAP_CHECK(t, has(cs_cstr(&o), "badly hurt"));
    TAP_CHECK(t, has(cs_cstr(&o), "[WOUND]"));
}

static void
t_attack_miss(Test *t)
{
    struct cs_out o;
    int down = 0, net;
    int faces[] = {1, 1, 1, 1};        /* aroll 4 < pd 10 (cover) */

    basic_fighters();
    seti(D, "cover", 6);               /* pd 4 + 6 = 10 */
    set_faces(faces, 4);
    o.len = 0;
    net = cs_attack_resolve(A, D, W, 0, 0, 0, &o, &down);
    TAP_CHECK(t, net == 0 && down == 0);
    TAP_CHECK(t, cs_geti(D, "bp", -1) == -1);   /* unhit: no bp written */
    TAP_CHECK(t, has(cs_cstr(&o), "misses"));
    TAP_CHECK(t, has(cs_cstr(&o), "[GLITCH]"));
}

static void
t_attack_soak(Test *t)
{
    struct cs_out o;
    int down = 0, net;
    int faces[] = {6, 6, 6, 6, 1, 1, 1, 1};   /* hit; dmg 4 == soak 4 -> net 0 */

    basic_fighters();
    set_faces(faces, 8);
    o.len = 0;
    net = cs_attack_resolve(A, D, W, 0, 0, 0, &o, &down);
    TAP_CHECK(t, net == 0 && down == 0);
    TAP_CHECK(t, cs_geti(D, "downed", 0) == 0);
    TAP_CHECK(t, has(cs_cstr(&o), "armor turns it"));
}

static void
t_attack_mook(Test *t)
{
    struct cs_out o;
    int down = 0, net;
    int faces[] = {6, 6, 6, 6, 6, 6, 6, 6};

    basic_fighters();
    sys_setprop(D, "grade", "mook");
    set_faces(faces, 8);
    o.len = 0;
    net = cs_attack_resolve(A, D, W, 0, 0, 0, &o, &down);
    TAP_CHECK(t, net > 0 && down == 1);
    TAP_CHECK(t, cs_geti(D, "downed", 0) == 1);
    TAP_CHECK(t, has(cs_cstr(&o), "It drops"));
}

static void
t_attack_guard(Test *t)
{
    struct cs_out o;
    int down = 0, net;
    int faces[] = {1, 1, 1, 1};        /* aroll 4; pd 4 + 2 guard = 6 -> miss */

    basic_fighters();
    sys_setprop(D, "stance", "guard");
    seti(D, "react_left", 1);
    set_faces(faces, 4);
    o.len = 0;
    net = cs_attack_resolve(A, D, W, 0, 0, 1, &o, &down);   /* allow_react */
    TAP_CHECK(t, net == 0);
    TAP_CHECKF(t, cs_geti(D, "react_left", -1) == 0, "react=%d",
               cs_geti(D, "react_left", -1));            /* reaction spent */
    TAP_CHECK(t, has(cs_cstr(&o), "(guarded)"));
    TAP_CHECK(t, has(cs_cstr(&o), "misses"));
}

static void
t_attack_longshot_exposed(Test *t)
{
    struct cs_out o;
    int down = 0, net;
    /* pool_mod -3 -> apool 9 (3 dice); aroll 3; pd_bonus -2 -> pd 2, so the 3
     * lands where a full pd of 4 would have missed. dmg 8 -> net 4. */
    int faces[] = {1, 1, 1, 2, 2, 2, 2};

    basic_fighters();
    set_faces(faces, 7);
    o.len = 0;
    net = cs_attack_resolve(A, D, W, -3, -2, 0, &o, &down);
    TAP_CHECKF(t, net == 4, "net=%d", net);
    TAP_CHECK(t, cs_geti(D, "bp", -1) == 14);
    TAP_CHECK(t, has(cs_cstr(&o), "at long range"));
}

/* ---- social conflict (M25e) ------------------------------------------ */
static void
t_social_derived(Test *t)
{
    props_clear();
    seti(1, "cha", 6);          /* 2D */
    seti(1, "wit", 6);          /* 2D */
    /* Max Resolve = 12 + Charm dice x3; Composure = Wit dice x2 + pips;
     * Passive Resolve = 4 + (Wit + Charm dice) x2 + pips. */
    TAP_CHECKF(t, cs_max_resolve(1) == 18, "maxr=%d", cs_max_resolve(1));
    TAP_CHECKF(t, cs_composure(1) == 4, "comp=%d", cs_composure(1));
    TAP_CHECKF(t, cs_passresolve(1) == 12, "pr=%d", cs_passresolve(1));

    seti(2, "cha", 7);          /* 2D+1: dice still 2, so Max Resolve unchanged */
    seti(2, "wit", 8);          /* 2D+2 */
    TAP_CHECKF(t, cs_max_resolve(2) == 18, "maxr2=%d", cs_max_resolve(2));
    TAP_CHECKF(t, cs_composure(2) == 6, "comp2=%d", cs_composure(2));
    /* Wit + Charm = 15 pts -> 5 dice, 0 pips -> 4 + 10 = 14 */
    TAP_CHECKF(t, cs_passresolve(2) == 14, "pr2=%d", cs_passresolve(2));
}

static void
t_push_resolve(Test *t)
{
    struct cs_out o;
    int yield = 0, loss;
    /* src: Charm 2D + Command 2D = a 4-dice push. tgt: Charm 2D, Wit 2D ->
     * Passive Resolve 12, Composure 4, Max Resolve 18, leverage default 9 (3D).
     * push 6,6,6,6 = 24 lands; leverage 3,3,3 = 9, minus Composure 4 -> 5 lost. */
    int faces[] = {6, 6, 6, 6, 3, 3, 3};

    props_clear();
    seti(A, "cha", 6); seti(A, "sk_command", 6); sys_setprop(A, "name", "A");
    seti(D, "cha", 6); seti(D, "wit", 6); sys_setprop(D, "name", "D");
    set_faces(faces, 7);
    o.len = 0;
    loss = cs_push_resolve(A, D, "command", 0, &o, &yield);
    TAP_CHECKF(t, loss == 5, "loss=%d", loss);
    TAP_CHECK(t, yield == 0);
    TAP_CHECKF(t, cs_geti(D, "resolve", -1) == 13, "res=%d",
               cs_geti(D, "resolve", -1));
    TAP_CHECK(t, has(cs_cstr(&o), "leans on D (command)"));

    /* a soft target with no poise: Passive Resolve 4, Composure 0, Max Resolve
     * 12, leverage 9 (3D) all sixes -> 18 lost, so it yields outright. */
    {
        int f2[] = {6, 6, 6, 6, 6, 6, 6};

        props_clear();
        seti(A, "cha", 6); seti(A, "sk_command", 6); sys_setprop(A, "name", "A");
        sys_setprop(D, "name", "D");    /* Wit/Charm unset = 0 */
        set_faces(f2, 7);
        o.len = 0; yield = 0;
        cs_push_resolve(A, D, "command", 0, &o, &yield);
        TAP_CHECK(t, yield == 1);
        TAP_CHECK(t, cs_geti(D, "resolve", -1) == 0);
        TAP_CHECK(t, cs_geti(D, "yielded", 0) == 1);
        TAP_CHECK(t, has(cs_cstr(&o), "They yield"));
    }
}

static void
t_wrong_mode(Test *t)
{
    props_clear();
    /* no active scene: neither kind of action is out of place */
    TAP_CHECK(t, cs_wrong_mode(ROOM, 0) == 0);
    TAP_CHECK(t, cs_wrong_mode(ROOM, 1) == 0);

    /* a live social scene: a push is fine, a fight action is refused */
    seti(ROOM, "cb_active", 1);
    sys_setprop(ROOM, "cb_mode", "social");
    TAP_CHECK(t, cs_scene_social(ROOM) == 1);
    TAP_CHECK(t, cs_wrong_mode(ROOM, 1) == 0);
    TAP_CHECK(t, cs_wrong_mode(ROOM, 0) == 1);

    /* a live fight: a fight action is fine, a push is refused; an unset mode
     * reads as a fight for back-compat */
    sys_setprop(ROOM, "cb_mode", "fight");
    TAP_CHECK(t, cs_scene_social(ROOM) == 0);
    TAP_CHECK(t, cs_wrong_mode(ROOM, 0) == 0);
    TAP_CHECK(t, cs_wrong_mode(ROOM, 1) == 1);
}

/* ---- faction standing (M25f) ----------------------------------------- */
static void
t_standing(Test *t)
{
    props_clear();
    /* absent property and absent faction both read as Neutral 0 */
    TAP_CHECK(t, cs_standing(1, "combine") == 0);
    sys_setprop(1, "standing", "combine:2,ncpd:-1,comb:5");
    TAP_CHECKF(t, cs_standing(1, "combine") == 2, "combine=%d",
               cs_standing(1, "combine"));
    TAP_CHECKF(t, cs_standing(1, "ncpd") == -1, "ncpd=%d",
               cs_standing(1, "ncpd"));
    /* a queried id that is a prefix of a stored id must not match it: "comb"
     * resolves to comb:5, never combine:2 */
    TAP_CHECKF(t, cs_standing(1, "comb") == 5, "comb=%d",
               cs_standing(1, "comb"));
    TAP_CHECK(t, cs_standing(1, "arasaka") == 0);

    /* band words across the seven-step scale */
    TAP_CHECK(t, has(cs_standing_word(3), "Allied"));
    TAP_CHECK(t, has(cs_standing_word(1), "Known"));
    TAP_CHECK(t, has(cs_standing_word(0), "Neutral"));
    TAP_CHECK(t, has(cs_standing_word(-2), "Hostile"));
    TAP_CHECK(t, has(cs_standing_word(-3), "Hunted"));
}

/* M25g: anatomy, slot roles, occupancy, and free-slot search. */
static void
t_slots(Test *t)
{
    char tag[16], an[256];
    enum { ME = 500, GUN = 501, VEST = 502, KNIFE = 503 };

    props_clear();

    /* an unset anatomy defaults to the standard human frame */
    cs_anatomy(ME, an, sizeof(an));
    TAP_CHECK(t, has(an, "hand0") && has(an, "torso0") && has(an, "waist0"));

    /* part roles from the tag's leading letters */
    TAP_CHECK(t, cs_part_role("hand1") == 'h');
    TAP_CHECK(t, cs_part_role("torso0") == 'w');
    TAP_CHECK(t, cs_part_role("tentacle11") == 'd');

    /* item roles: a weapon deals damage (hold), everything worn is wear */
    seti(VEST, "soak", 2);
    seti(GUN, "dmg", 12);
    TAP_CHECK(t, cs_item_role(VEST) == 'w');
    TAP_CHECK(t, cs_item_role(GUN) == 'h');

    /* nothing occupies a slot until an item in contents names it */
    TAP_CHECK(t, cs_slot_holder(ME, "hand0") == 0);
    seti(GUN, "location", ME);
    sys_setprop(GUN, "slot", "hand0");
    TAP_CHECKF(t, cs_slot_holder(ME, "hand0") == GUN, "holder=%d",
               cs_slot_holder(ME, "hand0"));
    /* an item present but in another location does not occupy the slot */
    sys_setprop(KNIFE, "slot", "hand1");
    TAP_CHECK(t, cs_slot_holder(ME, "hand1") == 0);

    /* a hold search skips the taken hand0 and returns the free hand1 */
    TAP_CHECK(t, cs_find_free_slot(ME, 'h', 0, tag, sizeof(tag)));
    TAP_CHECKF(t, cs_streq(tag, "hand1"), "free hold=%s", tag);
    /* a wear search finds the first open wear part */
    TAP_CHECK(t, cs_find_free_slot(ME, 'w', 0, tag, sizeof(tag)));
    TAP_CHECK(t, cs_part_role(tag) == 'w');
    /* a part preference steers a wear item to its slot family */
    TAP_CHECK(t, cs_find_free_slot(ME, 'w', "back", tag, sizeof(tag)));
    TAP_CHECKF(t, cs_streq(tag, "back0"), "pref back=%s", tag);

    /* with both hands filled, a hold search fails */
    seti(KNIFE, "location", ME);
    TAP_CHECK(t, !cs_find_free_slot(ME, 'h', 0, tag, sizeof(tag)));

    /* a tentacle (dual) accepts either role */
    sys_setprop(600, "anatomy", "torso0,tentacle0");
    TAP_CHECK(t, cs_find_free_slot(600, 'h', 0, tag, sizeof(tag)));
    TAP_CHECK(t, cs_streq(tag, "tentacle0"));
    TAP_CHECK(t, cs_find_free_slot(600, 'w', 0, tag, sizeof(tag)));
}

/* M25g slice 2: ammunition depletion and reload. */
static void
t_ammo(Test *t)
{
    enum { BLADE = 520, GUN = 521 };

    props_clear();
    /* a weapon with no clip never runs dry */
    TAP_CHECK(t, !cs_uses_ammo(BLADE));
    TAP_CHECK(t, !cs_weapon_empty(BLADE));
    TAP_CHECK(t, cs_fire(BLADE) == -1);

    /* a clip weapon depletes one round per shot, clamped at zero */
    seti(GUN, "clip", 2);
    seti(GUN, "ammo", 2);
    TAP_CHECK(t, cs_uses_ammo(GUN));
    TAP_CHECK(t, !cs_weapon_empty(GUN));
    TAP_CHECKF(t, cs_fire(GUN) == 1, "after 1 shot=%d", cs_geti(GUN, "ammo", -1));
    TAP_CHECK(t, cs_fire(GUN) == 0);
    TAP_CHECK(t, cs_weapon_empty(GUN));
    TAP_CHECK(t, cs_fire(GUN) == 0);            /* stays clamped, not negative */

    /* reload refills to the clip capacity */
    TAP_CHECK(t, cs_reload_weapon(GUN) == 2);
    TAP_CHECK(t, !cs_weapon_empty(GUN));
    TAP_CHECK(t, cs_geti(GUN, "ammo", 0) == 2);
}

/* M25g slice 2: carry rating, load summing, and the encumbrance tiers. */
static void
t_encumbrance(Test *t)
{
    enum { ME = 700, A = 701, B = 702, BAG = 703, C = 704 };

    props_clear();
    seti(ME, "mig", 9);                         /* Might 3D -> carry 6 */
    TAP_CHECKF(t, cs_carry(ME) == 6, "carry=%d", cs_carry(ME));
    TAP_CHECK(t, cs_load(ME) == 0);
    TAP_CHECK(t, cs_encumbrance(ME) == 0);

    /* items in contents add their weight; at or under carry is unburdened */
    seti(A, "weight", 5);
    seti(A, "location", ME);
    TAP_CHECKF(t, cs_load(ME) == 5, "load=%d", cs_load(ME));
    TAP_CHECK(t, cs_encumbrance(ME) == 0);

    /* over carry is encumbered */
    seti(B, "weight", 3);
    seti(B, "location", ME);
    TAP_CHECKF(t, cs_load(ME) == 8, "load=%d", cs_load(ME));
    TAP_CHECK(t, cs_encumbrance(ME) == 1);

    /* a container's nested contents count too; past twice carry is overloaded */
    seti(BAG, "cap", 2);
    seti(BAG, "weight", 1);
    seti(BAG, "location", ME);
    seti(C, "weight", 6);
    seti(C, "location", BAG);
    TAP_CHECKF(t, cs_load(ME) == 15, "load=%d", cs_load(ME));
    TAP_CHECK(t, cs_encumbrance(ME) == 2);

    /* item count is the same rollup: the bag holds one item, and the whole
     * subtree under ME holds four (A, B, the bag, and C inside it) */
    TAP_CHECKF(t, cs_count(BAG) == 1, "bag count=%d", cs_count(BAG));
    TAP_CHECKF(t, cs_count(ME) == 4, "me count=%d", cs_count(ME));
}

/* M25h: room span, the deepest range band a fight can open to indoors. */
static void
t_room_span(Test *t)
{
    enum { R = 800 };

    props_clear();
    /* indoor default reaches Short (1), outdoor reaches Long (2) */
    TAP_CHECKF(t, cs_room_span(R) == 1, "indoor default=%d", cs_room_span(R));
    seti(R, "outdoor", 1);
    TAP_CHECKF(t, cs_room_span(R) == 2, "outdoor default=%d", cs_room_span(R));
    /* an explicit span overrides the kind flag and clamps to 0..2 */
    seti(R, "span", 0);
    TAP_CHECK(t, cs_room_span(R) == 0);
    seti(R, "span", 2);
    TAP_CHECK(t, cs_room_span(R) == 2);
    seti(R, "span", 5);
    TAP_CHECKF(t, cs_room_span(R) == 2, "clamp high=%d", cs_room_span(R));
}

const Case tap_cases[] = {
    { "recalc", t_recalc },
    { "passperc", t_passperc },
    { "range", t_range },
    { "frontage", t_frontage },
    { "scales", t_scales },
    { "cover_and_roster", t_cover_and_roster },
    { "parse_format", t_parse_format },
    { "lookup", t_lookup },
    { "roll", t_roll },
    { "attack_hit_wound", t_attack_hit_wound },
    { "attack_miss", t_attack_miss },
    { "attack_soak", t_attack_soak },
    { "attack_mook", t_attack_mook },
    { "attack_guard", t_attack_guard },
    { "attack_longshot_exposed", t_attack_longshot_exposed },
    { "free_strike", t_free_strike },
    { "push", t_push },
    { "stim", t_stim },
    { "unlock", t_unlock },
    { "social_derived", t_social_derived },
    { "push_resolve", t_push_resolve },
    { "wrong_mode", t_wrong_mode },
    { "standing", t_standing },
    { "slots", t_slots },
    { "ammo", t_ammo },
    { "encumbrance", t_encumbrance },
    { "room_span", t_room_span },
    { NULL, NULL },
};
