/* cf_user.c : Linux user-mode machine for the ColdFire emulator */

#include "coldfire.h"
#include "elf32.h"
#include "guest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************
 * Exception interception
 *
 * The CPU core takes exceptions through a vector table the way real
 * hardware does, so user mode is built rather than special-cased: the
 * table lives in a page of guest memory and every entry points at a
 * distinct address in an unmapped magic window.  When the PC lands in
 * that window the driver knows which vector fired and what to do about
 * it: service the call for TRAP #0, report a signal for the rest.
 ****************************************************************/

#define CF_MAGIC_BASE   0xFFFF0000u
#define CF_MAGIC_END    (CF_MAGIC_BASE + 64 * 4)
#define CF_VECTOR_PAGE  0x00000000u

/* Linux/m68k syscall numbers. */
#define M68K_NR_EXIT        1
#define M68K_NR_READ        3
#define M68K_NR_WRITE       4
#define M68K_NR_OPEN        5
#define M68K_NR_CLOSE       6
#define M68K_NR_LSEEK       19
#define M68K_NR_BRK         45
#define M68K_NR_WRITEV      146
#define M68K_NR_EXIT_GROUP  247

static int
sys_of_m68k(uint32_t nr)
{
    switch (nr) {
    case M68K_NR_EXIT:          return GSYS_EXIT;
    case M68K_NR_READ:          return GSYS_READ;
    case M68K_NR_WRITE:         return GSYS_WRITE;
    case M68K_NR_OPEN:          return GSYS_OPEN;
    case M68K_NR_CLOSE:         return GSYS_CLOSE;
    case M68K_NR_LSEEK:         return GSYS_LSEEK;
    case M68K_NR_BRK:           return GSYS_BRK;
    case M68K_NR_WRITEV:        return GSYS_WRITEV;
    case M68K_NR_EXIT_GROUP:    return GSYS_EXIT_GROUP;
    default:                    return GSYS_UNKNOWN;
    }
}

/****************************************************************
 * Memory bus
 ****************************************************************/

static uint32_t
bus_r8(void *ctx, uint32_t addr)
{
    return gm_load(&((guest *)ctx)->mem, addr, 1, 1);
}

static uint32_t
bus_r16(void *ctx, uint32_t addr)
{
    return gm_load(&((guest *)ctx)->mem, addr, 2, 1);
}

static uint32_t
bus_r32(void *ctx, uint32_t addr)
{
    return gm_load(&((guest *)ctx)->mem, addr, 4, 1);
}

static void
bus_w8(void *ctx, uint32_t addr, uint32_t val)
{
    gm_store(&((guest *)ctx)->mem, addr, 1, 1, val);
}

static void
bus_w16(void *ctx, uint32_t addr, uint32_t val)
{
    gm_store(&((guest *)ctx)->mem, addr, 2, 1, val);
}

static void
bus_w32(void *ctx, uint32_t addr, uint32_t val)
{
    gm_store(&((guest *)ctx)->mem, addr, 4, 1, val);
}

/****************************************************************
 * Diagnostics
 ****************************************************************/

static const char *
trace_name(int type)
{
    switch (type) {
    case CF_TR_ILLEGAL:         return "illegal instruction";
    case CF_TR_ACCESS_ERROR:    return "access error";
    case CF_TR_ADDRESS_ERROR:   return "address error";
    case CF_TR_ZERO_DIVIDE:     return "divide by zero";
    case CF_TR_PRIVILEGE:       return "privilege violation";
    case CF_TR_FORMAT_ERROR:    return "format error";
    case CF_TR_LINE_A:          return "line A";
    case CF_TR_LINE_F:          return "line F";
    case CF_TR_TRAP:            return "trap";
    case CF_TR_DOUBLE_FAULT:    return "double fault";
    default:                    return "event";
    }
}

static void
dump_trace(const cf_cpu *cpu)
{
    uint32_t n = cf_trace_count(&cpu->trace);
    uint32_t i;

    if (cf_trace_overflowed(&cpu->trace))
        fprintf(stderr, "  (earlier events dropped)\n");
    for (i = 0; i < n; i++) {
        const cf_trace_event_t *e = cf_trace_peek(&cpu->trace, i);
        fprintf(stderr, "  pc=0x%08x op=0x%04x %s%s%s\n", e->pc, e->opword,
                trace_name(e->type), e->note[0] ? ": " : "", e->note);
    }
}

static void
dump_regs(const cf_cpu *cpu)
{
    int i;

    for (i = 0; i < 8; i++)
        fprintf(stderr, "  d%d=0x%08x a%d=0x%08x\n", i, cpu->d[i], i,
                cpu->a[i]);
    fprintf(stderr, "  pc=0x%08x sr=0x%04x\n", cpu->pc, cpu->sr & 0xFFFF);
}

/** Signal number a host kernel would raise for this vector. */
static int
signal_of_vector(int vec)
{
    switch (vec) {
    case CF_VEC_ACCESS_ERROR:
    case CF_VEC_ADDRESS_ERROR:
        return 11;              /* SIGSEGV */
    case CF_VEC_ZERO_DIVIDE:
        return 8;               /* SIGFPE */
    case CF_VEC_ILLEGAL:
    case CF_VEC_PRIVILEGE:
    case CF_VEC_LINE_A:
    case CF_VEC_LINE_F:
        return 4;               /* SIGILL */
    default:
        return 6;               /* SIGABRT */
    }
}

