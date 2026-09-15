/* rv_emit.c : RISC-V (RV32IMFD + Zfh) back-end, emits GAS-syntax assembly
 * Copyright (c) 2026 Jon Mayo
 * SPDX-License-Identifier: 0BSD OR CC0-1.0 */
/*
 * Stack-based calling convention (matches ColdFire backend pattern):
 *   - Args pushed right-to-left on the stack, 32 bits each; caller pops.
 *   - Return value in a0.
 *   - Callee-save: s0..s11, ra.
 *   - Scratch: t0..t6, a0..a7.
 *
 * Frame layout (after prologue, matches ColdFire convention):
 *
 *      s0 + 8 + 4*i    param i (pushed by caller before jal)
 *      s0 + 4           saved ra
 *      s0 + 0           saved old s0
 *      s0 - locals_size bottom of locals
 *      below locals     spill slots
 *      sp + 0..43       saved s1..s11 (11 words)
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#define NSAVED 11

/*
 * Register name table.  Indices 0-1 are scratch (t0, t1).
 * Indices 2-12 are allocatable callee-save (s1-s11), matching
 * FIRST_REG=2, NUM_REGS=11 in regalloc_rv.c.
 */
static const char *regs[] = {
    "t0", "t1",
    "s1", "s2", "s3", "s4", "s5", "s6",
    "s7", "s8", "s9", "s10", "s11",
};

/*
 * Float register name table (RV32FD).  Indices 0-1 are scratch (ft0, ft1).
 * Indices 2-13 are allocatable callee-save (fs0-fs11), matching
 * FP_FIRST_REG=2, FP_NUM_REGS=12 in regalloc_rv.c.  A RISC-V float register
 * has one name for both widths; single vs double is the instruction suffix
 * (.s / .d), not a separate register view.
 */
static const char *fregs[] = {
    "ft0", "ft1",
    "fs0", "fs1", "fs2", "fs3", "fs4", "fs5",
    "fs6", "fs7", "fs8", "fs9", "fs10", "fs11",
};

/* allocatable float index range in fregs[] (fs0..fs11) */
#define FP_ALLOC_FIRST 2
#define FP_ALLOC_LAST  13

#ifdef CC_PSABI
/****************************************************************
 * The RISC-V ILP32 calling convention
 *
 * This is the platform ABI, so a function compiled here and one
 * compiled by gcc -mabi=ilp32 can call each other.  Without it the
 * backend uses the toolkit's own stack convention, which m68k's C ABI
 * happens to match and RISC-V's does not, so a gcc-built runtime is
 * unreachable there (doc/emulator.md).
 *
 * The rules, as gcc emits them:
 *
 *   - A one-word argument takes the next a-register, a0 through a7.
 *   - A two-word one (long long, and a double, since the soft-float ABI
 *     passes floating point in the integer registers) takes the next
 *     two, low word first.  The pair is NOT aligned to an even
 *     register, unlike ARM's EABI.
 *   - With exactly one register left, a two-word argument splits: the
 *     low word rides it and the high word goes to the stack.
 *   - What is left goes in the outgoing block, a two-word argument
 *     8-aligned within it.
 *   - A double crosses through memory, since RV32 has no instruction
 *     moving the halves of an f-register to integer registers.  A
 *     single crosses in one register through fmv.x.w.
 *   - Results come back in a0, or a0 and a1 for two words.
 *
 * a0-a7 are neither allocated nor used as scratch (only t0 and t1 are),
 * so argument registers can be written in any order with no hazard.
 ****************************************************************/

static const char *argreg[] = {
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
};

#define NARGREG 8

/* Where one argument or parameter lives. */
struct rv_ploc {
    int words;      /* 1 or 2 (a larger aggregate rides the stack whole) */
    int ireg;       /* first a-register index, -1 if none */
    int nreg;       /* registers used: 0, 1 (split low word), or words */
    int soff;       /* byte offset of the stack part within the block */
};

/*
 * Assign homes to a sequence of arguments given their widths in words.
 * Returns the size of the outgoing stack block.  Both the caller and the
 * callee run this over the same sequence, which is what makes the two
 * sides agree.
 */
static int
rv_abi_assign(const int *words, int n, struct rv_ploc *loc)
{
    int ix = 0, soff = 0, k;

    for (k = 0; k < n; k++) {
        int w = words[k];

        loc[k].words = w;
        loc[k].ireg = -1;
        loc[k].nreg = 0;
        loc[k].soff = 0;

        if (w == 1) {
            if (ix < NARGREG) {
                loc[k].ireg = ix++;
                loc[k].nreg = 1;
            } else {
                loc[k].soff = soff;
                soff += 4;
            }
        } else if (w == 2) {
            if (ix + 1 < NARGREG) {
                loc[k].ireg = ix;
                loc[k].nreg = 2;
                ix += 2;
            } else if (ix < NARGREG) {
                /* split: the low word takes the last register, the high
                   word opens the stack block */
                loc[k].ireg = ix;
                loc[k].nreg = 1;
                ix = NARGREG;
                loc[k].soff = soff;
                soff += 4;
            } else {
                soff = (soff + 7) & ~7;
                loc[k].soff = soff;
                soff += 8;
            }
        } else {
            /* an aggregate wider than two words: stack, 8-aligned */
            ix = NARGREG;
            soff = (soff + 7) & ~7;
            loc[k].soff = soff;
            soff += w * 4;
        }
    }
    return soff;
}

/* Width in words of parameter `slot`, from its slot size. */
static int
rv_param_words(struct ir_func *fn, int slot)
{
    int sz = (fn->slot_size[slot] + 3) & ~3;

    /* A single-precision float is one ABI word even though its local slot is
       widened to 8 bytes; the caller passes it in one integer register, so
       the callee must count it the same way (see param_fw). */
    if (fn->param_fw && fn->param_fw[slot])
        return 1;
    return sz / 4 < 1 ? 1 : sz / 4;
}

/*
 * Home of parameter `slot` on the callee side, plus the byte offset of
 * its home slot in the frame when it arrives in registers.  Homes are
 * laid out below s0 in parameter order, each 8-aligned when two words
 * wide, so a register parameter is spilled once in the prologue and read
 * from its home by the ordinary slot-based lowering afterwards.
 */
static void
rv_param(struct ir_func *fn, int slot, struct rv_ploc *out, int *home)
{
    struct rv_ploc loc[64];
    int words[64] = { 0 };
    int n = fn->nparams;
    int i, h = 0;

    if (n > 64)
        n = 64;
    for (i = 0; i < n; i++)
        words[i] = rv_param_words(fn, i);
    rv_abi_assign(words, n, loc);

    for (i = 0; i < n; i++) {
        int sz = words[i] * 4;
        if (loc[i].nreg == 0)
            continue;
        if (sz % 8 == 0)
            h = (h + 7) & ~7;
        h += sz;
        if (i == slot) {
            *out = loc[i];
            *home = -h;
            return;
        }
    }
    *out = loc[slot < n ? slot : 0];
    *home = 0;
}

/* Bytes reserved below s0 for register-parameter homes. */
static int
param_home(struct ir_func *fn)
{
    struct rv_ploc p;
    int i, h = 0, home;

    for (i = 0; i < fn->nparams; i++) {
        rv_param(fn, i, &p, &home);
        if (p.nreg == 0)
            continue;
        if (-home > h)
            h = -home;
    }
    return (h + 7) & ~7;
}
#endif /* CC_PSABI */

/* Frame bytes reserved above the locals (0 under the stack convention). */
static int
frame_reserve(struct ir_func *fn)
{
#ifdef CC_PSABI
    return param_home(fn);
#else
    (void)fn;
    return 0;
#endif
}

/****************************************************************
 * Frame layout helpers
 ****************************************************************/

/*
 * 8-byte alignment.  Every function is entered with a 16-aligned sp (the
 * prologue rounds its own frame and the caller rounds the arg push), so
 * s0 = incoming_sp - 8 is 8-aligned.  On that anchor, any slot whose size is
 * a multiple of 8 (a double, a long long, or an array of them) is placed at
 * an 8-aligned offset, so an fld/fsd never lands on a 4-aligned address.
 * 4-byte slots stay 4-granular.
 */
static int
slot_bytes(struct ir_func *fn, int i)
{
    return (fn->slot_size[i] + 3) & ~3;
}

static int
locals_size(struct ir_func *fn)
{
    int i, x;

    x = 0;
    for (i = fn->nparams; i < fn->nslots; i++) {
        int sz = slot_bytes(fn, i);
        if (sz % 8 == 0)
            x = (x + 7) & ~7;
        x += sz;
    }
    return (x + 7) & ~7;   /* keep the spill areas below 8-aligned */
}

static int
slot_offset(struct ir_func *fn, int slot)
{
    int i, off;

    if (slot < fn->nparams) {
#ifdef CC_PSABI
        /* a register parameter reads from its home below s0, one the
           caller left on the stack from the incoming block: s0 is the
           entry sp minus 8, so the block starts at s0+8 */
        struct rv_ploc p;
        int home;

        rv_param(fn, slot, &p, &home);
        if (p.nreg > 0)
            return home;
        return 8 + p.soff;
#else
        /* params sit above the frame from s0+8 upward */
        off = 8;
        for (i = 0; ; i++) {
            int sz = slot_bytes(fn, i);
            if (sz % 8 == 0)
                off = (off + 7) & ~7;
            if (i == slot)
                return off;
            off += sz;
        }
#endif
    }
    /* locals grow downward from s0, below the parameter homes */
    off = 0;
    for (i = fn->nparams; i <= slot; i++) {
        int sz = slot_bytes(fn, i);
        if (sz % 8 == 0)
            off = (off + 7) & ~7;
        off += sz;
    }
    return -frame_reserve(fn) - off;
}

