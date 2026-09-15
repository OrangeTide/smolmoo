/* rv_user.c : Linux user-mode machine for the RV32 emulator
 * Copyright (c) 2026 Jon Mayo
 * SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "elf32.h"
#include "guest.h"
#include "rv32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linux/RISC-V syscall numbers (the generic table). */
#define RV_NR_CLOSE         57
#define RV_NR_LSEEK         62
#define RV_NR_READ          63
#define RV_NR_WRITE         64
#define RV_NR_WRITEV        66
#define RV_NR_EXIT          93
#define RV_NR_EXIT_GROUP    94
#define RV_NR_BRK           214

static int
sys_of_rv(uint32_t nr)
{
    switch (nr) {
    case RV_NR_CLOSE:       return GSYS_CLOSE;
    case RV_NR_LSEEK:       return GSYS_LSEEK;
    case RV_NR_READ:        return GSYS_READ;
    case RV_NR_WRITE:       return GSYS_WRITE;
    case RV_NR_WRITEV:      return GSYS_WRITEV;
    case RV_NR_EXIT:        return GSYS_EXIT;
    case RV_NR_EXIT_GROUP:  return GSYS_EXIT_GROUP;
    case RV_NR_BRK:         return GSYS_BRK;
    default:                return GSYS_UNKNOWN;
    }
}

/****************************************************************
 * Memory bus
 ****************************************************************/

static uint32_t
bus_r8(void *ctx, uint32_t addr)
{
    return gm_load(&((guest *)ctx)->mem, addr, 1, 0);
}

static uint32_t
bus_r16(void *ctx, uint32_t addr)
{
    return gm_load(&((guest *)ctx)->mem, addr, 2, 0);
}

static uint32_t
bus_r32(void *ctx, uint32_t addr)
{
    return gm_load(&((guest *)ctx)->mem, addr, 4, 0);
}

static void
bus_w8(void *ctx, uint32_t addr, uint32_t val)
{
    gm_store(&((guest *)ctx)->mem, addr, 1, 0, val);
}

static void
bus_w16(void *ctx, uint32_t addr, uint32_t val)
{
    gm_store(&((guest *)ctx)->mem, addr, 2, 0, val);
}

static void
bus_w32(void *ctx, uint32_t addr, uint32_t val)
{
    gm_store(&((guest *)ctx)->mem, addr, 4, 0, val);
}

/****************************************************************
 * ECALL
 ****************************************************************/

static int
do_ecall(rv_cpu *cpu, void *ctx)
{
    guest *g = ctx;
    int sys = sys_of_rv(cpu->x[17]);            /* a7 */
    int32_t r;

    r = guest_syscall(g, sys, cpu->x[10], cpu->x[11], cpu->x[12], cpu->x[13],
                      cpu->x[14], cpu->x[15]);
    cpu->x[10] = (uint32_t)r;
    if (g->exited)
        rv_halt(cpu);
    return 0;
}

/****************************************************************
 * Diagnostics
 ****************************************************************/

static const char *
trace_name(int type)
{
    switch (type) {
    case RV_TR_ILLEGAL:         return "illegal instruction";
    case RV_TR_FETCH_FAULT:     return "instruction fetch fault";
    case RV_TR_LOAD_FAULT:      return "load fault";
    case RV_TR_STORE_FAULT:     return "store fault";
    case RV_TR_LOAD_MISALIGN:   return "misaligned load";
    case RV_TR_STORE_MISALIGN:  return "misaligned store";
    case RV_TR_ECALL:           return "ecall";
    case RV_TR_EBREAK:          return "ebreak";
    case RV_TR_CSR:             return "csr";
    case RV_TR_TRAP:            return "trap";
    case RV_TR_DOUBLE_FAULT:    return "double fault";
    case RV_TR_INTERRUPT:       return "interrupt";
    default:                    return "event";
    }
}

