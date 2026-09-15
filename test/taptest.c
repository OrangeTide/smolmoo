/* taptest.c : test driver, TAP (Test Anything Protocol) support
 * SPDX-License-Identifier: 0BSD OR CC0-1.0
 * Copyright © 2026 Jon Mayo
 *
 * Runs the test files named on the command line and reads their output as
 * TAP: a plan "1..N" and "ok"/"not ok" points, with "# SKIP"/"# TODO"
 * directives, "1..0 # SKIP" whole-file skip, and "Bail out!". A file passes
 * with no real failures, no bail out, a matching plan, and a zero exit.
 * Counts are summed and the program exits non-zero if any file failed.
 *
 * Arguments are grouped by mode: --exe marks the following paths as test
 * binaries to execute directly, --script marks them as scripts to run
 * through TAPTEST_RUNNER and diff against a paired .expected file. The build
 * owns the file lists, so there is no directory scan here.
 *
 *   make test                  # builds the tests, runs them through taptest
 *   ./taptest --exe build/test_example
 *                              # run one binary
 *   ./taptest --exe build/test_example:value_wrap
 *                              # run one case of test_example (":case" suffix)
 *   ./taptest --self-test      # exercise the TAP parser alone
 *
 * See README.md for how to write and extend tests.
 */

#include "taptest.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* Aggregate counts across every test file. */
struct Total {
    int files;
    int files_ok;
    long passed;
    long failed;
    long skipped;
    long todo;
};
typedef struct Total Total;

static const char *
version()
{
    return "0.1.0";
}

/* Copy orig without its extension; non-zero on success. */
static int
rem_ext(char *dest, size_t max, const char *orig)
{
    const char *oldext;
    oldext = strrchr(orig, '.');
    while (*orig && orig != oldext) {
        if (max <= 1)        /* reserve one byte for the terminator */
            return 0;
        max--;
        *(dest++) = *(orig++);
    }
    *dest = 0;
    return 1;
}

/* Reset a tally: no plan, empty counts. */
void
tap_reset(Tap *t)
{
    *t = (Tap) { .plan = -1 };
}

/* Plan count does not match points seen; a bail out excuses it. */
static int
tap_plan_bad(const Tap *t)
{
    return t->plan >= 0 && t->seen != t->plan && !t->bailed;
}

/* Non-zero when the TAP content alone is a pass: no failures, no bail out,
 * plan matches points seen. expected_out, if set, gets the plan (-1 if none). */
int
tap_content_ok(const Tap *t, long *expected_out)
{
    if (expected_out)
        *expected_out = t->plan;
    return !t->bailed && !tap_plan_bad(t) && t->failed == 0;
}


/* Non-zero when s begins with prefix, ignoring case. */
static int
starts_with_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

/* Advance past spaces and tabs. */
static const char *
skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

/* Return 'S' for a SKIP directive, 'T' for TODO, 0 for none. */
static int
tap_directive(const char *line)
{
    const char *h = strchr(line, '#');
    if (!h)
        return 0;
    h = skip_ws(h + 1);
    if (starts_with_ci(h, "skip"))
        return 'S';
    if (starts_with_ci(h, "todo"))
        return 'T';
    return 0;
}

/* True when c ends the "ok" / "not ok" keyword (a word boundary). */
static int
tap_boundary(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0';
}

/* Fold one output line into the tally. */
void
tap_line(Tap *t, const char *line)
{
    long a, b;

    /* Indented lines are subtests or YAML; ignore. */
    if (*line == ' ' || *line == '\t')
        return;

    if (strncmp(line, "not ok", 6) == 0 && tap_boundary(line[6])) {
        t->seen++;
        if (tap_directive(line) == 'T')
            t->todo++;  /* expected failure, not a real one */
        else
            t->failed++;
        return;
    }

    if (strncmp(line, "ok", 2) == 0 && tap_boundary(line[2])) {
        t->seen++;
        switch(tap_directive(line)) {
        case 'S': t->skipped++; break;
        case 'T': t->todo++;    break;
        default:  t->passed++;  break;
        }
        return;
    }

    if (isdigit((unsigned char)line[0]) &&
       sscanf(line, "%ld..%ld", &a, &b) == 2) {
        t->plan = b;    /* plans are "1..N"; first is always 1 */
        if (b <= 0 && tap_directive(line) == 'S')
            t->plan_skip_all = 1;
        return;
    }

    if (strncmp(line, "Bail out!", 9) == 0)
        t->bailed = 1;

    /* "TAP version", diagnostics, and YAML markers are ignored. */
}