static int
frame_size(struct ir_func *fn)
{
    return frame_reserve(fn) + locals_size(fn) + fn->nfspills * 8
           + fn->nspills * 4
           + fn->ni64spills * 8;
}

/* float spills sit directly below the (8-aligned) locals, so a spilled
   double reload (fld) stays 8-aligned regardless of the int spill count */
static int
fspill_byte_offset(struct ir_func *fn, int temp)
{
    return -frame_reserve(fn) - locals_size(fn) - (fn->temp_spill[temp] + 8);
}

static int
spill_byte_offset(struct ir_func *fn, int temp)
{
    return -frame_reserve(fn) - locals_size(fn) - fn->nfspills * 8
           - (fn->temp_spill[temp] + 4);
}

/* i64 spills are read/written as lw/sw pairs, so 4-byte alignment suffices */
static int
i64spill_byte_offset(struct ir_func *fn, int temp)
{
    return -frame_reserve(fn) - locals_size(fn) - fn->nfspills * 8
           - fn->nspills * 4
           - (fn->temp_spill[temp] + 8);
}

/****************************************************************
 * Base-relative load/store with large-offset lowering
 *
 * A 12-bit signed immediate reaches only -2048..+2047 from the base.  A
 * frame larger than that (a function with big locals) puts spills, slot
 * accesses, and the callee-save reloads past that range, where the bare
 * `op reg, off(base)` would be unencodable.  This routes every
 * frame-relative access through one place: in range it emits the direct
 * form, out of range it materialises base+off in t6 and uses 0(t6).
 *
 * t6 is safe as the scratch: it is not in regs[] so the allocator never
 * assigns it, and no other emission uses it, so it holds nothing live
 * across the three instructions.  reg is always a saved, scratch, or float
 * register, never t6.
 ****************************************************************/

#define IMM12_MIN (-2048)
#define IMM12_MAX 2047

static void
emit_mem(FILE *out, const char *op, const char *reg, int off, const char *base)
{
    if (off >= IMM12_MIN && off <= IMM12_MAX) {
        fprintf(out, "\t%s %s, %d(%s)\n", op, reg, off, base);
        return;
    }
    fprintf(out, "\tli t6, %d\n", off);
    fprintf(out, "\tadd t6, %s, t6\n", base);
    fprintf(out, "\t%s %s, 0(t6)\n", op, reg);
}

/* Emit `addi dst, base, imm`, going through t6 when imm is out of range.
   dst may equal base (an sp or s0 adjustment); dst must not be t6. */
static void
emit_addbig(FILE *out, const char *dst, const char *base, int imm)
{
    if (imm >= IMM12_MIN && imm <= IMM12_MAX) {
        fprintf(out, "\taddi %s, %s, %d\n", dst, base, imm);
        return;
    }
    fprintf(out, "\tli t6, %d\n", imm);
    fprintf(out, "\tadd %s, %s, t6\n", dst, base);
}

/****************************************************************
 * Float temp -> register materialisation
 *
 * f32 selects the reload/store width (flw/fsw vs fld/fsd); the register
 * name is width-independent.  A spilled F32 lives in the low 4 bytes of
 * its 8-byte slot.
 ****************************************************************/

static const char *
frs_w(FILE *out, struct ir_func *fn, int t, int scratch, int f32)
{
    int r = fn->temp_reg[t];

    if (r >= 0)
        return fregs[r];
    emit_mem(out, f32 ? "flw" : "fld", fregs[scratch],
        fspill_byte_offset(fn, t), "s0");
    return fregs[scratch];
}

static const char *
frd_w(struct ir_func *fn, int t, int scratch)
{
    int r = fn->temp_reg[t];

    return r >= 0 ? fregs[r] : fregs[scratch];
}

static void
fwd_f(FILE *out, struct ir_func *fn, int t, const char *reg, int f32)
{
    if (fn->temp_reg[t] >= 0)
        return;
    emit_mem(out, f32 ? "fsw" : "fsd", reg, fspill_byte_offset(fn, t), "s0");
}

/****************************************************************
 * Temp -> register materialisation
 ****************************************************************/

static const char *
rs(FILE *out, struct ir_func *fn, int t, int scratch)
{
    int r = fn->temp_reg[t];

    if (r >= 0)
        return regs[r];
    emit_mem(out, "lw", regs[scratch], spill_byte_offset(fn, t), "s0");
    return regs[scratch];
}

static const char *
rd(struct ir_func *fn, int t, int scratch)
{
    int r = fn->temp_reg[t];

    if (r >= 0)
        return regs[r];
    return regs[scratch];
}

static void
wd(FILE *out, struct ir_func *fn, int t, const char *reg)
{
    if (fn->temp_reg[t] >= 0)
        return;
    emit_mem(out, "sw", reg, spill_byte_offset(fn, t), "s0");
}

/****************************************************************
 * I64 register-pair helpers
 *
 * Pair layout (allocated from top of s1..s11):
 *   pair 0: lo=s10 (idx 11), hi=s11 (idx 12)
 *   pair 1: lo=s8  (idx 9),  hi=s9  (idx 10)
 *   pair 2: lo=s6  (idx 7),  hi=s7  (idx 8)
 *   pair 3: lo=s4  (idx 5),  hi=s5  (idx 6)
 ****************************************************************/

static int i64_lo_idx(int pair) { return 11 - 2 * pair; }
static int i64_hi_idx(int pair) { return 12 - 2 * pair; }

static const char *
i64_rs_lo(FILE *out, struct ir_func *fn, int t, int scratch)
{
    int pair = fn->temp_reg[t];
    if (pair >= 0)
        return regs[i64_lo_idx(pair)];
    emit_mem(out, "lw", regs[scratch], i64spill_byte_offset(fn, t), "s0");
    return regs[scratch];
}

static const char *
i64_rs_hi(FILE *out, struct ir_func *fn, int t, int scratch)
{
    int pair = fn->temp_reg[t];
    if (pair >= 0)
        return regs[i64_hi_idx(pair)];
    emit_mem(out, "lw", regs[scratch], i64spill_byte_offset(fn, t) + 4, "s0");
    return regs[scratch];
}

static const char *
i64_rd_lo(struct ir_func *fn, int t, int scratch)
{
    int pair = fn->temp_reg[t];
    if (pair >= 0)
        return regs[i64_lo_idx(pair)];
    return regs[scratch];
}

static const char *
i64_rd_hi(struct ir_func *fn, int t, int scratch)
{
    int pair = fn->temp_reg[t];
    if (pair >= 0)
        return regs[i64_hi_idx(pair)];
    return regs[scratch];
}

static void
i64_wd_lo(FILE *out, struct ir_func *fn, int t, const char *reg)
{
    if (fn->temp_reg[t] >= 0)
        return;
    emit_mem(out, "sw", reg, i64spill_byte_offset(fn, t), "s0");
}

static void
i64_wd_hi(FILE *out, struct ir_func *fn, int t, const char *reg)
{
    if (fn->temp_reg[t] >= 0)
        return;
    emit_mem(out, "sw", reg, i64spill_byte_offset(fn, t) + 4, "s0");
}

/****************************************************************
 * Binary / unary / compare helpers
 ****************************************************************/

static void
emit_binop(FILE *out, struct ir_func *fn, struct ir_insn *i,
           const char *mnem)
{
    const char *sa, *sb, *sd;

    sa = rs(out, fn, i->a, 0);
    sb = rs(out, fn, i->b, 1);
    sd = rd(fn, i->dst, 0);
    fprintf(out, "\t%s %s, %s, %s\n", mnem, sd, sa, sb);
    wd(out, fn, i->dst, sd);
}

static void
emit_unop(FILE *out, struct ir_func *fn, struct ir_insn *i,
          const char *mnem)
{
    const char *sa, *sd;

    sa = rs(out, fn, i->a, 0);
    sd = rd(fn, i->dst, 0);
    fprintf(out, "\t%s %s, %s\n", mnem, sd, sa);
    wd(out, fn, i->dst, sd);
}

