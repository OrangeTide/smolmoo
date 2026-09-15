/* taptest.h : TAP tally shared between the driver and its self-test.
 * SPDX-License-Identifier: 0BSD OR CC0-1.0
 * Copyright © 2026 Jon Mayo
 */
#ifndef TAPTEST_H
#define TAPTEST_H

/*
 * Tally over one TAP stream: a plan "1..N" and "ok"/"not ok" points, with
 * "# SKIP"/"# TODO" directives, "1..0 # SKIP" whole-file skip, and
 * "Bail out!". Handles the TAP subset the suite uses.
 */
typedef struct Tap Tap;
struct Tap {
    long plan;      /* planned count from "1..N", -1 when no plan seen */
    int  plan_skip_all; /* "1..0 # SKIP": the whole file was skipped */
    long seen;      /* test points observed */
    long passed;
    long failed;        /* real failures: "not ok" without a TODO */
    long skipped;       /* "ok ... # SKIP" */
    long todo;      /* "# TODO" points, pass or fail */
    int  bailed;        /* "Bail out!" was emitted */
};

/* Reset a tally: no plan, empty counts. */
void tap_reset(Tap *t);

/* Fold one output line into the tally. */
void tap_line(Tap *t, const char *line);

/* Non-zero when the TAP content alone is a pass: no failures, no bail out,
 * plan matches points seen. expected_out, if set, gets the plan (-1 if none). */
int tap_content_ok(const Tap *t, long *expected_out);

/* Exercise the TAP parser alone; non-zero on any failed check. */
int tap_test(void);

#endif /* TAPTEST_H */
