/* verb_test_dist.c : smoke test that sys_random and roll_pool are sane */
/* Copyright (c) 2026 Jon Mayo <jon@rm-f.net>
 * SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* sys_random(6) samples per run. Expected 1/6 per face. */
#define FACE_N   3000
#define FACE_LO  350       /* 500 expected, ~7 sigma band */
#define FACE_HI  650

/* roll_pool samples. A 2D+1 pool (7 points) totals in [3,13]; the wild
 * die shows an Edge (6) or Glitch (1) about one time in six. */
#define POOL_N   600
#define POOL_PTS 7
#define POOL_MIN 3
#define POOL_MAX 13
#define WILD_LO  50        /* 100 expected each, loose band */
#define WILD_HI  150

void
_start(void)
{
    struct cs_out o;
    int hist[6] = { 0, 0, 0, 0, 0, 0 };
    int edges = 0, glitches = 0, out_of_range = 0;
    int ok = 1;

    o.len = 0;

    /* raw uniformity: every face must appear within a sane band */
    for (int i = 0; i < FACE_N; i++) {
        int r = sys_random(6);
        if (r < 0 || r >= 6) { ok = 0; continue; }
        hist[r]++;
    }
    for (int f = 0; f < 6; f++)
        if (hist[f] < FACE_LO || hist[f] > FACE_HI)
            ok = 0;

    /* pool engine: totals stay in range, Edge and Glitch both occur */
    for (int i = 0; i < POOL_N; i++) {
        int wild;
        int total = cs_roll(POOL_PTS, &wild);

        if (total < POOL_MIN || total > POOL_MAX)
            out_of_range++;
        if (wild > 0) edges++;
        else if (wild < 0) glitches++;
    }
    if (out_of_range)
        ok = 0;
    if (edges < WILD_LO || edges > WILD_HI)
        ok = 0;
    if (glitches < WILD_LO || glitches > WILD_HI)
        ok = 0;

    cs_s(&o, "DIST faces:");
    for (int f = 0; f < 6; f++) { cs_s(&o, " "); cs_i(&o, hist[f]); }
    cs_s(&o, " edge "); cs_i(&o, edges);
    cs_s(&o, " glitch "); cs_i(&o, glitches);
    cs_s(&o, "\n");
    cs_flush(&o);

    puts(ok ? "DIST:OK" : "DIST:BAD");
    _exit(0);
}