static void
emit_cmp(FILE *out, struct ir_func *fn, struct ir_insn *i,
         int op)
{
    const char *sa, *sb, *sd;

    sa = rs(out, fn, i->a, 0);
    sb = rs(out, fn, i->b, 1);
    sd = rd(fn, i->dst, 0);

    switch (op) {
    case IR_CMPEQ:
        fprintf(out, "\txor %s, %s, %s\n", sd, sa, sb);
        fprintf(out, "\tseqz %s, %s\n", sd, sd);
        break;
    case IR_CMPNE:
        fprintf(out, "\txor %s, %s, %s\n", sd, sa, sb);
        fprintf(out, "\tsnez %s, %s\n", sd, sd);
        break;
    case IR_CMPLTS:
        fprintf(out, "\tslt %s, %s, %s\n", sd, sa, sb);
        break;
    case IR_CMPLES:
        fprintf(out, "\tslt %s, %s, %s\n", sd, sb, sa);
        fprintf(out, "\txori %s, %s, 1\n", sd, sd);
        break;
    case IR_CMPGTS:
        fprintf(out, "\tslt %s, %s, %s\n", sd, sb, sa);
        break;
    case IR_CMPGES:
        fprintf(out, "\tslt %s, %s, %s\n", sd, sa, sb);
        fprintf(out, "\txori %s, %s, 1\n", sd, sd);
        break;
    case IR_CMPLTU:
        fprintf(out, "\tsltu %s, %s, %s\n", sd, sa, sb);
        break;
    case IR_CMPLEU:
        fprintf(out, "\tsltu %s, %s, %s\n", sd, sb, sa);
        fprintf(out, "\txori %s, %s, 1\n", sd, sd);
        break;
    case IR_CMPGTU:
        fprintf(out, "\tsltu %s, %s, %s\n", sd, sb, sa);
        break;
    case IR_CMPGEU:
        fprintf(out, "\tsltu %s, %s, %s\n", sd, sa, sb);
        fprintf(out, "\txori %s, %s, 1\n", sd, sd);
        break;
    }

    wd(out, fn, i->dst, sd);
}

static void
emit_fbinop(FILE *out, struct ir_func *fn, struct ir_insn *i,
            const char *base)
{
    int f32 = i->imm == FWIDTH_F32;
    const char *sfx = f32 ? "s" : "d";
    const char *sa, *sb, *sd;

    sa = frs_w(out, fn, i->a, 0, f32);
    sb = frs_w(out, fn, i->b, 1, f32);
    sd = frd_w(fn, i->dst, 0);
    fprintf(out, "\t%s.%s %s, %s, %s\n", base, sfx, sd, sa, sb);
    fwd_f(out, fn, i->dst, sd, f32);
}

static void
emit_funop(FILE *out, struct ir_func *fn, struct ir_insn *i,
           const char *base)
{
    int f32 = i->imm == FWIDTH_F32;
    const char *sfx = f32 ? "s" : "d";
    const char *sa, *sd;

    sa = frs_w(out, fn, i->a, 0, f32);
    sd = frd_w(fn, i->dst, 0);
    fprintf(out, "\t%s.%s %s, %s\n", base, sfx, sd, sa);
    fwd_f(out, fn, i->dst, sd, f32);
}

/****************************************************************
 * Per-instruction emission
 ****************************************************************/

#define RET_INT   0
#define RET_I64   1
#define RET_FLOAT 2

/* Maximum arguments in one call. Mirrors the C front end's cap (lower.c:
   "too many arguments"); the two must agree, or a call the front end accepts
   would be rejected here. */
#define RV_MAX_ARGS 32

static int arg_temps[RV_MAX_ARGS];
static int arg_is_i64[RV_MAX_ARGS];
static int arg_is_float[RV_MAX_ARGS];
static int arg_fw[RV_MAX_ARGS];   /* per-arg float width: 1 = F32, 0 = F64 */
static int narg;
static int label_prefix;
static int i64cmp_serial;

static void
emit_load(FILE *out, struct ir_func *fn, struct ir_insn *i,
          const char *mnem)
{
    const char *sa, *sd;

    sa = rs(out, fn, i->a, 0);
    sd = rd(fn, i->dst, 0);
    fprintf(out, "\t%s %s, 0(%s)\n", mnem, sd, sa);
    wd(out, fn, i->dst, sd);
}

static void
emit_store(FILE *out, struct ir_func *fn, struct ir_insn *i,
           const char *mnem)
{
    const char *sa, *sb;

    sa = rs(out, fn, i->a, 0);
    sb = rs(out, fn, i->b, 1);
    fprintf(out, "\t%s %s, 0(%s)\n", mnem, sb, sa);
}

/*
 * Callee-saved float registers actually used by this function.  Only these
 * are saved/restored, so a pure-integer function pays nothing for the float
 * class.  used_fregs[] holds their fregs[] indices (2..13), in order.
 */
static int used_fregs[12];
static int n_used_fregs;

static void
compute_used_fregs(struct ir_func *fn)
{
    struct ir_insn *i;
    int seen[14] = {0};
    int r;

    n_used_fregs = 0;
    for (i = fn->head; i; i = i->next) {
        if (ir_op_is_float_def(i->op) && i->dst >= 0 &&
            i->dst < fn->ntemps) {
            r = fn->temp_reg[i->dst];
            if (r >= FP_ALLOC_FIRST && r <= FP_ALLOC_LAST)
                seen[r] = 1;
        }
    }
    for (r = FP_ALLOC_FIRST; r <= FP_ALLOC_LAST; r++)
        if (seen[r])
            used_fregs[n_used_fregs++] = r;
}

static void
emit_prologue(FILE *out, struct ir_func *fn)
{
    int frame = frame_size(fn);
    int fsaved = n_used_fregs * 8;
    int base = NSAVED * 4 + fsaved + frame;
    /* round the whole frame to 16 so sp stays 16-aligned across calls; the
       padding is dead space at the bottom, leaving every s0-relative offset
       (and the epilogue) unchanged. */
    int total = (8 + base + 15) & ~15;
    int pad = total - (8 + base);
    int k;

    emit_addbig(out, "sp", "sp", -total);
    emit_mem(out, "sw", "ra", total - 4, "sp");
    emit_mem(out, "sw", "s0", total - 8, "sp");
    emit_addbig(out, "s0", "sp", total - 8);
    for (k = 0; k < NSAVED; k++)
        fprintf(out, "\tsw %s, %d(sp)\n", regs[k + 2], pad + k * 4);
    for (k = 0; k < n_used_fregs; k++)
        fprintf(out, "\tfsd %s, %d(sp)\n",
            fregs[used_fregs[k]], pad + NSAVED * 4 + k * 8);

#ifdef CC_PSABI
    /* Spill the register parameters into their homes, so every
       parameter is read from a slot afterwards and the rest of the
       lowering does not know the difference.  A split parameter's high
       word is copied down from the caller's block into the same home,
       which makes it contiguous like the others. */
    for (k = 0; k < fn->nparams; k++) {
        struct rv_ploc p;
        int home;

        rv_param(fn, k, &p, &home);
        if (p.nreg == 0)
            continue;
        fprintf(out, "\tsw %s, %d(s0)\n", argreg[p.ireg], home);
        if (p.nreg == 2)
            fprintf(out, "\tsw %s, %d(s0)\n", argreg[p.ireg + 1], home + 4);
        else if (p.words == 2) {
            fprintf(out, "\tlw t0, %d(s0)\n", 8 + p.soff);
            fprintf(out, "\tsw t0, %d(s0)\n", home + 4);
        }
    }
#endif
}

static void
emit_epilogue_no_ret(FILE *out, struct ir_func *fn)
{
    int frame = frame_size(fn);
    int fsaved = n_used_fregs * 8;
    int int_base = -(NSAVED * 4 + fsaved + frame);
    int fp_base = -(fsaved + frame);
    int k;

    for (k = 0; k < NSAVED; k++)
        emit_mem(out, "lw", regs[k + 2], int_base + k * 4, "s0");
    for (k = 0; k < n_used_fregs; k++)
        emit_mem(out, "fld", fregs[used_fregs[k]], fp_base + k * 8, "s0");
    fprintf(out, "\tlw ra, 4(s0)\n");
    fprintf(out, "\tlw t0, 0(s0)\n");
    fprintf(out, "\taddi sp, s0, 8\n");
    fprintf(out, "\tmv s0, t0\n");
}

static void
emit_epilogue(FILE *out, struct ir_func *fn)
{
    emit_epilogue_no_ret(out, fn);
    fprintf(out, "\tret\n");
}

#ifdef CC_PSABI
/*
 * Set up the arguments in a0-a7 and the outgoing block, call, and take
 * the result.  A double crosses in two integer registers and so has to
 * pass through memory: `stage` is 8 bytes at the top of the block kept
 * for that, and for a double result on the way back.
 */