static const char *
vector_name(int vec)
{
    switch (vec) {
    case CF_VEC_ACCESS_ERROR:   return "access error";
    case CF_VEC_ADDRESS_ERROR:  return "address error";
    case CF_VEC_ILLEGAL:        return "illegal instruction";
    case CF_VEC_ZERO_DIVIDE:    return "divide by zero";
    case CF_VEC_PRIVILEGE:      return "privilege violation";
    case CF_VEC_LINE_A:         return "unimplemented line A instruction";
    case CF_VEC_LINE_F:         return "unimplemented line F instruction";
    case CF_VEC_FORMAT_ERROR:   return "format error";
    default:                    return "unhandled exception";
    }
}

/****************************************************************
 * The run loop
 ****************************************************************/

/** Service TRAP #0 and return to the instruction after it. */
static void
do_syscall(guest *g, cf_cpu *cpu)
{
    uint32_t sp = cpu->a[7];
    uint32_t sr = gm_load(&g->mem, sp + 2, 2, 1);
    uint32_t ret = gm_load(&g->mem, sp + 4, 4, 1);
    int sys = sys_of_m68k(cpu->d[0]);
    int32_t r;

    cpu->a[7] = sp + 8;
    cpu->sr = (cpu->sr & ~0xFFFFu) | sr;
    cpu->pc = ret;

    r = guest_syscall(g, sys, cpu->d[1], cpu->d[2], cpu->d[3], cpu->d[4],
                      cpu->d[5], cpu->a[0]);
    cpu->d[0] = (uint32_t)r;
    if (g->exited)
        cpu->halted = 1;
}

int
run_coldfire(const run_opts *o, int *status)
{
    guest g;
    cf_cpu cpu;
    elf32_info info;
    uint32_t stack_base, sp, heap_base;
    uint64_t insns = 0;
    int rc = 0;
    int i;

    memset(&g, 0, sizeof(g));
    gm_init(&g.mem, o->mem_limit);
    g.arch = GUEST_ARCH_COLDFIRE;
    g.big_endian = 1;
    g.quiet = o->quiet;

    if (elf32_load(&g, o->path, &info) != 0) {
        gm_free(&g.mem);
        return -1;
    }
    if (info.machine != EM_68K) {
        fprintf(stderr, "skj-run: %s is not an m68k/ColdFire image\n", o->path);
        gm_free(&g.mem);
        return -1;
    }
    if (!info.big_endian) {
        fprintf(stderr, "skj-run: %s is little-endian; m68k images are "
                "big-endian\n", o->path);
        gm_free(&g.mem);
        return -1;
    }

    /* Heap directly above the image, then a gap, then the stack. */
    heap_base = (info.hi + GM_PAGE_SIZE - 1) & ~(GM_PAGE_SIZE - 1);
    g.brk_base = g.brk = heap_base;
    g.brk_limit = heap_base + o->heap_size;
    gm_map(&g.mem, heap_base, o->heap_size, GM_R | GM_W, "heap");

    stack_base = g.brk_limit + GM_PAGE_SIZE;
    gm_map(&g.mem, stack_base, o->stack_size, GM_R | GM_W, "stack");
    g.stack_top = stack_base + o->stack_size;
    sp = guest_build_stack(&g, o);

    /* Vector table: reset SP and PC, then every exception into the magic
     * window so the driver sees it. */
    gm_map(&g.mem, CF_VECTOR_PAGE, GM_PAGE_SIZE, GM_R, "vectors");
    g.mem.loading = 1;
    gm_store(&g.mem, CF_VECTOR_PAGE + 0, 4, 1, sp);
    gm_store(&g.mem, CF_VECTOR_PAGE + 4, 4, 1, info.entry);
    for (i = 2; i < 64; i++)
        gm_store(&g.mem, CF_VECTOR_PAGE + (uint32_t)i * 4, 4, 1,
                 CF_MAGIC_BASE + (uint32_t)i * 4);
    g.mem.loading = 0;

    cf_init(&cpu, bus_r8, bus_r16, bus_r32, bus_w8, bus_w16, bus_w32, &g);
    cf_reset(&cpu);

    while (!cpu.halted && !g.exited) {
        if (cpu.pc >= CF_MAGIC_BASE && cpu.pc < CF_MAGIC_END) {
            int vec = (int)((cpu.pc - CF_MAGIC_BASE) / 4);
            if (vec == CF_VEC_TRAP(0)) {
                do_syscall(&g, &cpu);
                continue;
            }
            fprintf(stderr, "skj-run: %s (vector %d)\n", vector_name(vec),
                    vec);
            dump_regs(&cpu);
            if (o->trace)
                dump_trace(&cpu);
            rc = 128 + signal_of_vector(vec);
            break;
        }
        if (cf_step(&cpu) < 0)
            break;
        insns++;
        if (g.mem.fault) {
            fprintf(stderr, "skj-run: bad memory %s at 0x%08x (%s), "
                    "pc=0x%08x\n", g.mem.fault_write ? "write" : "read",
                    g.mem.fault_addr, g.mem.fault_why, cpu.pc);
            if (o->trace)
                dump_trace(&cpu);
            rc = 128 + 11;
            break;
        }
        if (o->max_insns && insns >= o->max_insns) {
            fprintf(stderr, "skj-run: instruction limit (%llu) reached\n",
                    (unsigned long long)o->max_insns);
            rc = 128 + 24;      /* SIGXCPU */
            break;
        }
    }

    if (!rc && !g.exited) {
        fprintf(stderr, "skj-run: guest stopped without calling exit "
                "(pc=0x%08x)\n", cpu.pc);
        if (o->trace)
            dump_trace(&cpu);
        rc = 128 + 6;
    }

    if (o->stats)
        fprintf(stderr, "skj-run: %llu instructions, %llu syscalls, "
                "%llu KB guest memory\n", (unsigned long long)insns,
                (unsigned long long)g.syscalls,
                (unsigned long long)(g.mem.committed / 1024));

    *status = rc ? rc : g.exit_code;
    gm_free(&g.mem);
    return 0;
}
