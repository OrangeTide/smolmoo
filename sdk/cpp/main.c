/* main.c : skj-cpp preprocessor CLI */

#include "cpp.h"
#include "util.h"
#include "version.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void
cleanup_cpp(void *p)
{
    cpp_free(p);
}

static void
usage(void)
{
    fprintf(stderr, "usage: skj-cpp [-V] [-I dir] [-D name[=value]] [-o outfile] input.c\n");
    exit(1);
}

int
main(int argc, char **argv)
{
    struct cpp *p;
    const char *inpath;
    const char *outpath;
    FILE *out;
    int rc;

    util_set_progname("skj-cpp");

    p = cpp_new();

    inpath = NULL;
    outpath = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
            cpp_add_include_path(p, argv[++i]);
        } else if (strncmp(argv[i], "-I", 2) == 0 && argv[i][2]) {
            cpp_add_include_path(p, argv[i] + 2);
        } else if (strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
            i++;
            char *eq = strchr(argv[i], '=');
            if (eq) {
                *eq = '\0';
                cpp_define(p, argv[i], eq + 1);
                *eq = '=';
            } else {
                cpp_define(p, argv[i], "1");
            }
        } else if (strncmp(argv[i], "-D", 2) == 0 && argv[i][2]) {
            char *arg = argv[i] + 2;
            char *eq = strchr(arg, '=');
            if (eq) {
                *eq = '\0';
                cpp_define(p, arg, eq + 1);
                *eq = '=';
            } else {
                cpp_define(p, arg, "1");
            }
        } else if (strcmp(argv[i], "-V") == 0) {
            printf("skj-cpp %s\n", SKJ_VERSION);
            cpp_free(p);
            return 0;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            outpath = argv[++i];
        } else if (argv[i][0] != '-') {
            inpath = argv[i];
        } else {
            usage();
        }
    }

    if (!inpath)
        usage();

    if (setjmp(util_die_env) != 0)
        return 1;
    util_die_active = 1;
    util_cleanup_push(cleanup_cpp, p);

    out = stdout;
    if (outpath) {
        out = fopen(outpath, "w");
        if (!out) {
            fprintf(stderr, "skj-cpp: cannot open '%s' for writing\n", outpath);
            util_cleanup_run();
            return 1;
        }
    }

    rc = cpp_process_file(p, inpath, out);

    if (out != stdout)
        fclose(out);
    util_cleanup_run();
    return rc < 0 ? 1 : 0;
}