static void
dump_trace(const rv_cpu *cpu)
{
    uint32_t n = rv_trace_count(&cpu->trace);
    uint32_t i;

    if (rv_trace_overflowed(&cpu->trace))
        fprintf(stderr, "  (earlier events dropped)\n");
    for (i = 0; i < n; i++) {
        const rv_trace_event_t *e = rv_trace_peek(&cpu->trace, i);
        fprintf(stderr, "  pc=0x%08x insn=0x%08x %s%s%s\n", e->pc, e->insn,
                trace_name(e->type), e->note[0] ? ": " : "", e->note);
    }
}

static const char *
cause_name(uint32_t cause)
{
    switch (cause) {
    case RV_CAUSE_INSN_MISALIGNED:      return "misaligned instruction";
    case RV_CAUSE_INSN_ACCESS_FAULT:    return "instruction access fault";
    case RV_CAUSE_ILLEGAL_INSN:         return "illegal instruction";
    case RV_CAUSE_BREAKPOINT:           return "breakpoint";
    case RV_CAUSE_LOAD_MISALIGNED:      return "misaligned load";
    case RV_CAUSE_LOAD_ACCESS_FAULT:    return "load access fault";
    case RV_CAUSE_STORE_MISALIGNED:     return "misaligned store";
    case RV_CAUSE_STORE_ACCESS_FAULT:   return "store access fault";
    default:                            return "trap";
    }
}

static int
signal_of_cause(uint32_t cause)
{
    switch (cause) {
    case RV_CAUSE_ILLEGAL_INSN:
        return 4;               /* SIGILL */
    case RV_CAUSE_BREAKPOINT:
        return 5;               /* SIGTRAP */
    default:
        return 11;              /* SIGSEGV */
    }
}

/****************************************************************
 * The run loop
 ****************************************************************/

int
run_rv32(const run_opts *o, int *status)
{
    guest g;
    rv_cpu cpu;
    elf32_info info;
    uint32_t stack_base, sp, heap_base;
    uint64_t insns = 0;
    int rc = 0;

    memset(&g, 0, sizeof(g));
    gm_init(&g.mem, o->mem_limit);
    g.arch = GUEST_ARCH_RV32;
    g.big_endian = 0;
    g.quiet = o->quiet;

    if (elf32_load(&g, o->path, &info) != 0) {
        gm_free(&g.mem);
        return -1;
    }
    if (info.machine != EM_RISCV) {
        fprintf(stderr, "skj-run: %s is not a RISC-V image\n", o->path);
        gm_free(&g.mem);
        return -1;
    }
    if (info.big_endian) {
        fprintf(stderr, "skj-run: %s is big-endian; RV32 images are "
                "little-endian\n", o->path);
        gm_free(&g.mem);
        return -1;
    }

    heap_base = (info.hi + GM_PAGE_SIZE - 1) & ~(GM_PAGE_SIZE - 1);
    g.brk_base = g.brk = heap_base;
    g.brk_limit = heap_base + o->heap_size;
    gm_map(&g.mem, heap_base, o->heap_size, GM_R | GM_W, "heap");

    stack_base = g.brk_limit + GM_PAGE_SIZE;
    gm_map(&g.mem, stack_base, o->stack_size, GM_R | GM_W, "stack");
    g.stack_top = stack_base + o->stack_size;
    sp = guest_build_stack(&g, o);

    rv_init(&cpu, bus_r8, bus_r16, bus_r32, bus_w8, bus_w16, bus_w32, &g);
    rv_reset(&cpu, info.entry);
    rv_set_ecall(&cpu, do_ecall, &g);
    cpu.zcmp = 1;
    cpu.atomics = 1;
    cpu.bitmanip = 1;
    cpu.zcb = 1;
    cpu.x[2] = sp;              /* sp */

    while (!cpu.halted && !g.exited) {
        if (rv_step(&cpu) < 0)
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
        if (cpu.in_trap && !g.exited) {
            fprintf(stderr, "skj-run: %s at 0x%08x (mtval=0x%08x)\n",
                    cause_name(cpu.mcause), cpu.mepc, cpu.mtval);
            if (o->trace)
                dump_trace(&cpu);
            rc = 128 + signal_of_cause(cpu.mcause);
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
