/* test_rv32.c : unit tests for the embedded RV32 core
 * Copyright (c) 2026 Jon Mayo
 * SPDX-License-Identifier: 0BSD OR CC0-1.0
 *
 * Exercises rv32.c directly, without the server: a flat little-endian memory
 * bus (matching the server's vm_read/vm_write convention), a few hand-encoded
 * instructions, and the ECALL seam that the smolmoo VM dispatches syscalls
 * through.
 */

#include "test.h"
#include "rv32.h"

#include <stdint.h>
#include <string.h>

#define MEMSZ 0x10000

struct bus {
    uint8_t b[MEMSZ];
};

/* Little-endian bus callbacks, identical in convention to the server's. */

static uint32_t
r8(void *ctx, uint32_t a)
{
    struct bus *m = ctx;
    return a < MEMSZ ? m->b[a] : 0;
}

static uint32_t
r16(void *ctx, uint32_t a)
{
    struct bus *m = ctx;
    if (a + 1 < MEMSZ)
        return (uint32_t)m->b[a] | ((uint32_t)m->b[a + 1] << 8);
    return 0;
}

static uint32_t
r32(void *ctx, uint32_t a)
{
    struct bus *m = ctx;
    if (a + 3 < MEMSZ)
        return (uint32_t)m->b[a] | ((uint32_t)m->b[a + 1] << 8) |
               ((uint32_t)m->b[a + 2] << 16) | ((uint32_t)m->b[a + 3] << 24);
    return 0;
}

static void
w8(void *ctx, uint32_t a, uint32_t v)
{
    struct bus *m = ctx;
    if (a < MEMSZ)
        m->b[a] = v & 0xFF;
}

static void
w16(void *ctx, uint32_t a, uint32_t v)
{
    struct bus *m = ctx;
    if (a + 1 < MEMSZ) {
        m->b[a] = v & 0xFF;
        m->b[a + 1] = (v >> 8) & 0xFF;
    }
}

static void
w32(void *ctx, uint32_t a, uint32_t v)
{
    struct bus *m = ctx;
    if (a + 3 < MEMSZ) {
        m->b[a] = v & 0xFF;
        m->b[a + 1] = (v >> 8) & 0xFF;
        m->b[a + 2] = (v >> 16) & 0xFF;
        m->b[a + 3] = (v >> 24) & 0xFF;
    }
}

#define ENTRY 0x1000

/* Fresh memory + CPU, reset with PC parked at ENTRY. */
static void
setup(struct bus *m, rv_cpu *cpu)
{
    memset(m, 0, sizeof *m);
    rv_init(cpu, r8, r16, r32, w8, w16, w32, m);
    rv_reset(cpu, ENTRY);
}

/* Instruction encoders (little-endian words stored via w32). */

static uint32_t
enc_addi(int rd, int rs1, int imm)
{
    return ((uint32_t)(imm & 0xFFF) << 20) | ((uint32_t)rs1 << 15) |
           (0u << 12) | ((uint32_t)rd << 7) | 0x13u;
}

static uint32_t
enc_add(int rd, int rs1, int rs2)
{
    return ((uint32_t)rs2 << 20) | ((uint32_t)rs1 << 15) |
           (0u << 12) | ((uint32_t)rd << 7) | 0x33u;
}

static uint32_t
enc_sw(int rs2, int rs1, int imm)
{
    return ((uint32_t)((imm >> 5) & 0x7F) << 25) | ((uint32_t)rs2 << 20) |
           ((uint32_t)rs1 << 15) | (2u << 12) |
           ((uint32_t)(imm & 0x1F) << 7) | 0x23u;
}

static uint32_t
enc_lw(int rd, int rs1, int imm)
{
    return ((uint32_t)(imm & 0xFFF) << 20) | ((uint32_t)rs1 << 15) |
           (2u << 12) | ((uint32_t)rd << 7) | 0x03u;
}

#define ECALL 0x00000073u

static void
poke(struct bus *m, uint32_t addr, uint32_t insn)
{
    w32(m, addr, insn);
}

/* rv_reset parks PC at the entry it is handed. */
static void
test_reset(Test *t)
{
    struct bus m;
    rv_cpu cpu;

    setup(&m, &cpu);
    TAP_CHECKF(t, rv_get_pc(&cpu) == ENTRY, "PC = %08x", rv_get_pc(&cpu));
    TAP_CHECKF(t, rv_get_x(&cpu, 0) == 0, "x0 = %u", rv_get_x(&cpu, 0));
}

