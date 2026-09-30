/* main.c : skj-run driver — run a guest ELF on an in-tree emulator */

#include "elf32.h"
#include "guest.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each architecture is an optional component: a project that vendors only
 * one emulator builds skj-run with only that one compiled in. */
#ifndef EMU_COLDFIRE
#define EMU_COLDFIRE 1
#endif
#ifndef EMU_RV32
#define EMU_RV32 1
#endif

#define DEFAULT_MEM     (256u * 1024u * 1024u)
#define DEFAULT_STACK   (1u * 1024u * 1024u)
#define DEFAULT_HEAP    (16u * 1024u * 1024u)

static void
usage(FILE *out)
{
    fprintf(out,
        "usage: skj-run [options] program.elf [args...]\n"
        "\n"
        "Run a 32-bit guest program on the built-in emulator.  The\n"
        "architecture comes from the ELF header; --arch overrides it.\n"
        "\n"
        "options:\n"
        "  --arch NAME     guest architecture:"
#if EMU_COLDFIRE
        " coldfire"
#endif
#if EMU_RV32
        " rv32"
#endif
        "\n"
        "  --mem BYTES     cap on guest memory (default 256M)\n"
        "  --stack BYTES   guest stack size (default 1M)\n"
        "  --heap BYTES    guest heap size for brk (default 16M)\n"
        "  --max-insns N   stop after N instructions (default unlimited)\n"
        "  --total-mem BYTES  aggregate memory cap across all processes\n"
        "  --child-max-insns N  per-spawned-child instruction budget\n"
        "  --trace         dump the CPU event ring on a fault\n"
        "  --stats         report instruction and syscall counts\n"
        "  -q, --quiet     discard guest output\n"
        "  --version       print the version and exit\n"
        "  -h, --help      print this message and exit\n"
        "\n"
        "A size accepts a K, M or G suffix.  The exit status is the\n"
        "guest's; a guest killed by a fault reports 128 plus the signal\n"
        "a host kernel would have raised.\n");
}

/** Parse a size with an optional K/M/G suffix.  Returns 0 on error. */
static uint64_t
parse_size(const char *s)
{
    char *end;
    unsigned long long v = strtoull(s, &end, 0);

    if (end == s)
        return 0;
    switch (*end) {
    case 'k': case 'K': v *= 1024ull; end++; break;
    case 'm': case 'M': v *= 1024ull * 1024ull; end++; break;
    case 'g': case 'G': v *= 1024ull * 1024ull * 1024ull; end++; break;
    default: break;
    }
    if (*end)
        return 0;
    return v;
}

static int
arch_of_name(const char *name)
{
    if (strcmp(name, "coldfire") == 0 || strcmp(name, "cf") == 0 ||
        strcmp(name, "m68k") == 0)
        return GUEST_ARCH_COLDFIRE;
    if (strcmp(name, "rv32") == 0 || strcmp(name, "riscv32") == 0 ||
        strcmp(name, "riscv") == 0)
        return GUEST_ARCH_RV32;
    return GUEST_ARCH_NONE;
}

static int
arch_of_machine(uint32_t machine)
{
    switch (machine) {
    case EM_68K:    return GUEST_ARCH_COLDFIRE;
    case EM_RISCV:  return GUEST_ARCH_RV32;
    default:        return GUEST_ARCH_NONE;
    }
}

int
main(int argc, char **argv)
{
    run_opts o;
    elf32_info info;
    int arch = GUEST_ARCH_NONE;
    int status = 0;
    int i;

    memset(&o, 0, sizeof(o));
    o.mem_limit = DEFAULT_MEM;
    o.stack_size = DEFAULT_STACK;
    o.heap_size = DEFAULT_HEAP;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (a[0] != '-' || a[1] == '\0')
            break;
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(a, "--version") == 0) {
            printf("skj-run %s\n", SKJ_VERSION);
            return 0;
        }
        if (strcmp(a, "-q") == 0 || strcmp(a, "--quiet") == 0) {
            o.quiet = 1;
            continue;
        }
        if (strcmp(a, "--trace") == 0) {
            o.trace = 1;
            continue;
        }
        if (strcmp(a, "--stats") == 0) {
            o.stats = 1;
            continue;
        }
        if (strcmp(a, "--arch") == 0 && i + 1 < argc) {
            arch = arch_of_name(argv[++i]);
            if (arch == GUEST_ARCH_NONE) {
                fprintf(stderr, "skj-run: unknown architecture '%s'\n",
                        argv[i]);
                return 2;
            }
            continue;
        }
        if (strcmp(a, "--mem") == 0 && i + 1 < argc) {
            o.mem_limit = parse_size(argv[++i]);
            if (!o.mem_limit) {
                fprintf(stderr, "skj-run: bad size '%s'\n", argv[i]);
                return 2;
            }
            continue;
        }
        if (strcmp(a, "--stack") == 0 && i + 1 < argc) {
            o.stack_size = (uint32_t)parse_size(argv[++i]);
            if (!o.stack_size) {
                fprintf(stderr, "skj-run: bad size '%s'\n", argv[i]);
                return 2;
            }
            continue;
        }
        if (strcmp(a, "--heap") == 0 && i + 1 < argc) {
            o.heap_size = (uint32_t)parse_size(argv[++i]);
            if (!o.heap_size) {
                fprintf(stderr, "skj-run: bad size '%s'\n", argv[i]);
                return 2;
            }
            continue;
        }
        if (strcmp(a, "--max-insns") == 0 && i + 1 < argc) {
            o.max_insns = parse_size(argv[++i]);
            continue;
        }
        if (strcmp(a, "--total-mem") == 0 && i + 1 < argc) {
            o.total_mem = parse_size(argv[++i]);
            continue;
        }
        if (strcmp(a, "--child-max-insns") == 0 && i + 1 < argc) {
            o.child_max_insns = parse_size(argv[++i]);
            continue;
        }
        fprintf(stderr, "skj-run: unknown option '%s'\n", a);
        usage(stderr);
        return 2;
    }

    if (i >= argc) {
        usage(stderr);
        return 2;
    }

    o.path = argv[i];
    o.argv = argv + i;
    o.argc = argc - i;

    if (arch == GUEST_ARCH_NONE) {
        if (elf32_probe(o.path, &info) != 0)
            return 2;
        arch = arch_of_machine(info.machine);
        if (arch == GUEST_ARCH_NONE) {
            fprintf(stderr, "skj-run: %s targets an unsupported machine "
                    "(e_machine %u)\n", o.path, info.machine);
            return 2;
        }
    }

    switch (arch) {
#if EMU_COLDFIRE
    case GUEST_ARCH_COLDFIRE:
        if (run_coldfire(&o, &status) != 0)
            return 2;
        break;
#endif
#if EMU_RV32
    case GUEST_ARCH_RV32:
        if (run_rv32(&o, &status) != 0)
            return 2;
        break;
#endif
    default:
        fprintf(stderr, "skj-run: this build has no emulator for that "
                "architecture\n");
        return 2;
    }

    return status;
}