static void
emit_call_flush(FILE *out, struct ir_func *fn, struct ir_insn *i,
                int indirect, int retkind)
{
    struct rv_ploc loc[RV_MAX_ARGS];
    int words[RV_MAX_ARGS] = { 0 };
    int k, push_bytes, stack_bytes, stage, need_stage;

    for (k = 0; k < narg; k++) {
        if (arg_is_i64[k])
            words[k] = 2;
        else if (arg_is_float[k])
            words[k] = arg_fw[k] ? 1 : 2;    /* a single rides one word */
        else
            words[k] = 1;
    }
    stack_bytes = rv_abi_assign(words, narg, loc);

    need_stage = (retkind == RET_FLOAT && i->imm != FWIDTH_F32);
    for (k = 0; k < narg; k++)
        if (arg_is_float[k] && !arg_fw[k])
            need_stage = 1;

    stage = (stack_bytes + 7) & ~7;
    push_bytes = stage + (need_stage ? 8 : 0);
    push_bytes = (push_bytes + 15) & ~15;
    if (need_stage && push_bytes < stage + 8)
        push_bytes = stage + 16;

    if (push_bytes > 0)
        fprintf(out, "\taddi sp, sp, -%d\n", push_bytes);

    for (k = 0; k < narg; k++) {
        struct rv_ploc *p = &loc[k];

        if (arg_is_float[k] && !arg_fw[k]) {
            /* double: park it, then move the halves as words */
            const char *sa = frs_w(out, fn, arg_temps[k], 0, 0);
            fprintf(out, "\tfsd %s, %d(sp)\n", sa, stage);
            if (p->nreg >= 1)
                fprintf(out, "\tlw %s, %d(sp)\n", argreg[p->ireg], stage);
            else
                fprintf(out, "\tlw t0, %d(sp)\n\tsw t0, %d(sp)\n",
                    stage, p->soff);
            if (p->nreg == 2)
                fprintf(out, "\tlw %s, %d(sp)\n", argreg[p->ireg + 1],
                    stage + 4);
            else
                fprintf(out, "\tlw t0, %d(sp)\n\tsw t0, %d(sp)\n",
                    stage + 4, p->soff + (p->nreg == 1 ? 0 : 4));
        } else if (arg_is_float[k]) {
            /* single: one integer register's worth of bits */
            const char *sa = frs_w(out, fn, arg_temps[k], 0, 1);
            if (p->nreg == 1) {
                fprintf(out, "\tfmv.x.w %s, %s\n", argreg[p->ireg], sa);
            } else {
                fprintf(out, "\tfmv.x.w t0, %s\n", sa);
                fprintf(out, "\tsw t0, %d(sp)\n", p->soff);
            }
        } else if (arg_is_i64[k]) {
            const char *lo = i64_rs_lo(out, fn, arg_temps[k], 0);
            if (p->nreg >= 1)
                fprintf(out, "\tmv %s, %s\n", argreg[p->ireg], lo);
            else
                fprintf(out, "\tsw %s, %d(sp)\n", lo, p->soff);
            {
                const char *hi = i64_rs_hi(out, fn, arg_temps[k], 0);
                if (p->nreg == 2)
                    fprintf(out, "\tmv %s, %s\n", argreg[p->ireg + 1], hi);
                else
                    fprintf(out, "\tsw %s, %d(sp)\n", hi,
                        p->soff + (p->nreg == 1 ? 0 : 4));
            }
        } else {
            const char *sa = rs(out, fn, arg_temps[k], 0);
            if (p->nreg == 1)
                fprintf(out, "\tmv %s, %s\n", argreg[p->ireg], sa);
            else
                fprintf(out, "\tsw %s, %d(sp)\n", sa, p->soff);
        }
    }

    if (indirect) {
        /* the target is read before the call, and a0-a7 are already
           loaded, so it goes through a scratch register */
        const char *sa = rs(out, fn, i->a, 0);
        if (strcmp(sa, "t0") != 0)
            fprintf(out, "\tmv t0, %s\n", sa);
        fprintf(out, "\tjalr ra, t0, 0\n");
    } else {
        fprintf(out, "\tjal ra, %s\n", i->sym);
    }
    narg = 0;

    if (i->dst >= 0) {
        if (retkind == RET_I64) {
            const char *dlo = i64_rd_lo(fn, i->dst, 0);
            const char *dhi = i64_rd_hi(fn, i->dst, 1);
            if (strcmp(dlo, "a0") != 0)
                fprintf(out, "\tmv %s, a0\n", dlo);
            if (strcmp(dhi, "a1") != 0)
                fprintf(out, "\tmv %s, a1\n", dhi);
            i64_wd_lo(out, fn, i->dst, dlo);
            i64_wd_hi(out, fn, i->dst, dhi);
        } else if (retkind == RET_FLOAT) {
            int f32 = i->imm == FWIDTH_F32;
            const char *sd = frd_w(fn, i->dst, 0);
            if (f32) {
                fprintf(out, "\tfmv.w.x %s, a0\n", sd);
            } else {
                fprintf(out, "\tsw a0, %d(sp)\n", stage);
                fprintf(out, "\tsw a1, %d(sp)\n", stage + 4);
                fprintf(out, "\tfld %s, %d(sp)\n", sd, stage);
            }
            fwd_f(out, fn, i->dst, sd, f32);
        } else {
            const char *sd = rd(fn, i->dst, 0);
            if (strcmp(sd, "a0") != 0)
                fprintf(out, "\tmv %s, a0\n", sd);
            wd(out, fn, i->dst, sd);
        }
    }

    /* the block is released only after a double result has been read
       out of the staging area */
    if (push_bytes > 0)
        fprintf(out, "\taddi sp, sp, %d\n", push_bytes);
}
#else
static void
emit_call_flush(FILE *out, struct ir_func *fn, struct ir_insn *i,
                int indirect, int retkind)
{
    int k, push_bytes, off;

    /* An 8-byte arg (a float, which cc pads to 8 with an F32 on the low 4,
       or an i64) is 8-aligned in the outgoing block, mirroring slot_offset's
       param layout so the callee reads each param where the caller wrote it.
       The whole block rounds to 16 so the callee is entered with a 16-aligned
       sp (keeping its s0 8-aligned). */
    off = 0;
    for (k = 0; k < narg; k++) {
        int eight = arg_is_float[k] || arg_is_i64[k];
        if (eight)
            off = (off + 7) & ~7;
        off += eight ? 8 : 4;
    }
    push_bytes = (off + 15) & ~15;

    if (push_bytes > 0)
        fprintf(out, "\taddi sp, sp, -%d\n", push_bytes);
    off = 0;
    for (k = 0; k < narg; k++) {
        if (arg_is_float[k]) {
            int f32 = arg_fw[k];
            const char *sa;
            off = (off + 7) & ~7;
            sa = frs_w(out, fn, arg_temps[k], 0, f32);
            fprintf(out, "\t%s %s, %d(sp)\n", f32 ? "fsw" : "fsd",
                sa, off);
            off += 8;
        } else if (arg_is_i64[k]) {
            const char *lo, *hi;
            off = (off + 7) & ~7;
            lo = i64_rs_lo(out, fn, arg_temps[k], 0);
            fprintf(out, "\tsw %s, %d(sp)\n", lo, off);
            hi = i64_rs_hi(out, fn, arg_temps[k], 0);
            fprintf(out, "\tsw %s, %d(sp)\n", hi, off + 4);
            off += 8;
        } else {
            const char *sa = rs(out, fn, arg_temps[k], 0);
            fprintf(out, "\tsw %s, %d(sp)\n", sa, off);
            off += 4;
        }
    }
    if (indirect) {
        const char *sa = rs(out, fn, i->a, 0);
        fprintf(out, "\tjalr ra, %s, 0\n", sa);
    } else {
        fprintf(out, "\tjal ra, %s\n", i->sym);
    }
    if (push_bytes > 0)
        fprintf(out, "\taddi sp, sp, %d\n", push_bytes);
    narg = 0;

    if (i->dst >= 0) {
        if (retkind == RET_I64) {
            const char *dlo = i64_rd_lo(fn, i->dst, 0);
            const char *dhi = i64_rd_hi(fn, i->dst, 1);
            if (strcmp(dlo, "a0") != 0)
                fprintf(out, "\tmv %s, a0\n", dlo);
            if (strcmp(dhi, "a1") != 0)
                fprintf(out, "\tmv %s, a1\n", dhi);
            i64_wd_lo(out, fn, i->dst, dlo);
            i64_wd_hi(out, fn, i->dst, dhi);
        } else if (retkind == RET_FLOAT) {
            int f32 = i->imm == FWIDTH_F32;
            const char *sd = frd_w(fn, i->dst, 0);
            fprintf(out, "\tfmv.%s %s, fa0\n", f32 ? "s" : "d", sd);
            fwd_f(out, fn, i->dst, sd, f32);
        } else {
            const char *sd = rd(fn, i->dst, 0);
            if (strcmp(sd, "a0") != 0)
                fprintf(out, "\tmv %s, a0\n", sd);
            wd(out, fn, i->dst, sd);
        }
    }
}
#endif /* CC_PSABI */

#ifdef CC_PSABI
/*
 * A tail call under the platform ABI.  With every argument in a
 * register the frame can go before the jump, which is a real tail call.
 * With an argument in the outgoing block it cannot: that block lives
 * below the frame being torn down.  The fall-back is an ordinary call
 * and a return, which is correct but keeps the frame, so a tail-
 * recursive loop through it grows the stack.  Only the Scheme front end
 * relies on the tail property, and it does not target this ABI.
 */
