/* testmain.c : the main linked into every unit-test program
 * SPDX-License-Identifier: 0BSD OR CC0-1.0
 * Copyright © 2026 Jon Mayo
 *
 * Walks tap_cases[], runs each case, and prints one TAP point per case
 * with any failure diagnostics beneath it. Exits non-zero if any case failed.
 * Command-line arguments are case-name prefixes that filter which cases run.
 * See README.md.
 */

#include "test.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Test {
    jmp_buf escape;     /* tap_bail() jumps here */
    int failed;     /* this case recorded a failure */
    FILE   *diag;       /* memstream collecting "# ..." lines */
    char   *diagbuf;    /* backing store for diag */
    size_t  diaglen;
};

void
tap_failf(Test *t, const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    t->failed = 1;
    if (!t->diag)
        return;
    fprintf(t->diag, "# %s:%d: ", file, line);
    va_start(ap, fmt);
    vfprintf(t->diag, fmt, ap);
    va_end(ap);
    fputc('\n', t->diag);
}

void
tap_bail(Test *t)
{
    longjmp(t->escape, 1);
}

/* Program label: argv[0] basename with any "test_" prefix removed. */
static const char *
base_label(const char *argv0)
{
    const char *slash = strrchr(argv0, '/');
    const char *base = slash ? slash + 1 : argv0;

    if (strncmp(base, "test_", 5) == 0)
        base += 5;
    return base;
}

/* Non-zero when name matches any of the count prefixes, or when none given. */
static int
selected(const char *name, char **prefixes, int count)
{
    if (count == 0)
        return 1;
    for (int i = 0; i < count; i++)
        if (strncmp(name, prefixes[i], strlen(prefixes[i])) == 0)
            return 1;
    return 0;
}

/* Run one case as TAP point n and report it. Non-zero when it failed. The
 * setjmp lives here, away from main's loop locals, so tap_bail() unwinds
 * cleanly without clobbering them. */
static int
run_case(const Case *c, const char *label, int n)
{
    Test t = { 0 };
    t.diag = open_memstream(&t.diagbuf, &t.diaglen);

    if (setjmp(t.escape) == 0)
        c->run(&t);

    if (t.diag)
        fclose(t.diag);

    printf("%s %d %s/%s\n", t.failed ? "not ok" : "ok", n, label, c->name);
    if (t.diagbuf && t.diaglen)
        fputs(t.diagbuf, stdout);
    free(t.diagbuf);

    return t.failed;
}

int
main(int argc, char **argv)
{
    const char *label = base_label(argv[0]);
    char **prefixes = argv + 1;
    int nprefix = argc - 1;

    int n = 0;
    int any_failed = 0;

    for (const Case *c = tap_cases; c->name; c++) {
        if (!selected(c->name, prefixes, nprefix))
            continue;
        if (run_case(c, label, ++n))
            any_failed = 1;
    }

    printf("1..%d\n", n);
    return any_failed ? 1 : 0;
}