/* addi a0, zero, 42  ->  x10 = 42, PC advances by 4. */
static void
test_addi(Test *t)
{
    struct bus m;
    rv_cpu cpu;

    setup(&m, &cpu);
    poke(&m, ENTRY, enc_addi(10, 0, 42));
    rv_run(&cpu, 1, NULL);
    TAP_CHECKF(t, rv_get_x(&cpu, 10) == 42, "x10 = %u", rv_get_x(&cpu, 10));
    TAP_CHECKF(t, rv_get_pc(&cpu) == ENTRY + 4,
        "PC = %08x", rv_get_pc(&cpu));
}

/* addi a1, zero, -1  ->  the 12-bit immediate is sign-extended to 32. */
static void
test_addi_sign_extend(Test *t)
{
    struct bus m;
    rv_cpu cpu;

    setup(&m, &cpu);
    poke(&m, ENTRY, enc_addi(11, 0, -1));
    rv_run(&cpu, 1, NULL);
    TAP_CHECKF(t, rv_get_x(&cpu, 11) == 0xFFFFFFFFu,
        "x11 = %08x", rv_get_x(&cpu, 11));
}

/* Three instructions: 5 + 7 = 12 in x12. */
static void
test_add(Test *t)
{
    struct bus m;
    rv_cpu cpu;
    int n;

    setup(&m, &cpu);
    poke(&m, ENTRY,     enc_addi(10, 0, 5));
    poke(&m, ENTRY + 4, enc_addi(11, 0, 7));
    poke(&m, ENTRY + 8, enc_add(12, 10, 11));
    rv_run(&cpu, 3, &n);
    TAP_CHECKF(t, n == 3, "executed %d", n);
    TAP_CHECKF(t, rv_get_x(&cpu, 12) == 12, "x12 = %u", rv_get_x(&cpu, 12));
    TAP_CHECKF(t, rv_get_pc(&cpu) == ENTRY + 12,
        "PC = %08x", rv_get_pc(&cpu));
}

/* sw then lw round-trips a word, and the bus stores it little-endian. */
static void
test_load_store(Test *t)
{
    struct bus m;
    rv_cpu cpu;

    setup(&m, &cpu);
    /* x10 = 0x2A1; store at 0x40; load back into x11 */
    poke(&m, ENTRY,      enc_addi(10, 0, 0x2A1));
    poke(&m, ENTRY + 4,  enc_sw(10, 0, 0x40));
    poke(&m, ENTRY + 8,  enc_lw(11, 0, 0x40));
    rv_run(&cpu, 3, NULL);
    TAP_CHECKF(t, rv_get_x(&cpu, 11) == 0x2A1,
        "x11 = %03x", rv_get_x(&cpu, 11));
    TAP_CHECKF(t, m.b[0x40] == 0xA1 && m.b[0x41] == 0x02,
        "stored LE: %02x %02x", m.b[0x40], m.b[0x41]);
}

/* The ECALL seam: the handler sees a7 as the syscall number and a0 as the
 * first argument, its register writes are visible, and PC advances past the
 * ecall. This is how the smolmoo VM dispatches syscalls. */
struct ec_probe {
    int calls;
    uint32_t last_nr;
    uint32_t last_a0;
};

static int
ec_handler(rv_cpu *cpu, void *ctx)
{
    struct ec_probe *p = ctx;

    p->calls++;
    p->last_nr = rv_get_x(cpu, 17);     /* a7 */
    p->last_a0 = rv_get_x(cpu, 10);     /* a0 */
    rv_set_x(cpu, 10, 0xCAFE);          /* a return value the guest reads */
    return 0;                           /* handled */
}

static void
test_ecall(Test *t)
{
    struct bus m;
    rv_cpu cpu;
    struct ec_probe p = { 0, 0, 0 };

    setup(&m, &cpu);
    rv_set_ecall(&cpu, ec_handler, &p);
    poke(&m, ENTRY,     enc_addi(17, 0, 7));    /* a7 = 7 (syscall nr) */
    poke(&m, ENTRY + 4, enc_addi(10, 0, 99));   /* a0 = 99 (arg)       */
    poke(&m, ENTRY + 8, ECALL);
    rv_run(&cpu, 3, NULL);
    TAP_CHECKF(t, p.calls == 1, "handler calls = %d", p.calls);
    TAP_CHECKF(t, p.last_nr == 7, "a7 = %u", p.last_nr);
    TAP_CHECKF(t, p.last_a0 == 99, "a0 = %u", p.last_a0);
    TAP_CHECKF(t, rv_get_x(&cpu, 10) == 0xCAFE,
        "a0 (return) = %08x", rv_get_x(&cpu, 10));
    TAP_CHECKF(t, rv_get_pc(&cpu) == ENTRY + 12,
        "PC = %08x", rv_get_pc(&cpu));
}

const Case tap_cases[] = {
    { "reset", test_reset },
    { "addi", test_addi },
    { "addi_sign_extend", test_addi_sign_extend },
    { "add", test_add },
    { "load_store", test_load_store },
    { "ecall", test_ecall },
    { NULL, NULL },
};