static int verbose; /* -v: echo each test point under its file */

/* True for a line worth echoing under -v: a point or a diagnostic. */
static int
tap_echoable(const char *line)
{
    return strncmp(line, "ok", 2) == 0 ||
           strncmp(line, "not ok", 6) == 0 ||
           line[0] == '#';
}

/* Print the ", killed by signal N" or ", exit N" tail for a wait() status.
 * When want >= 0 the exit line also names the expected status; when want is
 * negative a zero exit prints nothing. */
static void
print_wait_suffix(int status, int want)
{
    if (WIFSIGNALED(status))
        printf(", killed by signal %d", WTERMSIG(status));
    else if (!WIFEXITED(status))
        return;
    else if (want >= 0)
        printf(", exit %d, expected %d", WEXITSTATUS(status), want);
    else if (WEXITSTATUS(status) != 0)
        printf(", exit %d", WEXITSTATUS(status));
}

/* Run cmd, parse its TAP output, print a verdict line, fold into totals.
 * Non-zero when the file fails. */
static int
run_command(const char *label, const char *cmd, Total *tot)
{
    Tap t;
    char line[4096];
    char *capbuf = NULL;
    size_t caplen = 0;
    FILE *cap = verbose ? open_memstream(&capbuf, &caplen) : NULL;

    tap_reset(&t);

    FILE *p = popen(cmd, "r");
    if (!p) {
        fprintf(stderr, "%-24s ERROR  cannot run (%s)\n", label,
            strerror(errno));
        tot->files++;
        if (cap)
            fclose(cap);
        free(capbuf);
        return 1;
    }
    while (fgets(line, sizeof(line), p)) {
        tap_line(&t, line);
        if (cap && tap_echoable(line))
            fprintf(cap, "    %s", line);
    }
    int status = pclose(p);

    long expected;
    int ok = tap_content_ok(&t, &expected);
    int plan_bad = tap_plan_bad(&t);
    if (status == -1 || WIFSIGNALED(status))
        ok = 0;
    /* Clean TAP but non-zero exit is dubious. */
    else if (WIFEXITED(status) && WEXITSTATUS(status) != 0 &&
        t.failed == 0 && !t.plan_skip_all)
        ok = 0;

    tot->files++;
    tot->files_ok += ok ? 1 : 0;
    tot->passed += t.passed;
    tot->failed += t.failed;
    tot->skipped += t.skipped;
    tot->todo += t.todo;

    const char *verdict = ok ? "PASS" : "FAIL";
    if (t.plan_skip_all)
        verdict = "SKIP";

    printf("%-24s %s  %ld/%ld", label, verdict, t.passed, t.seen);
    if (t.failed)
        printf(", %ld failed", t.failed);
    if (t.skipped)
        printf(", %ld skipped", t.skipped);
    if (t.todo)
        printf(", %ld todo", t.todo);
    if (t.bailed)
        printf(", bailed out");
    else if (plan_bad)
        printf(", plan expected %ld", expected);
    else
        print_wait_suffix(status, -1);
    putchar('\n');

    if (cap) {
        fclose(cap);
        if (capbuf)
            fputs(capbuf, stdout);
        free(capbuf);
    }

    return ok ? 0 : 1;
}

/* Display label for a test path: the basename with its directory and
 * extension removed and any "test_" prefix stripped. */
static void
make_label(char *dest, size_t max, const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    if (strncmp(base, "test_", 5) == 0)
        base += 5;
    rem_ext(dest, max, base);
}

/* Execute one test binary directly, optionally passing a case prefix. */
static void
run_exe(const char *path, const char *cas, Total *tot)
{
    char label[NAME_MAX + 1];
    char cmd[PATH_MAX + 256];

    make_label(label, sizeof(label), path);
    if (cas && *cas)
        snprintf(cmd, sizeof(cmd), "%s %s", path, cas);
    else
        snprintf(cmd, sizeof(cmd), "%s", path);
    run_command(label, cmd, tot);
}

