/* taptest_selftest.c : unit tests for the TAP parser in taptest.c.
 * SPDX-License-Identifier: 0BSD OR CC0-1.0
 * Copyright © 2026 Jon Mayo
 *
 * Feeds hand-written TAP line lists through the parser and checks the tally.
 * Run via "taptest --self-test"; see taptest.c for the driver.
 */

#include "taptest.h"
#include <stdio.h>

static int failures;    /* CHECK() failures in the parser self-test */

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", \
            __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

/* Feed a NULL-terminated line list into a fresh tally. */
static void
feed(Tap *t, const char *const *lines)
{
    tap_reset(t);
    for (; *lines; lines++)
        tap_line(t, *lines);
}

int
tap_test(void)
{
    Tap t;
    long expected;

    /* A plan with every point passing. */
    {
        static const char *const lines[] = {
            "TAP version 14",
            "1..2",
            "ok 1 - first\n",
            "ok 2 - second\n",
            NULL,
        };
        feed(&t, lines);
        CHECK(t.seen == 2);
        CHECK(t.passed == 2);
        CHECK(t.failed == 0);
        CHECK(tap_content_ok(&t, &expected) == 1);
        CHECK(expected == 2);
    }

    /* A real failure plus an expected (TODO) failure. */
    {
        static const char *const lines[] = {
            "1..3",
            "ok 1",
            "not ok 2 - broken",
            "not ok 3 - work in progress # TODO",
            NULL,
        };
        feed(&t, lines);
        CHECK(t.seen == 3);
        CHECK(t.passed == 1);
        CHECK(t.failed == 1);   /* the TODO does not count as a failure */
        CHECK(t.todo == 1);
        CHECK(tap_content_ok(&t, NULL) == 0);
    }

    /* A SKIP directive marks a passing point as skipped. */
    {
        static const char *const lines[] = {
            "1..1",
            "ok 1 - later # skip no backend yet",
            NULL,
        };
        feed(&t, lines);
        CHECK(t.seen == 1);
        CHECK(t.passed == 0);
        CHECK(t.skipped == 1);
        CHECK(tap_content_ok(&t, NULL) == 1);
    }

    /* A whole-file skip: "1..0 # SKIP". */
    {
        static const char *const lines[] = {
            "1..0 # SKIP no display",
            NULL,
        };
        feed(&t, lines);
        CHECK(t.plan_skip_all == 1);
        CHECK(t.seen == 0);
        CHECK(tap_content_ok(&t, &expected) == 1);
        CHECK(expected == 0);
    }

    /* Fewer points than planned is a failure. */
    {
        static const char *const lines[] = {
            "1..5",
            "ok 1",
            "ok 2",
            NULL,
        };
        feed(&t, lines);
        CHECK(t.seen == 2);
        CHECK(tap_content_ok(&t, &expected) == 0);
        CHECK(expected == 5);
    }

    /* Bail out! aborts the file even with no failing point. */
    {
        static const char *const lines[] = {
            "1..5",
            "ok 1",
            "Bail out! database is gone",
            NULL,
        };
        feed(&t, lines);
        CHECK(t.bailed == 1);
        CHECK(tap_content_ok(&t, NULL) == 0);
    }

    /* No plan is allowed: an unplanned run of passes is ok. */
    {
        static const char *const lines[] = {
            "ok 1",
            "ok 2",
            NULL,
        };
        feed(&t, lines);
        CHECK(t.seen == 2);
        CHECK(t.passed == 2);
        CHECK(tap_content_ok(&t, &expected) == 1);
        CHECK(expected == -1);
    }

    /* Indented lines are subtests or YAML and must not be counted. */
    {
        static const char *const lines[] = {
            "1..1",
            "    ok 1 - inner",
            "    not ok 2 - inner",
            "ok 1 - outer",
            NULL,
        };
        feed(&t, lines);
        CHECK(t.seen == 1);
        CHECK(t.passed == 1);
        CHECK(tap_content_ok(&t, NULL) == 1);
    }

    /* Word boundaries: "okey" and "not okay" are not test points. */
    {
        static const char *const lines[] = {
            "1..2",
            "okey dokey then",
            "not okay here",
            "ok 1",
            "not ok 2",
            NULL,
        };
        feed(&t, lines);
        CHECK(t.seen == 2);
        CHECK(t.passed == 1);
        CHECK(t.failed == 1);
    }

    /* Bare "ok"/"not ok" with no number still count. */
    {
        static const char *const lines[] = {
            "ok",
            "not ok",
            NULL,
        };
        feed(&t, lines);
        CHECK(t.seen == 2);
        CHECK(t.passed == 1);
        CHECK(t.failed == 1);
    }

    if (failures) {
        fprintf(stderr, "tap_test: %d check(s) failed\n", failures);
        return 1;
    }
    printf("tap_test: all checks passed\n");
    return 0;
}