static void
emit_tailcall_flush(FILE *out, struct ir_func *fn, struct ir_insn *i,
                    int indirect)
{
    struct rv_ploc loc[RV_MAX_ARGS];
    int words[RV_MAX_ARGS] = { 0 };
    int k, stack_bytes;

    for (k = 0; k < narg; k++) {
        if (arg_is_i64[k])
            words[k] = 2;
        else if (arg_is_float[k])
            words[k] = arg_fw[k] ? 1 : 2;
        else
            words[k] = 1;
    }
    stack_bytes = rv_abi_assign(words, narg, loc);

    if (stack_bytes > 0) {
        struct ir_insn call = *i;
        call.dst = -1;
        emit_call_flush(out, fn, &call, indirect, RET_INT);
        emit_epilogue(out, fn);
        return;
    }

    for (k = 0; k < narg; k++) {
        struct rv_ploc *p = &loc[k];

        if (arg_is_float[k] && !arg_fw[k]) {
            const char *sa = frs_w(out, fn, arg_temps[k], 0, 0);
            fprintf(out, "\taddi sp, sp, -8\n");
            fprintf(out, "\tfsd %s, 0(sp)\n", sa);
            fprintf(out, "\tlw %s, 0(sp)\n", argreg[p->ireg]);
            fprintf(out, "\tlw %s, 4(sp)\n", argreg[p->ireg + 1]);
            fprintf(out, "\taddi sp, sp, 8\n");
        } else if (arg_is_float[k]) {
            const char *sa = frs_w(out, fn, arg_temps[k], 0, 1);
            fprintf(out, "\tfmv.x.w %s, %s\n", argreg[p->ireg], sa);
        } else if (arg_is_i64[k]) {
            const char *lo = i64_rs_lo(out, fn, arg_temps[k], 0);
            fprintf(out, "\tmv %s, %s\n", argreg[p->ireg], lo);
            fprintf(out, "\tmv %s, %s\n", argreg[p->ireg + 1],
                i64_rs_hi(out, fn, arg_temps[k], 0));
        } else {
            const char *sa = rs(out, fn, arg_temps[k], 0);
            fprintf(out, "\tmv %s, %s\n", argreg[p->ireg], sa);
        }
    }
    if (indirect) {
        const char *sa = rs(out, fn, i->a, 0);
        fprintf(out, "\tmv t2, %s\n", sa);
    }
    narg = 0;
    emit_epilogue_no_ret(out, fn);
    if (indirect)
        fprintf(out, "\tjr t2\n");
    else
        fprintf(out, "\tj %s\n", i->sym);
}
#else
static void
emit_tailcall_flush(FILE *out, struct ir_func *fn, struct ir_insn *i,
                    int indirect)
{
    int k;

    for (k = 0; k < narg; k++) {
        const char *sa = rs(out, fn, arg_temps[k], 0);
        fprintf(out, "\tsw %s, %d(s0)\n", sa, 8 + 4 * k);
    }
    if (indirect) {
        const char *sa = rs(out, fn, i->a, 0);
        fprintf(out, "\tmv t2, %s\n", sa);
    }
    narg = 0;
    emit_epilogue_no_ret(out, fn);
    if (indirect)
        fprintf(out, "\tjr t2\n");
    else
        fprintf(out, "\tj %s\n", i->sym);
}
#endif /* CC_PSABI */