/* Drain an open stream into a malloc'd buffer (caller frees) and set *len.
 * Does not close f. Non-NULL even for empty input. */
static char *
drain(FILE *f, size_t *len)
{
    char *buf = NULL;
    size_t n = 0;
    FILE *m = open_memstream(&buf, &n);
    char chunk[4096];
    size_t r;
    while ((r = fread(chunk, 1, sizeof(chunk), f)) > 0)
        fwrite(chunk, 1, r, m);
    fclose(m);

    *len = n;
    return buf;
}

/* Read a whole file into a malloc'd buffer, NULL on open failure. On success
 * *len is the byte count and the caller frees the buffer. */
static char *
slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    char *buf = drain(f, len);
    fclose(f);
    return buf; /* non-NULL even for an empty file; NULL only on fopen */
}

/* Capture a command's stdout into a malloc'd buffer (caller frees), leaving
 * its stderr attached to the terminal. Sets *len and the pclose *status. */
static char *
capture(const char *cmd, size_t *len, int *status)
{
    FILE *p = popen(cmd, "r");
    if (!p) {
        *len = 0;
        *status = -1;
        return NULL;
    }

    char *buf = drain(p, len);
    *status = pclose(p);
    return buf;
}

/* Expected exit status from a "// expect-status: N" line in the script's
 * leading comment block; 0 when there is none. */
static int
script_expected_status(const char *scriptpath)
{
    FILE *f = fopen(scriptpath, "r");
    if (!f)
        return 0;

    char line[512];
    int status = 0;
    while (fgets(line, sizeof(line), f)) {
        const char *s = skip_ws(line);
        if (*s == '\n' || *s == '\r' || *s == '\0')
            continue;       /* blank line, still in the block */
        if (s[0] != '/' || s[1] != '/')
            break;          /* first real line ends the block */
        s = skip_ws(s + 2);
        if (strncmp(s, "expect-status:", 14) == 0) {
            status = (int)strtol(s + 14, NULL, 10);
            break;
        }
    }

    fclose(f);
    return status;
}

/* Run one script, compare its stdout and exit status against the paired
 * .expected file, and fold the result into totals. In update mode the
 * .expected is rewritten from the output instead of compared. Non-zero when
 * the script fails. */
static int
run_script(const char *label, const char *cmd, const char *scriptpath,
    const char *exppath, int update, Total *tot)
{
    size_t outlen;
    int status;
    char *out = capture(cmd, &outlen, &status);

    tot->files++;
    if (!out) {
        fprintf(stderr, "%-24s ERROR  cannot run (%s)\n", label,
            strerror(errno));
        tot->failed++;
        return 1;
    }

    if (update) {
        FILE *f = fopen(exppath, "wb");
        if (f) {
            fwrite(out, 1, outlen, f);
            fclose(f);
        }
        printf("%-24s WROTE  %s\n", label, exppath);
        tot->files_ok++;
        tot->passed++;
        free(out);
        return 0;
    }

    size_t explen = 0;
    char *exp = slurp(exppath, &explen);
    int want = script_expected_status(scriptpath);

    int diff_ok = exp && explen == outlen &&
              (outlen == 0 || memcmp(exp, out, outlen) == 0);
    int st_ok = WIFEXITED(status) && WEXITSTATUS(status) == want;
    int ok = exp && diff_ok && st_ok;

    tot->files_ok += ok ? 1 : 0;
    if (ok)
        tot->passed++;
    else
        tot->failed++;

    printf("%-24s %s  1/1", label, ok ? "PASS" : "FAIL");
    if (!exp)
        printf(", no %s", exppath);
    else if (!diff_ok)
        printf(", stdout differs");
    if (!st_ok)
        print_wait_suffix(status, want);
    putchar('\n');

    if (verbose && exp && !diff_ok) {
        size_t i = 0;
        while (i < outlen && i < explen && out[i] == exp[i])
            i++;
        printf("    stdout differs at byte %zu (%zu bytes, expected %zu)\n",
               i, outlen, explen);
    }

    free(out);
    free(exp);
    return ok ? 0 : 1;
}

/* Run one script through runner, deriving its .expected path from the script
 * path (the extension replaced with ".expected"). An empty runner runs the
 * script path directly. */
