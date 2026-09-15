/* test.h : unit-test API for test programs
 * SPDX-License-Identifier: 0BSD OR CC0-1.0
 * Copyright © 2026 Jon Mayo
 *
 * A unit test is a test_NAME.c file that defines a null-terminated
 * tap_cases[] table and no main. The main that walks the table lives in
 * testmain.c and is linked into every test program, so there is one copy of
 * the reporting code. Each case reports through the macros below, and the
 * program prints its results as TAP for the taptest driver to read. See
 * README.md.
 */

#ifndef TAP_TEST_H
#define TAP_TEST_H


/* Per-case context, opaque to the case. */
typedef struct Test Test;

typedef struct Case Case;
struct Case {
    const char *name;
    void (*run)(Test *t);
};

/* Every test program defines this table, NULL-terminated. */
extern const Case tap_cases[];

/* Record a failure at file:line with a printf-style message. */
void tap_failf(Test *t, const char *file, int line,
    const char *fmt, ...);

/* Abandon the current case but let the rest of the file run. */
void tap_bail(Test *t);

/* Record the failure and continue. The one to reach for. */
#define TAP_CHECK(t, x) \
    do { \
        if (!(x)) \
            tap_failf((t), __FILE__, __LINE__, "%s", #x); \
    } while (0)

/* Record with a message and continue. */
#define TAP_CHECKF(t, x, ...) \
    do { \
        if (!(x)) \
            tap_failf((t), __FILE__, __LINE__, \
                #x ": " __VA_ARGS__); \
    } while (0)

/* Record and bail out of the case: for when continuing would crash. */
#define TAP_ASSERT(t, x) \
    do { \
        if (!(x)) { \
            tap_failf((t), __FILE__, __LINE__, "%s", #x); \
            tap_bail((t)); \
        } \
    } while (0)

#endif /* TAP_TEST_H */