static void
emit_insn(FILE *out, struct ir_func *fn, struct ir_insn *i)
{
    switch (i->op) {
    case IR_NOP:
        break;

    case IR_ASM:
        /* basic inline asm: the string is emitted verbatim */
        fprintf(out, "%s\n", i->sym);
        break;

    case IR_LIC: {
        const char *sd = rd(fn, i->dst, 0);
        fprintf(out, "\tli %s, %ld\n", sd, i->imm);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_LEA: {
        const char *sd = rd(fn, i->dst, 0);
        fprintf(out, "\tla %s, %s\n", sd, i->sym);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_ADL: {
        const char *sd = rd(fn, i->dst, 0);
        int off = slot_offset(fn, i->slot);
        emit_addbig(out, sd, "s0", off);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_MOV: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sd = rd(fn, i->dst, 0);
        if (strcmp(sa, sd) != 0)
            fprintf(out, "\tmv %s, %s\n", sd, sa);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_ADD:  emit_binop(out, fn, i, "add");  break;
    case IR_SUB:  emit_binop(out, fn, i, "sub");  break;
    case IR_MUL:  emit_binop(out, fn, i, "mul");  break;
    case IR_AND:  emit_binop(out, fn, i, "and");  break;
    case IR_OR:   emit_binop(out, fn, i, "or");   break;
    case IR_XOR:  emit_binop(out, fn, i, "xor");  break;
    case IR_SHL:  emit_binop(out, fn, i, "sll");  break;
    case IR_SHRS: emit_binop(out, fn, i, "sra");  break;
    case IR_SHRU: emit_binop(out, fn, i, "srl");  break;
    case IR_DIVS: emit_binop(out, fn, i, "div");  break;
    case IR_DIVU: emit_binop(out, fn, i, "divu"); break;
    case IR_MODS: emit_binop(out, fn, i, "rem");  break;
    case IR_MODU: emit_binop(out, fn, i, "remu"); break;

    case IR_NEG: emit_unop(out, fn, i, "neg"); break;
    case IR_NOT: emit_unop(out, fn, i, "not"); break;

    case IR_LB:  emit_load(out, fn, i, "lbu"); break;
    case IR_LBS: emit_load(out, fn, i, "lb");  break;
    case IR_LH:  emit_load(out, fn, i, "lhu"); break;
    case IR_LHS: emit_load(out, fn, i, "lh");  break;
    case IR_LW:  emit_load(out, fn, i, "lw");  break;

    case IR_SB: emit_store(out, fn, i, "sb"); break;
    case IR_SH: emit_store(out, fn, i, "sh"); break;
    case IR_SW: emit_store(out, fn, i, "sw"); break;

    case IR_LDL: {
        const char *sd = rd(fn, i->dst, 0);
        int off = slot_offset(fn, i->slot);
        emit_mem(out, "lw", sd, off, "s0");
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_STL: {
        const char *sa = rs(out, fn, i->a, 0);
        int off = slot_offset(fn, i->slot);
        emit_mem(out, "sw", sa, off, "s0");
        break;
    }

    case IR_ALLOCA: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sd = rd(fn, i->dst, 0);
        if (strcmp(sa, "t0") != 0)
            fprintf(out, "\tmv t0, %s\n", sa);
        fprintf(out, "\taddi t0, t0, 3\n");
        fprintf(out, "\tandi t0, t0, -4\n");
        fprintf(out, "\tsub sp, sp, t0\n");
        fprintf(out, "\tmv %s, sp\n", sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CMPEQ:
    case IR_CMPNE:
    case IR_CMPLTS: case IR_CMPLES: case IR_CMPGTS: case IR_CMPGES:
    case IR_CMPLTU: case IR_CMPLEU: case IR_CMPGTU: case IR_CMPGEU:
        emit_cmp(out, fn, i, i->op);
        break;

    case IR_JMP:
        fprintf(out, "\tj .L%d_%d\n", label_prefix, i->label);
        break;
    case IR_BZ: {
        const char *sa = rs(out, fn, i->a, 0);
        fprintf(out, "\tbeqz %s, .L%d_%d\n",
            sa, label_prefix, i->label);
        break;
    }
    case IR_BNZ: {
        const char *sa = rs(out, fn, i->a, 0);
        fprintf(out, "\tbnez %s, .L%d_%d\n",
            sa, label_prefix, i->label);
        break;
    }
    case IR_LABEL:
        fprintf(out, ".L%d_%d:\n", label_prefix, i->label);
        break;

    case IR_ARG:
        if (narg >= RV_MAX_ARGS)
            die("rv_emit: too many args");
        arg_is_i64[narg] = 0;
        arg_is_float[narg] = 0;
        arg_temps[narg++] = i->a;
        break;
    case IR_ARG64:
        if (narg >= RV_MAX_ARGS)
            die("rv_emit: too many args");
        arg_is_i64[narg] = 1;
        arg_is_float[narg] = 0;
        arg_temps[narg++] = i->a;
        break;
    case IR_CALL:
        emit_call_flush(out, fn, i, 0, 0);
        break;
    case IR_CALLI:
        emit_call_flush(out, fn, i, 1, 0);
        break;
    case IR_TAILCALL:
        emit_tailcall_flush(out, fn, i, 0);
        break;
    case IR_TAILCALLI:
        emit_tailcall_flush(out, fn, i, 1);
        break;

    case IR_RET:
        emit_epilogue(out, fn);
        break;
    case IR_RETV: {
        const char *sa = rs(out, fn, i->a, 0);
        if (strcmp(sa, "a0") != 0)
            fprintf(out, "\tmv a0, %s\n", sa);
        emit_epilogue(out, fn);
        break;
    }

    case IR_MARK: {
        const char *sd = rd(fn, i->dst, 0);
        int off = slot_offset(fn, i->slot);

        emit_mem(out, "sw", "s0", off, "s0");
        emit_mem(out, "sw", "sp", off + 4, "s0");
        fprintf(out, "\tla t0, .Lmark%d_%d\n",
            label_prefix, i->label);
        emit_mem(out, "sw", "t0", off + 8, "s0");
        emit_addbig(out, "t0", "s0", off);
        fprintf(out, "\tla t1, __cont_mark_sp\n");
        fprintf(out, "\tsw t0, 0(t1)\n");
        fprintf(out, "\tli a0, 0\n");
        fprintf(out, ".Lmark%d_%d:\n", label_prefix, i->label);
        if (strcmp(sd, "a0") != 0)
            fprintf(out, "\tmv %s, a0\n", sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CAPTURE: {
        const char *sd = rd(fn, i->dst, 0);
        int k;

        for (k = 0; k < NSAVED; k++)
            fprintf(out, "\taddi sp, sp, -4\n\tsw %s, 0(sp)\n",
                regs[k + 2]);
        fprintf(out, "\tjal ra, __cont_capture\n");
        for (k = NSAVED - 1; k >= 0; k--)
            fprintf(out, "\tlw %s, 0(sp)\n\taddi sp, sp, 4\n",
                regs[k + 2]);
        if (strcmp(sd, "a0") != 0)
            fprintf(out, "\tmv %s, a0\n", sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_RESUME: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sb = rs(out, fn, i->b, 1);

        fprintf(out, "\taddi sp, sp, -8\n");
        fprintf(out, "\tsw %s, 0(sp)\n", sa);
        fprintf(out, "\tsw %s, 4(sp)\n", sb);
        fprintf(out, "\tjal ra, __cont_resume\n");
        break;
    }

    /* ---- Floating point (IEEE 754, hardware RV32FD + Zfh) ---- */

    case IR_FADD: emit_fbinop(out, fn, i, "fadd"); break;
    case IR_FSUB: emit_fbinop(out, fn, i, "fsub"); break;
    case IR_FMUL: emit_fbinop(out, fn, i, "fmul"); break;
    case IR_FDIV: emit_fbinop(out, fn, i, "fdiv"); break;
    case IR_FNEG: emit_funop(out, fn, i, "fneg"); break;
    case IR_FABS: emit_funop(out, fn, i, "fabs"); break;

    case IR_FCMPEQ: case IR_FCMPLT: case IR_FCMPLE: {
        /* feq/flt/fle write an int bool directly; a NaN operand yields 0
           (unordered), matching the other backends. */
        int f32 = i->imm == FWIDTH_F32;
        const char *sfx = f32 ? "s" : "d";
        const char *fa = frs_w(out, fn, i->a, 0, f32);
        const char *fb = frs_w(out, fn, i->b, 1, f32);
        const char *sd = rd(fn, i->dst, 0);
        const char *op = i->op == IR_FCMPEQ ? "feq"
                       : i->op == IR_FCMPLT ? "flt" : "fle";
        fprintf(out, "\t%s.%s %s, %s, %s\n", op, sfx, sd, fa, fb);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_ITOF: {
        int f32 = i->imm == FWIDTH_F32;
        const char *sa = rs(out, fn, i->a, 0);       /* int */
        const char *sd = frd_w(fn, i->dst, 0);
        fprintf(out, "\tfcvt.%s.w %s, %s\n", f32 ? "s" : "d", sd, sa);
        fwd_f(out, fn, i->dst, sd, f32);
        break;
    }

    case IR_FTOI: {
        int f32 = i->imm == FWIDTH_F32;
        const char *sa = frs_w(out, fn, i->a, 0, f32);
        const char *sd = rd(fn, i->dst, 0);          /* int */
        fprintf(out, "\tfcvt.w.%s %s, %s, rtz\n", f32 ? "s" : "d",
            sd, sa);                                 /* round toward zero */
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_F32TOF64: {                              /* single -> double */
        const char *sa = frs_w(out, fn, i->a, 0, 1);
        const char *sd = frd_w(fn, i->dst, 0);
        fprintf(out, "\tfcvt.d.s %s, %s\n", sd, sa);
        fwd_f(out, fn, i->dst, sd, 0);
        break;
    }

    case IR_F64TOF32: {                              /* double -> single */
        const char *sa = frs_w(out, fn, i->a, 0, 0);
        const char *sd = frd_w(fn, i->dst, 0);
        fprintf(out, "\tfcvt.s.d %s, %s\n", sd, sa);
        fwd_f(out, fn, i->dst, sd, 1);
        break;
    }

    case IR_FLDL: {
        int f32 = i->imm == FWIDTH_F32;
        const char *sd = frd_w(fn, i->dst, 0);
        emit_mem(out, f32 ? "flw" : "fld", sd,
            slot_offset(fn, i->slot), "s0");
        fwd_f(out, fn, i->dst, sd, f32);
        break;
    }

    case IR_FSTL: {
        int f32 = i->imm == FWIDTH_F32;
        const char *sa = frs_w(out, fn, i->a, 0, f32);
        emit_mem(out, f32 ? "fsw" : "fsd", sa,
            slot_offset(fn, i->slot), "s0");
        break;
    }

    case IR_FLD: {
        int f32 = i->imm == FWIDTH_F32;
        const char *sa = rs(out, fn, i->a, 0);       /* address */
        const char *sd = frd_w(fn, i->dst, 0);
        fprintf(out, "\t%s %s, 0(%s)\n", f32 ? "flw" : "fld", sd, sa);
        fwd_f(out, fn, i->dst, sd, f32);
        break;
    }

    case IR_FSD: {
        int f32 = i->imm == FWIDTH_F32;
        const char *sa = rs(out, fn, i->a, 0);       /* address */
        const char *sb = frs_w(out, fn, i->b, 0, f32);
        fprintf(out, "\t%s %s, 0(%s)\n", f32 ? "fsw" : "fsd", sb, sa);
        break;
    }

    case IR_FLS: {                                   /* load single -> double */
        const char *sa = rs(out, fn, i->a, 0);
        const char *sd = frd_w(fn, i->dst, 0);
        fprintf(out, "\tflw ft0, 0(%s)\n", sa);
        fprintf(out, "\tfcvt.d.s %s, ft0\n", sd);
        fwd_f(out, fn, i->dst, sd, 0);
        break;
    }

    case IR_FSS: {                                   /* double -> single, store */
        const char *sa = rs(out, fn, i->a, 0);
        const char *sb = frs_w(out, fn, i->b, 0, 0);
        fprintf(out, "\tfcvt.s.d ft0, %s\n", sb);
        fprintf(out, "\tfsw ft0, 0(%s)\n", sa);
        break;
    }

    case IR_FLH: {                                   /* load half -> double */
        const char *sa = rs(out, fn, i->a, 0);
        const char *sd = frd_w(fn, i->dst, 0);
        fprintf(out, "\tflh ft0, 0(%s)\n", sa);      /* Zfh, no helper */
        fprintf(out, "\tfcvt.d.h %s, ft0\n", sd);
        fwd_f(out, fn, i->dst, sd, 0);
        break;
    }

    case IR_FSH: {                                   /* double -> half, store */
        const char *sa = rs(out, fn, i->a, 0);
        const char *sb = frs_w(out, fn, i->b, 0, 0);
        fprintf(out, "\tfcvt.h.d ft0, %s\n", sb);
        fprintf(out, "\tfsh ft0, 0(%s)\n", sa);
        break;
    }

    case IR_FRETV: {
        int f32 = i->imm == FWIDTH_F32;
        const char *sa = frs_w(out, fn, i->a, 0, f32);
#ifdef CC_PSABI
        /* the soft-float ABI returns in a0 (and a1 for a double), which
           on RV32 a double can only reach through memory */
        if (f32) {
            fprintf(out, "\tfmv.x.w a0, %s\n", sa);
        } else {
            fprintf(out, "\taddi sp, sp, -8\n");
            fprintf(out, "\tfsd %s, 0(sp)\n", sa);
            fprintf(out, "\tlw a0, 0(sp)\n");
            fprintf(out, "\tlw a1, 4(sp)\n");
            fprintf(out, "\taddi sp, sp, 8\n");
        }
#else
        if (strcmp(sa, "fa0") != 0)
            fprintf(out, "\tfmv.%s fa0, %s\n", f32 ? "s" : "d", sa);
#endif
        emit_epilogue(out, fn);
        break;
    }

    case IR_FARG:
        if (narg >= RV_MAX_ARGS)
            die("rv_emit: too many args");
        arg_is_i64[narg] = 0;
        arg_is_float[narg] = 1;
        arg_fw[narg] = i->imm == FWIDTH_F32;
        arg_temps[narg++] = i->a;
        break;

    case IR_FCALL:
        emit_call_flush(out, fn, i, 0, RET_FLOAT);
        break;
    case IR_FCALLI:
        emit_call_flush(out, fn, i, 1, RET_FLOAT);
        break;

    /* ---- I64 opcodes ---- */

    case IR_LIC64: {
        uint64_t val = (uint64_t)i->imm;
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        fprintf(out, "\tli %s, %d\n", dlo, (int)(uint32_t)val);
        fprintf(out, "\tli %s, %d\n", dhi,
            (int)(uint32_t)(val >> 32));
        i64_wd_lo(out, fn, i->dst, dlo);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_ADD64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *blo = i64_rs_lo(out, fn, i->b, 1);
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        fprintf(out, "\tadd %s, %s, %s\n", dlo, alo, blo);
        fprintf(out, "\tsltu t0, %s, %s\n", dlo, blo);
        i64_wd_lo(out, fn, i->dst, dlo);
        const char *ahi = i64_rs_hi(out, fn, i->a, 1);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        fprintf(out, "\tadd %s, %s, t0\n", dhi, ahi);
        const char *bhi = i64_rs_hi(out, fn, i->b, 0);
        fprintf(out, "\tadd %s, %s, %s\n", dhi, dhi, bhi);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_SUB64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *blo = i64_rs_lo(out, fn, i->b, 1);
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        fprintf(out, "\tsltu t0, %s, %s\n", alo, blo);
        fprintf(out, "\tsub %s, %s, %s\n", dlo, alo, blo);
        i64_wd_lo(out, fn, i->dst, dlo);
        const char *ahi = i64_rs_hi(out, fn, i->a, 1);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        fprintf(out, "\tsub %s, %s, t0\n", dhi, ahi);
        const char *bhi = i64_rs_hi(out, fn, i->b, 0);
        fprintf(out, "\tsub %s, %s, %s\n", dhi, dhi, bhi);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_MUL64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *blo = i64_rs_lo(out, fn, i->b, 1);
        fprintf(out, "\taddi sp, sp, -8\n");
        fprintf(out, "\tsw %s, 0(sp)\n", alo);
        fprintf(out, "\tsw %s, 4(sp)\n",
            i64_rs_hi(out, fn, i->a, 0));
        fprintf(out, "\taddi sp, sp, -8\n");
        fprintf(out, "\tsw %s, 0(sp)\n", blo);
        fprintf(out, "\tsw %s, 4(sp)\n",
            i64_rs_hi(out, fn, i->b, 1));
        fprintf(out, "\tjal ra, __muldi3\n");
        fprintf(out, "\taddi sp, sp, 16\n");
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        if (strcmp(dlo, "a0") != 0)
            fprintf(out, "\tmv %s, a0\n", dlo);
        if (strcmp(dhi, "a1") != 0)
            fprintf(out, "\tmv %s, a1\n", dhi);
        i64_wd_lo(out, fn, i->dst, dlo);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_AND64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *blo = i64_rs_lo(out, fn, i->b, 1);
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        fprintf(out, "\tand %s, %s, %s\n", dlo, alo, blo);
        i64_wd_lo(out, fn, i->dst, dlo);
        const char *ahi = i64_rs_hi(out, fn, i->a, 0);
        const char *bhi = i64_rs_hi(out, fn, i->b, 1);
        const char *dhi = i64_rd_hi(fn, i->dst, 0);
        fprintf(out, "\tand %s, %s, %s\n", dhi, ahi, bhi);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_OR64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *blo = i64_rs_lo(out, fn, i->b, 1);
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        fprintf(out, "\tor %s, %s, %s\n", dlo, alo, blo);
        i64_wd_lo(out, fn, i->dst, dlo);
        const char *ahi = i64_rs_hi(out, fn, i->a, 0);
        const char *bhi = i64_rs_hi(out, fn, i->b, 1);
        const char *dhi = i64_rd_hi(fn, i->dst, 0);
        fprintf(out, "\tor %s, %s, %s\n", dhi, ahi, bhi);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_XOR64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *blo = i64_rs_lo(out, fn, i->b, 1);
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        fprintf(out, "\txor %s, %s, %s\n", dlo, alo, blo);
        i64_wd_lo(out, fn, i->dst, dlo);
        const char *ahi = i64_rs_hi(out, fn, i->a, 0);
        const char *bhi = i64_rs_hi(out, fn, i->b, 1);
        const char *dhi = i64_rd_hi(fn, i->dst, 0);
        fprintf(out, "\txor %s, %s, %s\n", dhi, ahi, bhi);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_NEG64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *ahi = i64_rs_hi(out, fn, i->a, 1);
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        fprintf(out, "\tneg %s, %s\n", dlo, alo);
        fprintf(out, "\tsnez t0, %s\n", alo);
        fprintf(out, "\tneg %s, %s\n", dhi, ahi);
        fprintf(out, "\tsub %s, %s, t0\n", dhi, dhi);
        i64_wd_lo(out, fn, i->dst, dlo);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_SHL64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *ahi = i64_rs_hi(out, fn, i->a, 1);
        const char *sb = rs(out, fn, i->b, 0);
        fprintf(out, "\taddi sp, sp, -8\n");
        fprintf(out, "\tsw %s, 0(sp)\n", alo);
        fprintf(out, "\tsw %s, 4(sp)\n", ahi);
        fprintf(out, "\taddi sp, sp, -4\n");
        fprintf(out, "\tsw %s, 0(sp)\n", sb);
        fprintf(out, "\tjal ra, __ashldi3\n");
        fprintf(out, "\taddi sp, sp, 12\n");
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        if (strcmp(dlo, "a0") != 0)
            fprintf(out, "\tmv %s, a0\n", dlo);
        if (strcmp(dhi, "a1") != 0)
            fprintf(out, "\tmv %s, a1\n", dhi);
        i64_wd_lo(out, fn, i->dst, dlo);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_SHRS64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *ahi = i64_rs_hi(out, fn, i->a, 1);
        const char *sb = rs(out, fn, i->b, 0);
        fprintf(out, "\taddi sp, sp, -8\n");
        fprintf(out, "\tsw %s, 0(sp)\n", alo);
        fprintf(out, "\tsw %s, 4(sp)\n", ahi);
        fprintf(out, "\taddi sp, sp, -4\n");
        fprintf(out, "\tsw %s, 0(sp)\n", sb);
        fprintf(out, "\tjal ra, __ashrdi3\n");
        fprintf(out, "\taddi sp, sp, 12\n");
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        if (strcmp(dlo, "a0") != 0)
            fprintf(out, "\tmv %s, a0\n", dlo);
        if (strcmp(dhi, "a1") != 0)
            fprintf(out, "\tmv %s, a1\n", dhi);
        i64_wd_lo(out, fn, i->dst, dlo);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_SHRU64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *ahi = i64_rs_hi(out, fn, i->a, 1);
        const char *sb = rs(out, fn, i->b, 0);
        fprintf(out, "\taddi sp, sp, -8\n");
        fprintf(out, "\tsw %s, 0(sp)\n", alo);
        fprintf(out, "\tsw %s, 4(sp)\n", ahi);
        fprintf(out, "\taddi sp, sp, -4\n");
        fprintf(out, "\tsw %s, 0(sp)\n", sb);
        fprintf(out, "\tjal ra, __lshrdi3\n");
        fprintf(out, "\taddi sp, sp, 12\n");
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        if (strcmp(dlo, "a0") != 0)
            fprintf(out, "\tmv %s, a0\n", dlo);
        if (strcmp(dhi, "a1") != 0)
            fprintf(out, "\tmv %s, a1\n", dhi);
        i64_wd_lo(out, fn, i->dst, dlo);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_CMP64EQ: case IR_CMP64NE: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *blo = i64_rs_lo(out, fn, i->b, 1);
        const char *sd = rd(fn, i->dst, 0);
        fprintf(out, "\txor t0, %s, %s\n", alo, blo);
        const char *ahi = i64_rs_hi(out, fn, i->a, 0);
        const char *bhi = i64_rs_hi(out, fn, i->b, 1);
        fprintf(out, "\txor t1, %s, %s\n", ahi, bhi);
        fprintf(out, "\tor t0, t0, t1\n");
        if (i->op == IR_CMP64EQ)
            fprintf(out, "\tseqz %s, t0\n", sd);
        else
            fprintf(out, "\tsnez %s, t0\n", sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CMP64LTS: case IR_CMP64LES:
    case IR_CMP64GTS: case IR_CMP64GES:
    case IR_CMP64LTU: case IR_CMP64LEU:
    case IR_CMP64GTU: case IR_CMP64GEU: {
        int ser = ++i64cmp_serial;
        const char *ahi = i64_rs_hi(out, fn, i->a, 0);
        const char *bhi = i64_rs_hi(out, fn, i->b, 1);
        const char *sd = rd(fn, i->dst, 0);
        const char *hi_op, *lo_op;
        int swap_ab = 0;

        switch (i->op) {
        case IR_CMP64LTS: hi_op = "slt"; lo_op = "sltu"; break;
        case IR_CMP64GTS: hi_op = "slt"; lo_op = "sltu"; swap_ab = 1; break;
        case IR_CMP64LTU: hi_op = "sltu"; lo_op = "sltu"; break;
        case IR_CMP64GTU: hi_op = "sltu"; lo_op = "sltu"; swap_ab = 1; break;
        case IR_CMP64LES: hi_op = "slt"; lo_op = "sltu"; swap_ab = 1; break;
        case IR_CMP64GES: hi_op = "slt"; lo_op = "sltu"; break;
        case IR_CMP64LEU: hi_op = "sltu"; lo_op = "sltu"; swap_ab = 1; break;
        case IR_CMP64GEU: hi_op = "sltu"; lo_op = "sltu"; break;
        default: hi_op = lo_op = "sltu"; break;
        }

        if (swap_ab) {
            const char *tmp;
            tmp = ahi; ahi = bhi; bhi = tmp;
        }

        fprintf(out, "\t%s %s, %s, %s\n", hi_op, sd, ahi, bhi);
        fprintf(out, "\tbne %s, %s, .Lc%d_done\n", ahi, bhi, ser);

        const char *alo = i64_rs_lo(out, fn,
            swap_ab ? i->b : i->a, 0);
        const char *blo = i64_rs_lo(out, fn,
            swap_ab ? i->a : i->b, 1);
        fprintf(out, "\t%s %s, %s, %s\n", lo_op, sd, alo, blo);
        fprintf(out, ".Lc%d_done:\n", ser);

        switch (i->op) {
        case IR_CMP64LES: case IR_CMP64GES:
        case IR_CMP64LEU: case IR_CMP64GEU:
            fprintf(out, "\txori %s, %s, 1\n", sd, sd);
            break;
        default:
            break;
        }

        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_LD64: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        fprintf(out, "\tlw %s, 0(%s)\n", dlo, sa);
        fprintf(out, "\tlw %s, 4(%s)\n", dhi, sa);
        i64_wd_lo(out, fn, i->dst, dlo);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_ST64: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *blo = i64_rs_lo(out, fn, i->b, 1);
        fprintf(out, "\tsw %s, 0(%s)\n", blo, sa);
        const char *bhi = i64_rs_hi(out, fn, i->b, 1);
        fprintf(out, "\tsw %s, 4(%s)\n", bhi, sa);
        break;
    }

    case IR_LDL64: {
        int off = slot_offset(fn, i->slot);
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        emit_mem(out, "lw", dlo, off, "s0");
        emit_mem(out, "lw", dhi, off + 4, "s0");
        i64_wd_lo(out, fn, i->dst, dlo);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_STL64: {
        int off = slot_offset(fn, i->slot);
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        emit_mem(out, "sw", alo, off, "s0");
        const char *ahi = i64_rs_hi(out, fn, i->a, 1);
        emit_mem(out, "sw", ahi, off + 4, "s0");
        break;
    }

    case IR_SEXT64: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        if (strcmp(dlo, sa) != 0)
            fprintf(out, "\tmv %s, %s\n", dlo, sa);
        fprintf(out, "\tsrai %s, %s, 31\n", dhi, dlo);
        i64_wd_lo(out, fn, i->dst, dlo);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_ZEXT64: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *dlo = i64_rd_lo(fn, i->dst, 0);
        const char *dhi = i64_rd_hi(fn, i->dst, 1);
        if (strcmp(dlo, sa) != 0)
            fprintf(out, "\tmv %s, %s\n", dlo, sa);
        fprintf(out, "\tli %s, 0\n", dhi);
        i64_wd_lo(out, fn, i->dst, dlo);
        i64_wd_hi(out, fn, i->dst, dhi);
        break;
    }

    case IR_TRUNC64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *sd = rd(fn, i->dst, 0);
        if (strcmp(sd, alo) != 0)
            fprintf(out, "\tmv %s, %s\n", sd, alo);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CALL64:
        emit_call_flush(out, fn, i, 0, 1);
        break;
    case IR_CALLI64:
        emit_call_flush(out, fn, i, 1, 1);
        break;

    case IR_RETV64: {
        const char *alo = i64_rs_lo(out, fn, i->a, 0);
        const char *ahi = i64_rs_hi(out, fn, i->a, 1);
        if (strcmp(alo, "a0") != 0)
            fprintf(out, "\tmv a0, %s\n", alo);
        if (strcmp(ahi, "a1") != 0)
            fprintf(out, "\tmv a1, %s\n", ahi);
        emit_epilogue(out, fn);
        break;
    }

    case IR_FUNC:
    case IR_ENDF:
        break;

    default:
        die("rv_emit: unhandled op %d", i->op);
    }
}

/****************************************************************
 * Per-function emission
 ****************************************************************/

static int fn_serial;

static void
emit_function(FILE *out, struct ir_func *fn)
{
    struct ir_insn *i;
    struct ir_insn *last = NULL;

    narg = 0;
    label_prefix = ++fn_serial;
    compute_used_fregs(fn);

    fprintf(out, "\n\t.text\n\t.align 2\n");
    if (!fn->is_local)
        fprintf(out, "\t.globl %s\n", fn->name);
    fprintf(out, "%s:\n", fn->name);
    emit_prologue(out, fn);

    for (i = fn->head; i; i = i->next) {
        emit_insn(out, fn, i);
        if (i->op != IR_FUNC && i->op != IR_ENDF &&
            i->op != IR_LABEL)
            last = i;
    }

    if (!last || (last->op != IR_RET && last->op != IR_RETV &&
        last->op != IR_RETV64 &&
        last->op != IR_TAILCALL && last->op != IR_TAILCALLI))
        emit_epilogue(out, fn);
}

/****************************************************************
 * Globals
 ****************************************************************/

static void
emit_string_bytes(FILE *out, const char *s, int n)
{
    int k;

    fputs("\t.ascii \"", out);
    for (k = 0; k < n; k++) {
        unsigned char c = (unsigned char)s[k];
        if (c == '\\')
            fputs("\\\\", out);
        else if (c == '"')
            fputs("\\\"", out);
        else if (c == '\n')
            fputs("\\n", out);
        else if (c == '\t')
            fputs("\\t", out);
        else if (c == '\r')
            fputs("\\r", out);
        else if (c >= 0x20 && c < 0x7f)
            fputc(c, out);
        else
            fprintf(out, "\\%03o", c);
    }
    fputs("\"\n", out);
}

/* emit an aggregate global's byte image: pad to each item's offset, emit the
   sized value (8-byte low word first, little-endian), pad to the full size */
static void
emit_init_image(FILE *out, struct ir_global *g)
{
    struct ir_init *it;
    int cur = 0;

    for (it = g->inits; it; it = it->next) {
        if (it->offset > cur) {
            fprintf(out, "\t.space %d\n", it->offset - cur);
            cur = it->offset;
        }
        if (it->sym)
            fprintf(out, "\t.word %s\n", it->sym);
        else if (it->size == 8) {
            uint64_t b = (uint64_t)it->ival;
            fprintf(out, "\t.word 0x%08x\n", (unsigned)(b & 0xFFFFFFFF));
            fprintf(out, "\t.word 0x%08x\n", (unsigned)(b >> 32));
        } else if (it->size == 2)
            fprintf(out, "\t.short 0x%04x\n", (unsigned)(it->ival & 0xFFFF));
        else if (it->size == 1)
            fprintf(out, "\t.byte 0x%02x\n", (unsigned)(it->ival & 0xFF));
        else
            fprintf(out, "\t.word 0x%08x\n", (unsigned)it->ival);
        cur += it->size;
    }
    if (g->arr_size > cur)
        fprintf(out, "\t.space %d\n", g->arr_size - cur);
}

static void
emit_globals(FILE *out, struct ir_program *prog)
{
    struct ir_global *g;

    fputs("\n\t.data\n", out);
    for (g = prog->globals; g; g = g->next) {
        int elsz;

        switch (g->base_type) {
        case IR_I8:  elsz = 1; break;
        case IR_I16: elsz = 2; break;
        case IR_I64: elsz = 8; break;
        case IR_F64: elsz = 8; break;
        default:     elsz = 4; break;   /* incl. IR_F32 (4-byte bit pattern) */
        }

        /* 8-align a double global (read with fld); an aggregate with an
           8-byte field or an _Alignas request (g->align) may raise it.  GAS
           .align is a power-of-two exponent (3 = 8 bytes, 2 = 4 bytes). */
        {
            int alb = g->base_type == IR_F64 ? 8 : 4, e = 0;
            if (g->align > alb)
                alb = g->align;
            while ((1 << e) < alb)
                e++;
            fprintf(out, "\t.align %d\n", e);
        }
        if (!g->is_local)
            fprintf(out, "\t.globl %s\n", g->name);
        fprintf(out, "%s:\n", g->name);
        if (g->inits) {
            emit_init_image(out, g);
        } else if (g->init_string) {
            emit_string_bytes(out, g->init_string,
                      g->init_strlen);
        } else if (g->init_count > 0) {
            int k;
            for (k = 0; k < g->init_count; k++) {
                if (g->init_syms && g->init_syms[k])
                    fprintf(out, "\t.word %s\n",
                        g->init_syms[k]);
                else if (g->base_type == IR_I64 ||
                         g->base_type == IR_F64) {
                    /* little-endian: low word first (F64 holds the raw
                       64-bit IEEE bit pattern in init_ivals) */
                    uint64_t bits = (uint64_t)g->init_ivals[k];
                    fprintf(out, "\t.word 0x%08x\n",
                        (unsigned)(bits & 0xFFFFFFFF));
                    fprintf(out, "\t.word 0x%08x\n",
                        (unsigned)(bits >> 32));
                } else if (elsz == 1)
                    fprintf(out, "\t.byte %" PRId64 "\n",
                        g->init_ivals[k]);
                else if (elsz == 2)
                    fprintf(out, "\t.short %" PRId64 "\n",
                        g->init_ivals[k]);
                else
                    fprintf(out, "\t.word %" PRId64 "\n",
                        g->init_ivals[k]);
            }
            if (g->arr_size > g->init_count)
                fprintf(out, "\t.space %d\n",
                    (g->arr_size - g->init_count) * elsz);
        } else {
            int sz = (g->arr_size > 0)
                 ? g->arr_size * elsz
                 : elsz;
            fprintf(out, "\t.space %d\n", sz);
        }
    }
}

/****************************************************************
 * Entry point
 ****************************************************************/

void
target_emit(FILE *out, struct ir_program *prog)
{
    struct ir_func *fn;

    fputs("# tinc: generated RISC-V (RV32IMFD + Zfh) assembly\n", out);
    for (fn = prog->funcs; fn; fn = fn->next)
        emit_function(out, fn);
    emit_globals(out, prog);
}