static void
run_script_file(const char *runner, const char *path, int update, Total *tot)
{
    char label[NAME_MAX + 1];
    char exppath[PATH_MAX];
    char cmd[PATH_MAX + 256];
    size_t used;

    make_label(label, sizeof(label), path);
    rem_ext(exppath, sizeof(exppath), path);    /* drop the extension */
    used = strlen(exppath);
    snprintf(exppath + used, sizeof(exppath) - used, ".expected");
    if (runner && *runner)
        snprintf(cmd, sizeof(cmd), "%s %s", runner, path);
    else
        snprintf(cmd, sizeof(cmd), "%s", path);
    run_script(label, cmd, path, exppath, update, tot);
}

static void
usage(FILE *f, const char *argv0)
{
    fprintf(f,
        "usage: %s [options] [--exe bin...] [--script file...]\n"
        "  Runs the named test files and parses their output as TAP.\n"
        "  --exe                 following paths are test binaries to run\n"
        "                        (a \":case\" suffix runs one case)\n"
        "  --script              following paths are scripts, diffed\n"
        "                        against a paired .expected file\n"
        "  -v                    echo each test point, not just the verdict\n"
        "  --version             print version and exit\n"
        "  --self-test           exercise the TAP parser\n"
        "  --help                print this message and exit\n"
        "environment:\n"
        "  TAPTEST_RUNNER        runner for scripts (default: run the script"
        " directly)\n"
        "  TAPTEST_UPDATE        rewrite each .expected from actual output\n",
        argv0);
}

/* One test file to run: a binary (is_script 0) or a script (is_script 1). */
struct Job {
    int is_script;
    char *path;
    char *cas;  /* case prefix for a binary, else NULL */
};
typedef struct Job Job;

int
main(int argc, char **argv)
{
    int do_tap_test = 0;
    int mode_script = 0;    /* current group: 0 = --exe, 1 = --script */
    Job jobs[256];
    int njob = 0;

    for (int i = 1; i < argc; i++) {
        char *a = argv[i];

        if (strcmp(a, "--version") == 0) {
            printf("taptest %s\n", version());
            return 0;
        } else if (strcmp(a, "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(a, "--self-test") == 0) {
            do_tap_test = 1;
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(a, "--exe") == 0) {
            mode_script = 0;
        } else if (strcmp(a, "--script") == 0) {
            mode_script = 1;
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], a);
            usage(stderr, argv[0]);
            return 2;
        } else {
            int max = (int)(sizeof(jobs) / sizeof(jobs[0]));
            if (njob >= max) {
                fprintf(stderr, "%s: too many test files (max %d)\n",
                    argv[0], max);
                return 2;
            }
            jobs[njob].is_script = mode_script;
            jobs[njob].path = a;
            jobs[njob].cas = NULL;
            if (!mode_script) {  /* split off a ":case" suffix for binaries */
                char *colon = strrchr(a, ':');
                if (colon) {
                    *colon = '\0';
                    jobs[njob].cas = colon + 1;
                }
            }
            njob++;
        }
    }

    /* Self-test first, so its verdict precedes any file runs. */
    if (do_tap_test && tap_test())
        return 1;

    if (njob == 0) {
        if (do_tap_test)     /* bare --self-test is the whole job */
            return 0;
        fprintf(stderr, "%s: no test files given\n", argv[0]);
        usage(stderr, argv[0]);
        return 2;
    }

    /* Runner for scripts; empty runs the script directly. Override it for
     * an interpreter, or for out-of-tree and cross builds. */
    const char *runner = getenv("TAPTEST_RUNNER");
    if (!runner)
        runner = "";
    int update = getenv("TAPTEST_UPDATE") != NULL;

    Total tot = { 0 };
    for (int i = 0; i < njob; i++) {
        if (jobs[i].is_script)
            run_script_file(runner, jobs[i].path, update, &tot);
        else
            run_exe(jobs[i].path, jobs[i].cas, &tot);
    }

    printf("--------\n"
           "%d files, %d passed, %d failed; "
           "%ld ok, %ld failed, %ld skipped, %ld todo\n",
           tot.files, tot.files_ok, tot.files - tot.files_ok,
           tot.passed, tot.failed, tot.skipped, tot.todo);

    return tot.files_ok == tot.files ? 0 : 1;
}
