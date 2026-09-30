/* cf_emit.c : ColdFire / m68k back-end, emits GAS-syntax assembly */
/*
 * Instruction selection is direct: one IR op -> a short burst of m68k
 * instructions.  Integer temps live in d2..d7, float temps in fp2..fp7
 * (after regalloc); spilled temps reload through scratch registers
 * (d0/d1 for int, fp0/fp1 for float).  Address temps go through a0/a1.
 *
 * SysV m68k calling convention subset:
 *   - Args pushed right-to-left on the stack, 32 bits each; caller pops.
 *   - Return value in d0.
 *   - Callee-save: d2..d7, a2..a5, fp (a6), fp2..fp7.
 *   - Scratch:     d0, d1, a0, a1, fp0, fp1.
 *
 * Frame layout:
 *
 *      +8 + 4*i (%fp)   param i
 *      +4       (%fp)   return address
 *      +0       (%fp)   saved fp
 *      -locals_size     bottom of locals
 *                       integer spill slots (4 bytes each)
 *                       float spill slots (8 bytes each)
 *                       saved d2..d7 (24 bytes, always)
 *                       saved fp2..fp7 (48 bytes, if function uses floats)
 *                %sp -> bottom of save area
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

static const char *dregs[] = {
    "%d0", "%d1", "%d2", "%d3", "%d4", "%d5", "%d6", "%d7",
};

static const char *fpregs[] = {
    "%fp0", "%fp1", "%fp2", "%fp3", "%fp4", "%fp5", "%fp6", "%fp7",
};

/****************************************************************
 * Frame layout helpers
 ****************************************************************/

static int
locals_size(struct ir_func *fn)
{
    int i, s;

    s = 0;
    for (i = fn->nparams; i < fn->nslots; i++) {
        int sz = (fn->slot_size[i] + 3) & ~3;
        s += sz;
    }
    return s;
}

static int
slot_offset(struct ir_func *fn, int slot)
{
    int i, off;

    if (slot < fn->nparams) {
        off = 8;
        for (i = 0; i < slot; i++) {
            int sz = (fn->slot_size[i] + 3) & ~3;
            off += sz;
        }
        return off;
    }
    off = 0;
    for (i = fn->nparams; i <= slot; i++) {
        int sz = (fn->slot_size[i] + 3) & ~3;
        off += sz;
    }
    return -off;
}

static int
spill_byte_offset(struct ir_func *fn, int temp)
{
    return -locals_size(fn) - (fn->temp_spill[temp] + 4);
}

static int
fspill_byte_offset(struct ir_func *fn, int temp)
{
    return -locals_size(fn) - fn->nspills * 4 - (fn->temp_spill[temp] + 8);
}

static int
i64spill_byte_offset(struct ir_func *fn, int temp)
{
    return -locals_size(fn) - fn->nspills * 4
           - fn->nfspills * 8 - (fn->temp_spill[temp] + 8);
}

static int
func_uses_fpregs(struct ir_func *fn)
{
    struct ir_insn *i;

    for (i = fn->head; i; i = i->next) {
        if (ir_op_is_float_def(i->op))
            return 1;
    }
    return 0;
}

/****************************************************************
 * Temp -> register materialisation
 ****************************************************************/

static const char *
rs(FILE *out, struct ir_func *fn, int t, int scratch)
{
    int r = fn->temp_reg[t];

    if (r >= 0)
        return dregs[r];
    fprintf(out, "\tmove.l %d(%%fp), %s\n",
        spill_byte_offset(fn, t), dregs[scratch]);
    return dregs[scratch];
}

static const char *
rd(struct ir_func *fn, int t, int scratch)
{
    int r = fn->temp_reg[t];

    if (r >= 0)
        return dregs[r];
    return dregs[scratch];
}

static void
wd(FILE *out, struct ir_func *fn, int t, const char *reg)
{
    if (fn->temp_reg[t] >= 0)
        return;
    fprintf(out, "\tmove.l %s, %d(%%fp)\n",
        reg, spill_byte_offset(fn, t));
}

static const char *
frs(FILE *out, struct ir_func *fn, int t, int scratch)
{
    int r = fn->temp_reg[t];

    if (r >= 0)
        return fpregs[r];
    fprintf(out, "\tfmove.d %d(%%fp), %s\n",
        fspill_byte_offset(fn, t), fpregs[scratch]);
    return fpregs[scratch];
}

static const char *
frd(struct ir_func *fn, int t, int scratch)
{
    int r = fn->temp_reg[t];

    if (r >= 0)
        return fpregs[r];
    return fpregs[scratch];
}

static void
fwd(FILE *out, struct ir_func *fn, int t, const char *reg)
{
    if (fn->temp_reg[t] >= 0)
        return;
    fprintf(out, "\tfmove.d %s, %d(%%fp)\n",
        reg, fspill_byte_offset(fn, t));
}

/****************************************************************
 * I64 register-pair helpers
 *
 * Pair index P maps to physical registers:
 *   hi = d(6 - 2*P), lo = d(7 - 2*P)
 * Pair 0 = (d6,d7), Pair 1 = (d4,d5), Pair 2 = (d2,d3)
 *
 * Scratch for spill reload: d0 (hi), d1 (lo)
 ****************************************************************/

static int
i64_hi_reg(int pair)
{
    return 6 - 2 * pair;
}

static int
i64_lo_reg(int pair)
{
    return 7 - 2 * pair;
}

static void
i64_rs(FILE *out, struct ir_func *fn, int t, int *hi, int *lo)
{
    int p = fn->temp_reg[t];

    if (p >= 0) {
        *hi = i64_hi_reg(p);
        *lo = i64_lo_reg(p);
    } else {
        int off = i64spill_byte_offset(fn, t);
        fprintf(out, "\tmove.l %d(%%fp), %%d0\n", off);
        fprintf(out, "\tmove.l %d(%%fp), %%d1\n", off - 4);
        *hi = 0;
        *lo = 1;
    }
}

static void
i64_rd(struct ir_func *fn, int t, int *hi, int *lo)
{
    int p = fn->temp_reg[t];

    if (p >= 0) {
        *hi = i64_hi_reg(p);
        *lo = i64_lo_reg(p);
    } else {
        *hi = 0;
        *lo = 1;
    }
}

static void
i64_wd(FILE *out, struct ir_func *fn, int t, int hi, int lo)
{
    int off;

    if (fn->temp_reg[t] >= 0)
        return;
    off = i64spill_byte_offset(fn, t);
    fprintf(out, "\tmove.l %s, %d(%%fp)\n", dregs[hi], off);
    fprintf(out, "\tmove.l %s, %d(%%fp)\n", dregs[lo], off - 4);
}

/****************************************************************
 * Binary op helper
 ****************************************************************/

static void
emit_binop(FILE *out, struct ir_func *fn, struct ir_insn *i,
           const char *mnem)
{
    const char *sa, *sb, *sd;

    sa = rs(out, fn, i->a, 0);
    sb = rs(out, fn, i->b, 1);
    sd = rd(fn, i->dst, 0);
    if (strcmp(sb, sd) == 0 && strcmp(sa, sd) != 0) {
        fprintf(out, "\tmove.l %s, %%d1\n", sb);
        sb = dregs[1];
    }
    if (strcmp(sa, sd) != 0)
        fprintf(out, "\tmove.l %s, %s\n", sa, sd);
    fprintf(out, "\t%s %s, %s\n", mnem, sb, sd);
    wd(out, fn, i->dst, sd);
}

static void
emit_unop(FILE *out, struct ir_func *fn, struct ir_insn *i,
          const char *mnem)
{
    const char *sa, *sd;

    sa = rs(out, fn, i->a, 0);
    sd = rd(fn, i->dst, 0);
    if (strcmp(sa, sd) != 0)
        fprintf(out, "\tmove.l %s, %s\n", sa, sd);
    fprintf(out, "\t%s %s\n", mnem, sd);
    wd(out, fn, i->dst, sd);
}

static void
emit_cmp(FILE *out, struct ir_func *fn, struct ir_insn *i,
         const char *scc)
{
    const char *sa, *sb, *sd;

    sa = rs(out, fn, i->a, 0);
    sb = rs(out, fn, i->b, 1);
    fprintf(out, "\tcmp.l %s, %s\n", sb, sa);
    sd = rd(fn, i->dst, 0);
    fprintf(out, "\t%s %s\n", scc, sd);
    /* scc writes 0x00/0xFF to the low byte; and.l #1 turns that into a
     * clean 0/1 and clears the upper bits, replacing the 68k-only
     * neg.b + extb.l pair (ColdFire has no byte-size neg) */
    fprintf(out, "\tand.l #1, %s\n", sd);
    wd(out, fn, i->dst, sd);
}

static void
emit_fbinop(FILE *out, struct ir_func *fn, struct ir_insn *i,
            const char *mnem)
{
    const char *sa, *sb, *sd;

    sa = frs(out, fn, i->a, 0);
    sb = frs(out, fn, i->b, 1);
    sd = frd(fn, i->dst, 0);
    if (strcmp(sb, sd) == 0 && strcmp(sa, sd) != 0) {
        fprintf(out, "\tfmove.x %s, %%fp1\n", sb);
        sb = fpregs[1];
    }
    if (strcmp(sa, sd) != 0)
        fprintf(out, "\tfmove.x %s, %s\n", sa, sd);
    fprintf(out, "\t%s %s, %s\n", mnem, sb, sd);
    fwd(out, fn, i->dst, sd);
}

static void
emit_funop(FILE *out, struct ir_func *fn, struct ir_insn *i,
           const char *mnem)
{
    const char *sa, *sd;

    sa = frs(out, fn, i->a, 0);
    sd = frd(fn, i->dst, 0);
    if (strcmp(sa, sd) != 0)
        fprintf(out, "\tfmove.x %s, %s\n", sa, sd);
    fprintf(out, "\t%s %s\n", mnem, sd);
    fwd(out, fn, i->dst, sd);
}

/****************************************************************
 * Per-instruction emission
 ****************************************************************/

/* Maximum arguments in one call. Mirrors the C front end's cap (lower.c:
   "too many arguments"); the two must agree, or a call the front end accepts
   would be rejected here. */
#define CF_MAX_ARGS 32

static int arg_temps[CF_MAX_ARGS];
static int arg_is_float[CF_MAX_ARGS];
static int arg_is_f32[CF_MAX_ARGS];
static int arg_is_i64[CF_MAX_ARGS];
static int narg;
static int label_prefix;
static int uses_floats;
static int fcmp_serial;
static int i64cmp_serial;

static void
emit_load(FILE *out, struct ir_func *fn, struct ir_insn *i,
          const char *sz, int sign_extend, int clear_first)
{
    const char *sa, *sd;

    sa = rs(out, fn, i->a, 0);
    sd = rd(fn, i->dst, 0);
    fprintf(out, "\tmovea.l %s, %%a0\n", sa);
    if (clear_first)
        fprintf(out, "\tmoveq #0, %s\n", sd);
    fprintf(out, "\tmove.%s (%%a0), %s\n", sz, sd);
    if (sign_extend) {
        if (sz[0] == 'b')
            fprintf(out, "\text.w %s\n", sd);
        fprintf(out, "\text.l %s\n", sd);
    }
    wd(out, fn, i->dst, sd);
}

static void
emit_store(FILE *out, struct ir_func *fn, struct ir_insn *i,
           const char *sz)
{
    const char *sa, *sb;

    sa = rs(out, fn, i->a, 0);
    sb = rs(out, fn, i->b, 1);
    fprintf(out, "\tmovea.l %s, %%a0\n", sa);
    fprintf(out, "\tmove.%s %s, (%%a0)\n", sz, sb);
}

static int
frame_size(struct ir_func *fn)
{
    return locals_size(fn) + fn->nspills * 4
           + fn->nfspills * 8 + fn->ni64spills * 8;
}

static void
emit_prologue(FILE *out, struct ir_func *fn)
{
    fprintf(out, "\tlink.w %%fp, #%d\n", -frame_size(fn));
    /* real ColdFire movem has no predecrement mode: reserve then store
     * through (%sp), the same final layout the 68k -(%sp) form produced */
    fprintf(out, "\tlea -24(%%sp), %%sp\n");
    fprintf(out, "\tmovem.l %%d2-%%d7, (%%sp)\n");
    if (uses_floats) {
        fprintf(out, "\tlea -48(%%sp), %%sp\n");
        fprintf(out, "\tfmove.d %%fp2, (%%sp)\n");
        fprintf(out, "\tfmove.d %%fp3, 8(%%sp)\n");
        fprintf(out, "\tfmove.d %%fp4, 16(%%sp)\n");
        fprintf(out, "\tfmove.d %%fp5, 24(%%sp)\n");
        fprintf(out, "\tfmove.d %%fp6, 32(%%sp)\n");
        fprintf(out, "\tfmove.d %%fp7, 40(%%sp)\n");
    }
}

static void
emit_epilogue_no_rts(FILE *out, struct ir_func *fn)
{
    int frame = frame_size(fn);

    if (uses_floats) {
        fprintf(out, "\tlea %d(%%fp), %%sp\n", -(frame + 72));
        fprintf(out, "\tfmove.d (%%sp), %%fp2\n");
        fprintf(out, "\tfmove.d 8(%%sp), %%fp3\n");
        fprintf(out, "\tfmove.d 16(%%sp), %%fp4\n");
        fprintf(out, "\tfmove.d 24(%%sp), %%fp5\n");
        fprintf(out, "\tfmove.d 32(%%sp), %%fp6\n");
        fprintf(out, "\tfmove.d 40(%%sp), %%fp7\n");
        fprintf(out, "\tlea 48(%%sp), %%sp\n");
    } else {
        fprintf(out, "\tlea %d(%%fp), %%sp\n", -(frame + 24));
    }
    /* ColdFire movem: load through (%sp); unlk resets %sp from %fp, so
     * the 24-byte pop the old (%sp)+ form did is not needed */
    fprintf(out, "\tmovem.l (%%sp), %%d2-%%d7\n");
    fprintf(out, "\tunlk %%fp\n");
}

static void
emit_epilogue(FILE *out, struct ir_func *fn)
{
    emit_epilogue_no_rts(out, fn);
    fprintf(out, "\trts\n");
}

static void
emit_call_flush(FILE *out, struct ir_func *fn, struct ir_insn *i,
                int indirect)
{
    int k;
    int arg_bytes = 0;
    int float_ret = (i->op == IR_FCALL || i->op == IR_FCALLI);

    for (k = narg - 1; k >= 0; k--) {
        if (arg_is_float[k]) {
            const char *sa = frs(out, fn, arg_temps[k], 0);
            if (arg_is_f32[k]) {
                /* single format at the slot base (the callee reads fmove.s);
                 * reserve the full 8-byte arg slot */
                fprintf(out, "\tlea -8(%%sp), %%sp\n");
                fprintf(out, "\tfmove.s %s, (%%sp)\n", sa);
            } else {
                fprintf(out, "\tfmove.d %s, -(%%sp)\n", sa);
            }
            arg_bytes += 8;
        } else if (arg_is_i64[k]) {
            int hi, lo;
            i64_rs(out, fn, arg_temps[k], &hi, &lo);
            fprintf(out, "\tmove.l %s, -(%%sp)\n", dregs[lo]);
            fprintf(out, "\tmove.l %s, -(%%sp)\n", dregs[hi]);
            arg_bytes += 8;
        } else {
            const char *sa = rs(out, fn, arg_temps[k], 0);
            fprintf(out, "\tmove.l %s, -(%%sp)\n", sa);
            arg_bytes += 4;
        }
    }
    if (indirect) {
        const char *sa = rs(out, fn, i->a, 0);
        fprintf(out, "\tmovea.l %s, %%a0\n", sa);
        fprintf(out, "\tjsr (%%a0)\n");
    } else {
        fprintf(out, "\tjsr %s\n", i->sym);
    }
    if (arg_bytes > 0)
        fprintf(out, "\tlea %d(%%sp), %%sp\n", arg_bytes);
    narg = 0;

    if (i->dst >= 0) {
        if (float_ret) {
            const char *sd = frd(fn, i->dst, 0);
            if (strcmp(sd, "%fp0") != 0)
                fprintf(out, "\tfmove.x %%fp0, %s\n", sd);
            fwd(out, fn, i->dst, sd);
        } else if (i->op == IR_CALL64 || i->op == IR_CALLI64) {
            int dhi, dlo;
            i64_rd(fn, i->dst, &dhi, &dlo);
            if (dhi != 0)
                fprintf(out, "\tmove.l %%d0, %s\n", dregs[dhi]);
            if (dlo != 1)
                fprintf(out, "\tmove.l %%d1, %s\n", dregs[dlo]);
            i64_wd(out, fn, i->dst, dhi, dlo);
        } else {
            const char *sd = rd(fn, i->dst, 0);
            if (strcmp(sd, "%d0") != 0)
                fprintf(out, "\tmove.l %%d0, %s\n", sd);
            wd(out, fn, i->dst, sd);
        }
    }
}

static void
emit_tailcall_flush(FILE *out, struct ir_func *fn, struct ir_insn *i,
                    int indirect)
{
    int k;

    {
        int off = 8;
        for (k = 0; k < narg; k++) {
            if (arg_is_float[k]) {
                const char *sa = frs(out, fn, arg_temps[k], 0);
                fprintf(out, "\tfmove.%c %s, %d(%%fp)\n",
                    arg_is_f32[k] ? 's' : 'd', sa, off);
                off += 8;
            } else {
                const char *sa = rs(out, fn, arg_temps[k], 0);
                fprintf(out, "\tmove.l %s, %d(%%fp)\n", sa, off);
                off += 4;
            }
        }
    }
    if (indirect) {
        const char *sa = rs(out, fn, i->a, 0);
        fprintf(out, "\tmovea.l %s, %%a1\n", sa);
    }
    narg = 0;
    emit_epilogue_no_rts(out, fn);
    if (indirect)
        fprintf(out, "\tjmp (%%a1)\n");
    else
        fprintf(out, "\tjmp %s\n", i->sym);
}

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
        if (i->imm >= -128 && i->imm <= 127)
            fprintf(out, "\tmoveq #%ld, %s\n", i->imm, sd);
        else
            fprintf(out, "\tmove.l #%ld, %s\n", i->imm, sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_LEA: {
        const char *sd = rd(fn, i->dst, 0);
        fprintf(out, "\tlea %s, %%a0\n", i->sym);
        fprintf(out, "\tmove.l %%a0, %s\n", sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_ADL: {
        const char *sd = rd(fn, i->dst, 0);
        int off = slot_offset(fn, i->slot);
        fprintf(out, "\tlea %d(%%fp), %%a0\n", off);
        fprintf(out, "\tmove.l %%a0, %s\n", sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_MOV: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sd = rd(fn, i->dst, 0);
        if (strcmp(sa, sd) != 0)
            fprintf(out, "\tmove.l %s, %s\n", sa, sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_ADD:  emit_binop(out, fn, i, "add.l");  break;
    case IR_SUB:  emit_binop(out, fn, i, "sub.l");  break;
    case IR_MUL:  emit_binop(out, fn, i, "muls.l"); break;
    case IR_AND:  emit_binop(out, fn, i, "and.l");  break;
    case IR_OR:   emit_binop(out, fn, i, "or.l");   break;
    case IR_XOR:  emit_binop(out, fn, i, "eor.l");  break;
    case IR_SHL:  emit_binop(out, fn, i, "lsl.l");  break;
    case IR_SHRS: emit_binop(out, fn, i, "asr.l");  break;
    case IR_SHRU: emit_binop(out, fn, i, "lsr.l");  break;

    case IR_DIVS: emit_binop(out, fn, i, "divs.l"); break;
    case IR_DIVU: emit_binop(out, fn, i, "divu.l"); break;

    case IR_MODS:
    case IR_MODU: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sb = rs(out, fn, i->b, 1);
        const char *sd = rd(fn, i->dst, 0);
        const char *divop = (i->op == IR_MODS) ? "divs.l" : "divu.l";
        if (strcmp(sb, "%d1") != 0) {
            fprintf(out, "\tmove.l %s, %%d1\n", sb);
            sb = "%d1";
        }
        fprintf(out, "\tmove.l %s, -(%%sp)\n", sa);
        fprintf(out, "\tmove.l %s, %%d0\n", sa);
        fprintf(out, "\t%s %s, %%d0\n", divop, sb);
        fprintf(out, "\tmuls.l %s, %%d0\n", sb);
        fprintf(out, "\tmove.l (%%sp)+, %s\n", sd);
        fprintf(out, "\tsub.l %%d0, %s\n", sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_NEG: emit_unop(out, fn, i, "neg.l"); break;
    case IR_NOT: emit_unop(out, fn, i, "not.l"); break;

    case IR_LB:  emit_load(out, fn, i, "b", 0, 1); break;
    case IR_LBS: emit_load(out, fn, i, "b", 1, 0); break;
    case IR_LH:  emit_load(out, fn, i, "w", 0, 1); break;
    case IR_LHS: emit_load(out, fn, i, "w", 1, 0); break;
    case IR_LW:  emit_load(out, fn, i, "l", 0, 0); break;

    case IR_SB: emit_store(out, fn, i, "b"); break;
    case IR_SH: emit_store(out, fn, i, "w"); break;
    case IR_SW: emit_store(out, fn, i, "l"); break;

    case IR_LDL: {
        const char *sd = rd(fn, i->dst, 0);
        int off = slot_offset(fn, i->slot);
        fprintf(out, "\tmove.l %d(%%fp), %s\n", off, sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_STL: {
        const char *sa = rs(out, fn, i->a, 0);
        int off = slot_offset(fn, i->slot);
        fprintf(out, "\tmove.l %s, %d(%%fp)\n", sa, off);
        break;
    }

    case IR_ALLOCA: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sd = rd(fn, i->dst, 0);
        if (strcmp(sa, "%d0") != 0)
            fprintf(out, "\tmove.l %s, %%d0\n", sa);
        fprintf(out, "\taddq.l #3, %%d0\n");
        fprintf(out, "\tand.l #-4, %%d0\n");
        fprintf(out, "\tsuba.l %%d0, %%sp\n");
        fprintf(out, "\tmovea.l %%sp, %%a0\n");
        fprintf(out, "\tmove.l %%a0, %s\n", sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CMPEQ:  emit_cmp(out, fn, i, "seq"); break;
    case IR_CMPNE:  emit_cmp(out, fn, i, "sne"); break;
    case IR_CMPLTS: emit_cmp(out, fn, i, "slt"); break;
    case IR_CMPLES: emit_cmp(out, fn, i, "sle"); break;
    case IR_CMPGTS: emit_cmp(out, fn, i, "sgt"); break;
    case IR_CMPGES: emit_cmp(out, fn, i, "sge"); break;
    case IR_CMPLTU: emit_cmp(out, fn, i, "scs"); break;
    case IR_CMPLEU: emit_cmp(out, fn, i, "sls"); break;
    case IR_CMPGTU: emit_cmp(out, fn, i, "shi"); break;
    case IR_CMPGEU: emit_cmp(out, fn, i, "scc"); break;

    case IR_JMP:
        fprintf(out, "\tbra .L%d_%d\n", label_prefix, i->label);
        break;
    case IR_BZ: {
        const char *sa = rs(out, fn, i->a, 0);
        fprintf(out, "\ttst.l %s\n\tbeq .L%d_%d\n",
            sa, label_prefix, i->label);
        break;
    }
    case IR_BNZ: {
        const char *sa = rs(out, fn, i->a, 0);
        fprintf(out, "\ttst.l %s\n\tbne .L%d_%d\n",
            sa, label_prefix, i->label);
        break;
    }
    case IR_LABEL:
        fprintf(out, ".L%d_%d:\n", label_prefix, i->label);
        break;

    case IR_ARG:
        if (narg >= CF_MAX_ARGS)
            die("cf_emit: too many args");
        arg_is_float[narg] = 0;
        arg_is_f32[narg] = 0;
        arg_is_i64[narg] = 0;
        arg_temps[narg++] = i->a;
        break;
    case IR_FARG:
        if (narg >= CF_MAX_ARGS)
            die("cf_emit: too many args");
        arg_is_float[narg] = 1;
        arg_is_f32[narg] = (i->imm == FWIDTH_F32);
        arg_is_i64[narg] = 0;
        arg_temps[narg++] = i->a;
        break;
    case IR_CALL:
        emit_call_flush(out, fn, i, 0);
        break;
    case IR_CALLI:
        emit_call_flush(out, fn, i, 1);
        break;
    case IR_FCALL:
        emit_call_flush(out, fn, i, 0);
        break;
    case IR_FCALLI:
        emit_call_flush(out, fn, i, 1);
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
        if (strcmp(sa, "%d0") != 0)
            fprintf(out, "\tmove.l %s, %%d0\n", sa);
        emit_epilogue(out, fn);
        break;
    }
    case IR_FRETV: {
        const char *sa = frs(out, fn, i->a, 0);
        if (strcmp(sa, "%fp0") != 0)
            fprintf(out, "\tfmove.x %s, %%fp0\n", sa);
        emit_epilogue(out, fn);
        break;
    }

    case IR_MARK: {
        const char *sd = rd(fn, i->dst, 0);
        int off = slot_offset(fn, i->slot);
        fprintf(out, "\tmove.l %%fp, %d(%%fp)\n", off);
        fprintf(out, "\tlea 0(%%sp), %%a0\n");
        fprintf(out, "\tmove.l %%a0, %d(%%fp)\n", off + 4);
        fprintf(out, "\tlea .Lmark%d_%d, %%a0\n",
            label_prefix, i->label);
        fprintf(out, "\tmove.l %%a0, %d(%%fp)\n", off + 8);
        fprintf(out, "\tlea %d(%%fp), %%a0\n", off);
        fprintf(out, "\tmove.l %%a0, __cont_mark_sp\n");
        /* stash the continuation-arena high-water mark (first entry only,
         * before the re-entry label) for IR_CONT_UNWIND to restore */
        fprintf(out, "\tmove.l __cont_arena_ptr, %%d0\n");
        fprintf(out, "\tmove.l %%d0, %d(%%fp)\n", off + 12);
        fprintf(out, "\tmoveq #0, %%d0\n");
        fprintf(out, ".Lmark%d_%d:\n", label_prefix, i->label);
        if (strcmp(sd, "%d0") != 0)
            fprintf(out, "\tmove.l %%d0, %s\n", sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CONT_UNWIND: {
        /* restore the continuation arena to the mark-time high-water mark,
         * reclaiming every buffer captured within the closing reset extent */
        int off = slot_offset(fn, i->slot);
        fprintf(out, "\tmove.l %d(%%fp), %%d0\n", off + 12);
        fprintf(out, "\tmove.l %%d0, __cont_arena_ptr\n");
        break;
    }

    case IR_CAPTURE: {
        const char *sd = rd(fn, i->dst, 0);
        /* ColdFire-legal movem (control mode); the stack layout the
         * capture/resume protocol sees is unchanged */
        fprintf(out, "\tlea -24(%%sp), %%sp\n");
        fprintf(out, "\tmovem.l %%d2-%%d7, (%%sp)\n");
        fprintf(out, "\tjsr __cont_capture\n");
        fprintf(out, "\tmovem.l (%%sp), %%d2-%%d7\n");
        fprintf(out, "\tlea 24(%%sp), %%sp\n");
        if (strcmp(sd, "%d0") != 0)
            fprintf(out, "\tmove.l %%d0, %s\n", sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_RESUME: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sb = rs(out, fn, i->b, 1);
        fprintf(out, "\tmove.l %s, -(%%sp)\n", sb);
        fprintf(out, "\tmove.l %s, -(%%sp)\n", sa);
        fprintf(out, "\tjsr __cont_resume\n");
        /* __cont_resume does not return here */
        break;
    }

    case IR_FADD: emit_fbinop(out, fn, i, "fadd.x"); break;
    case IR_FSUB: emit_fbinop(out, fn, i, "fsub.x"); break;
    case IR_FMUL: emit_fbinop(out, fn, i, "fmul.x"); break;
    case IR_FDIV: emit_fbinop(out, fn, i, "fdiv.x"); break;
    case IR_FNEG: emit_funop(out, fn, i, "fneg.x"); break;
    case IR_FABS: emit_funop(out, fn, i, "fabs.x"); break;

    case IR_FCMPEQ:
    case IR_FCMPLT:
    case IR_FCMPLE: {
        const char *sa, *sb, *sd;
        const char *fbcc;
        int id = fcmp_serial++;

        sa = frs(out, fn, i->a, 0);
        sb = frs(out, fn, i->b, 1);
        sd = rd(fn, i->dst, 0);
        if (i->op == IR_FCMPEQ)
            fbcc = "fbeq";
        else if (i->op == IR_FCMPLT)
            fbcc = "fblt";
        else
            fbcc = "fble";
        fprintf(out, "\tfcmp.x %s, %s\n", sb, sa);
        fprintf(out, "\tmoveq #1, %s\n", sd);
        fprintf(out, "\t%s .Lfc%d\n", fbcc, id);
        fprintf(out, "\tmoveq #0, %s\n", sd);
        fprintf(out, ".Lfc%d:\n", id);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_ITOF: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sd = frd(fn, i->dst, 0);
        fprintf(out, "\tfmove.l %s, %s\n", sa, sd);
        fwd(out, fn, i->dst, sd);
        break;
    }

    case IR_FTOI: {
        const char *sa = frs(out, fn, i->a, 0);
        const char *sd = rd(fn, i->dst, 0);
        fprintf(out, "\tfintrz.x %s, %%fp0\n", sa);
        fprintf(out, "\tfmove.l %%fp0, %s\n", sd);
        wd(out, fn, i->dst, sd);
        break;
    }

    /* IR_F32 on ColdFire: the FPU computes in wide registers (no single-
     * rounded fsadd/fsmul), so single precision is storage-only, rounded at
     * fmove.s.  single -> double is an exact widening copy; double -> single
     * rounds through a single-precision stack round-trip. */
    case IR_F32TOF64: {
        const char *sa = frs(out, fn, i->a, 0);
        const char *sd = frd(fn, i->dst, 0);
        if (strcmp(sa, sd) != 0)
            fprintf(out, "\tfmove.x %s, %s\n", sa, sd);
        fwd(out, fn, i->dst, sd);
        break;
    }

    case IR_F64TOF32: {
        const char *sa = frs(out, fn, i->a, 0);
        const char *sd = frd(fn, i->dst, 0);
        fprintf(out, "\tfmove.s %s, -(%%sp)\n", sa);   /* round to single */
        fprintf(out, "\tfmove.s (%%sp)+, %s\n", sd);
        fwd(out, fn, i->dst, sd);
        break;
    }

    case IR_FLS: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sd = frd(fn, i->dst, 0);
        fprintf(out, "\tmovea.l %s, %%a0\n", sa);
        fprintf(out, "\tfmove.s (%%a0), %s\n", sd);
        fwd(out, fn, i->dst, sd);
        break;
    }

    case IR_FLD: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sd = frd(fn, i->dst, 0);
        fprintf(out, "\tmovea.l %s, %%a0\n", sa);
        fprintf(out, "\tfmove.%c (%%a0), %s\n",
            i->imm == FWIDTH_F32 ? 's' : 'd', sd);
        fwd(out, fn, i->dst, sd);
        break;
    }

    case IR_FSS: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sb = frs(out, fn, i->b, 0);
        fprintf(out, "\tmovea.l %s, %%a0\n", sa);
        fprintf(out, "\tfmove.s %s, (%%a0)\n", sb);
        break;
    }

    case IR_FSD: {
        const char *sa = rs(out, fn, i->a, 0);
        const char *sb = frs(out, fn, i->b, 0);
        fprintf(out, "\tmovea.l %s, %%a0\n", sa);
        fprintf(out, "\tfmove.%c %s, (%%a0)\n",
            i->imm == FWIDTH_F32 ? 's' : 'd', sb);
        break;
    }

    case IR_FLH: {
        /* half in memory -> F64 reg, via __skj_extendhfsf then fmove.s.
         * The helper is integer-only (no fp regs), so fp2-fp7 survive. */
        const char *sa = rs(out, fn, i->a, 0);
        const char *sd = frd(fn, i->dst, 0);
        fprintf(out, "\tmovea.l %s, %%a0\n", sa);
        fprintf(out, "\tmoveq #0, %%d0\n");
        fprintf(out, "\tmove.w (%%a0), %%d0\n");     /* zero-extend the 16 bits */
        fprintf(out, "\tmove.l %%d0, -(%%sp)\n");
        fprintf(out, "\tjsr __skj_extendhfsf\n");     /* d0 = single bits */
        fprintf(out, "\taddq.l #4, %%sp\n");
        fprintf(out, "\tmove.l %%d0, -(%%sp)\n");
        fprintf(out, "\tfmove.s (%%sp), %s\n", sd);   /* widen single -> double */
        fprintf(out, "\taddq.l #4, %%sp\n");
        fwd(out, fn, i->dst, sd);
        break;
    }

    case IR_FSH: {
        /* F64 reg -> half in memory, via fmove.s then __skj_truncsfhf. */
        const char *sa = rs(out, fn, i->a, 0);
        const char *sb = frs(out, fn, i->b, 0);
        fprintf(out, "\tfmove.s %s, -(%%sp)\n", sb);  /* round double -> single */
        fprintf(out, "\tmove.l (%%sp)+, %%d0\n");
        fprintf(out, "\tmove.l %%d0, -(%%sp)\n");
        fprintf(out, "\tjsr __skj_truncsfhf\n");       /* d0 = half bits */
        fprintf(out, "\taddq.l #4, %%sp\n");
        fprintf(out, "\tmovea.l %s, %%a0\n", sa);
        fprintf(out, "\tmove.w %%d0, (%%a0)\n");
        break;
    }

    case IR_FLDL: {
        const char *sd = frd(fn, i->dst, 0);
        int off = slot_offset(fn, i->slot);
        fprintf(out, "\tfmove.%c %d(%%fp), %s\n",
            i->imm == FWIDTH_F32 ? 's' : 'd', off, sd);
        fwd(out, fn, i->dst, sd);
        break;
    }

    case IR_FSTL: {
        const char *sa = frs(out, fn, i->a, 0);
        int off = slot_offset(fn, i->slot);
        fprintf(out, "\tfmove.%c %s, %d(%%fp)\n",
            i->imm == FWIDTH_F32 ? 's' : 'd', sa, off);
        break;
    }

    case IR_FUNC:
    case IR_ENDF:
        break;

    /* ---- I64 opcodes ---- */

    case IR_LIC64: {
        int dhi, dlo;
        int64_t val = (int64_t)i->imm;
        uint32_t hi = (uint32_t)((uint64_t)val >> 32);
        uint32_t lo = (uint32_t)(val & 0xFFFFFFFF);

        i64_rd(fn, i->dst, &dhi, &dlo);
        if ((int32_t)hi >= -128 && (int32_t)hi <= 127)
            fprintf(out, "\tmoveq #%d, %s\n", (int32_t)hi, dregs[dhi]);
        else
            fprintf(out, "\tmove.l #0x%x, %s\n", hi, dregs[dhi]);
        if ((int32_t)lo >= -128 && (int32_t)lo <= 127)
            fprintf(out, "\tmoveq #%d, %s\n", (int32_t)lo, dregs[dlo]);
        else
            fprintf(out, "\tmove.l #0x%x, %s\n", lo, dregs[dlo]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_ADD64: {
        int ahi, alo, bhi, blo, dhi, dlo;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        i64_rd(fn, i->dst, &dhi, &dlo);
        if (dlo != alo)
            fprintf(out, "\tmove.l %s, %s\n", dregs[alo], dregs[dlo]);
        if (dhi != ahi)
            fprintf(out, "\tmove.l %s, %s\n", dregs[ahi], dregs[dhi]);
        fprintf(out, "\tadd.l %s, %s\n", dregs[blo], dregs[dlo]);
        fprintf(out, "\taddx.l %s, %s\n", dregs[bhi], dregs[dhi]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_SUB64: {
        int ahi, alo, bhi, blo, dhi, dlo;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        i64_rd(fn, i->dst, &dhi, &dlo);
        if (dlo != alo)
            fprintf(out, "\tmove.l %s, %s\n", dregs[alo], dregs[dlo]);
        if (dhi != ahi)
            fprintf(out, "\tmove.l %s, %s\n", dregs[ahi], dregs[dhi]);
        fprintf(out, "\tsub.l %s, %s\n", dregs[blo], dregs[dlo]);
        fprintf(out, "\tsubx.l %s, %s\n", dregs[bhi], dregs[dhi]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_MUL64: {
        int ahi, alo, bhi, blo, dhi, dlo;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        i64_rd(fn, i->dst, &dhi, &dlo);
        /*
         * 64-bit multiply (low 64 bits of result):
         *   result_lo = a_lo * b_lo  (mulu.l gives 32-bit result)
         *   result_hi = a_lo * b_hi + a_hi * b_lo + hi(a_lo * b_lo)
         *
         * Use __muldi3 library call for correctness.
         */
        fprintf(out, "\tmove.l %s, -(%%sp)\n", dregs[blo]);
        fprintf(out, "\tmove.l %s, -(%%sp)\n", dregs[bhi]);
        fprintf(out, "\tmove.l %s, -(%%sp)\n", dregs[alo]);
        fprintf(out, "\tmove.l %s, -(%%sp)\n", dregs[ahi]);
        fprintf(out, "\tjsr __muldi3\n");
        fprintf(out, "\tlea 16(%%sp), %%sp\n");
        if (dhi != 0)
            fprintf(out, "\tmove.l %%d0, %s\n", dregs[dhi]);
        if (dlo != 1)
            fprintf(out, "\tmove.l %%d1, %s\n", dregs[dlo]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_AND64: {
        int ahi, alo, bhi, blo, dhi, dlo;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        i64_rd(fn, i->dst, &dhi, &dlo);
        if (dlo != alo)
            fprintf(out, "\tmove.l %s, %s\n", dregs[alo], dregs[dlo]);
        if (dhi != ahi)
            fprintf(out, "\tmove.l %s, %s\n", dregs[ahi], dregs[dhi]);
        fprintf(out, "\tand.l %s, %s\n", dregs[blo], dregs[dlo]);
        fprintf(out, "\tand.l %s, %s\n", dregs[bhi], dregs[dhi]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_OR64: {
        int ahi, alo, bhi, blo, dhi, dlo;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        i64_rd(fn, i->dst, &dhi, &dlo);
        if (dlo != alo)
            fprintf(out, "\tmove.l %s, %s\n", dregs[alo], dregs[dlo]);
        if (dhi != ahi)
            fprintf(out, "\tmove.l %s, %s\n", dregs[ahi], dregs[dhi]);
        fprintf(out, "\tor.l %s, %s\n", dregs[blo], dregs[dlo]);
        fprintf(out, "\tor.l %s, %s\n", dregs[bhi], dregs[dhi]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_XOR64: {
        int ahi, alo, bhi, blo, dhi, dlo;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        i64_rd(fn, i->dst, &dhi, &dlo);
        if (dlo != alo)
            fprintf(out, "\tmove.l %s, %s\n", dregs[alo], dregs[dlo]);
        if (dhi != ahi)
            fprintf(out, "\tmove.l %s, %s\n", dregs[ahi], dregs[dhi]);
        fprintf(out, "\teor.l %s, %s\n", dregs[blo], dregs[dlo]);
        fprintf(out, "\teor.l %s, %s\n", dregs[bhi], dregs[dhi]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_SHL64:
    case IR_SHRS64:
    case IR_SHRU64: {
        int ahi, alo, dhi, dlo;
        const char *sb;
        const char *func;

        i64_rs(out, fn, i->a, &ahi, &alo);
        sb = rs(out, fn, i->b, 1);
        i64_rd(fn, i->dst, &dhi, &dlo);
        if (i->op == IR_SHL64)
            func = "__ashldi3";
        else if (i->op == IR_SHRS64)
            func = "__ashrdi3";
        else
            func = "__lshrdi3";
        fprintf(out, "\tmove.l %s, -(%%sp)\n", sb);
        fprintf(out, "\tmove.l %s, -(%%sp)\n", dregs[alo]);
        fprintf(out, "\tmove.l %s, -(%%sp)\n", dregs[ahi]);
        fprintf(out, "\tjsr %s\n", func);
        fprintf(out, "\tlea 12(%%sp), %%sp\n");
        if (dhi != 0)
            fprintf(out, "\tmove.l %%d0, %s\n", dregs[dhi]);
        if (dlo != 1)
            fprintf(out, "\tmove.l %%d1, %s\n", dregs[dlo]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_NEG64: {
        int ahi, alo, dhi, dlo;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rd(fn, i->dst, &dhi, &dlo);
        if (dlo != alo)
            fprintf(out, "\tmove.l %s, %s\n", dregs[alo], dregs[dlo]);
        if (dhi != ahi)
            fprintf(out, "\tmove.l %s, %s\n", dregs[ahi], dregs[dhi]);
        fprintf(out, "\tneg.l %s\n", dregs[dlo]);
        fprintf(out, "\tnegx.l %s\n", dregs[dhi]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_CMP64EQ:
    case IR_CMP64NE: {
        int ahi, alo, bhi, blo;
        const char *sd;
        int id = i64cmp_serial++;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        sd = rd(fn, i->dst, 0);
        fprintf(out, "\tmoveq #0, %s\n", sd);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[bhi], dregs[ahi]);
        fprintf(out, "\tbne .Li64c%d\n", id);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[blo], dregs[alo]);
        fprintf(out, ".Li64c%d:\n", id);
        fprintf(out, "\t%s %s\n",
            i->op == IR_CMP64EQ ? "seq" : "sne", sd);
        fprintf(out, "\tand.l #1, %s\n", sd);      /* 0x00/0xFF -> 0/1, CF-legal */
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CMP64LTS: {
        int ahi, alo, bhi, blo;
        const char *sd;
        int id = i64cmp_serial++;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        sd = rd(fn, i->dst, 0);
        fprintf(out, "\tmoveq #0, %s\n", sd);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[bhi], dregs[ahi]);
        fprintf(out, "\tblt .Li64c%d_t\n", id);
        fprintf(out, "\tbgt .Li64c%d_d\n", id);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[blo], dregs[alo]);
        fprintf(out, "\tbcc .Li64c%d_d\n", id);
        fprintf(out, ".Li64c%d_t:\n", id);
        fprintf(out, "\tmoveq #1, %s\n", sd);
        fprintf(out, ".Li64c%d_d:\n", id);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CMP64LES: {
        int ahi, alo, bhi, blo;
        const char *sd;
        int id = i64cmp_serial++;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        sd = rd(fn, i->dst, 0);
        fprintf(out, "\tmoveq #0, %s\n", sd);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[bhi], dregs[ahi]);
        fprintf(out, "\tblt .Li64c%d_t\n", id);
        fprintf(out, "\tbgt .Li64c%d_d\n", id);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[blo], dregs[alo]);
        fprintf(out, "\tbhi .Li64c%d_d\n", id);
        fprintf(out, ".Li64c%d_t:\n", id);
        fprintf(out, "\tmoveq #1, %s\n", sd);
        fprintf(out, ".Li64c%d_d:\n", id);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CMP64GTS: {
        int ahi, alo, bhi, blo;
        const char *sd;
        int id = i64cmp_serial++;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        sd = rd(fn, i->dst, 0);
        fprintf(out, "\tmoveq #0, %s\n", sd);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[bhi], dregs[ahi]);
        fprintf(out, "\tbgt .Li64c%d_t\n", id);
        fprintf(out, "\tblt .Li64c%d_d\n", id);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[blo], dregs[alo]);
        fprintf(out, "\tbls .Li64c%d_d\n", id);
        fprintf(out, ".Li64c%d_t:\n", id);
        fprintf(out, "\tmoveq #1, %s\n", sd);
        fprintf(out, ".Li64c%d_d:\n", id);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CMP64GES: {
        int ahi, alo, bhi, blo;
        const char *sd;
        int id = i64cmp_serial++;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        sd = rd(fn, i->dst, 0);
        fprintf(out, "\tmoveq #0, %s\n", sd);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[bhi], dregs[ahi]);
        fprintf(out, "\tbgt .Li64c%d_t\n", id);
        fprintf(out, "\tblt .Li64c%d_d\n", id);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[blo], dregs[alo]);
        fprintf(out, "\tbcs .Li64c%d_d\n", id);
        fprintf(out, ".Li64c%d_t:\n", id);
        fprintf(out, "\tmoveq #1, %s\n", sd);
        fprintf(out, ".Li64c%d_d:\n", id);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CMP64LTU: {
        int ahi, alo, bhi, blo;
        const char *sd;
        int id = i64cmp_serial++;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        sd = rd(fn, i->dst, 0);
        fprintf(out, "\tmoveq #0, %s\n", sd);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[bhi], dregs[ahi]);
        fprintf(out, "\tbcs .Li64c%d_t\n", id);
        fprintf(out, "\tbhi .Li64c%d_d\n", id);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[blo], dregs[alo]);
        fprintf(out, "\tbcc .Li64c%d_d\n", id);
        fprintf(out, ".Li64c%d_t:\n", id);
        fprintf(out, "\tmoveq #1, %s\n", sd);
        fprintf(out, ".Li64c%d_d:\n", id);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CMP64LEU: {
        int ahi, alo, bhi, blo;
        const char *sd;
        int id = i64cmp_serial++;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        sd = rd(fn, i->dst, 0);
        fprintf(out, "\tmoveq #0, %s\n", sd);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[bhi], dregs[ahi]);
        fprintf(out, "\tbcs .Li64c%d_t\n", id);
        fprintf(out, "\tbhi .Li64c%d_d\n", id);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[blo], dregs[alo]);
        fprintf(out, "\tbhi .Li64c%d_d\n", id);
        fprintf(out, ".Li64c%d_t:\n", id);
        fprintf(out, "\tmoveq #1, %s\n", sd);
        fprintf(out, ".Li64c%d_d:\n", id);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CMP64GTU: {
        int ahi, alo, bhi, blo;
        const char *sd;
        int id = i64cmp_serial++;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        sd = rd(fn, i->dst, 0);
        fprintf(out, "\tmoveq #0, %s\n", sd);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[bhi], dregs[ahi]);
        fprintf(out, "\tbhi .Li64c%d_t\n", id);
        fprintf(out, "\tbcs .Li64c%d_d\n", id);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[blo], dregs[alo]);
        fprintf(out, "\tbls .Li64c%d_d\n", id);
        fprintf(out, ".Li64c%d_t:\n", id);
        fprintf(out, "\tmoveq #1, %s\n", sd);
        fprintf(out, ".Li64c%d_d:\n", id);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_CMP64GEU: {
        int ahi, alo, bhi, blo;
        const char *sd;
        int id = i64cmp_serial++;

        i64_rs(out, fn, i->a, &ahi, &alo);
        i64_rs(out, fn, i->b, &bhi, &blo);
        sd = rd(fn, i->dst, 0);
        fprintf(out, "\tmoveq #0, %s\n", sd);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[bhi], dregs[ahi]);
        fprintf(out, "\tbhi .Li64c%d_t\n", id);
        fprintf(out, "\tbcs .Li64c%d_d\n", id);
        fprintf(out, "\tcmp.l %s, %s\n", dregs[blo], dregs[alo]);
        fprintf(out, "\tbcs .Li64c%d_d\n", id);
        fprintf(out, ".Li64c%d_t:\n", id);
        fprintf(out, "\tmoveq #1, %s\n", sd);
        fprintf(out, ".Li64c%d_d:\n", id);
        wd(out, fn, i->dst, sd);
        break;
    }

    case IR_LD64: {
        int dhi, dlo;
        const char *sa;

        sa = rs(out, fn, i->a, 0);
        i64_rd(fn, i->dst, &dhi, &dlo);
        fprintf(out, "\tmovea.l %s, %%a0\n", sa);
        fprintf(out, "\tmove.l (%%a0), %s\n", dregs[dhi]);
        fprintf(out, "\tmove.l 4(%%a0), %s\n", dregs[dlo]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_ST64: {
        int bhi, blo;
        const char *sa;

        sa = rs(out, fn, i->a, 0);
        i64_rs(out, fn, i->b, &bhi, &blo);
        fprintf(out, "\tmovea.l %s, %%a0\n", sa);
        fprintf(out, "\tmove.l %s, (%%a0)\n", dregs[bhi]);
        fprintf(out, "\tmove.l %s, 4(%%a0)\n", dregs[blo]);
        break;
    }

    case IR_LDL64: {
        int dhi, dlo;
        int off = slot_offset(fn, i->slot);

        i64_rd(fn, i->dst, &dhi, &dlo);
        fprintf(out, "\tmove.l %d(%%fp), %s\n", off, dregs[dhi]);
        fprintf(out, "\tmove.l %d(%%fp), %s\n", off + 4, dregs[dlo]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_STL64: {
        int ahi, alo;
        int off = slot_offset(fn, i->slot);

        i64_rs(out, fn, i->a, &ahi, &alo);
        fprintf(out, "\tmove.l %s, %d(%%fp)\n", dregs[ahi], off);
        fprintf(out, "\tmove.l %s, %d(%%fp)\n", dregs[alo], off + 4);
        break;
    }

    case IR_SEXT64: {
        int dhi, dlo;
        const char *sa;

        sa = rs(out, fn, i->a, 0);
        i64_rd(fn, i->dst, &dhi, &dlo);
        fprintf(out, "\tmove.l %s, %s\n", sa, dregs[dlo]);
        fprintf(out, "\tmove.l %s, %s\n", sa, dregs[dhi]);
        fprintf(out, "\tmoveq #31, %%d1\n");
        fprintf(out, "\tasr.l %%d1, %s\n", dregs[dhi]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_ZEXT64: {
        int dhi, dlo;
        const char *sa;

        sa = rs(out, fn, i->a, 0);
        i64_rd(fn, i->dst, &dhi, &dlo);
        fprintf(out, "\tmove.l %s, %s\n", sa, dregs[dlo]);
        fprintf(out, "\tmoveq #0, %s\n", dregs[dhi]);
        i64_wd(out, fn, i->dst, dhi, dlo);
        break;
    }

    case IR_TRUNC64: {
        int ahi, alo;
        const char *sd;

        i64_rs(out, fn, i->a, &ahi, &alo);
        sd = rd(fn, i->dst, 0);
        if (strcmp(dregs[alo], sd) != 0)
            fprintf(out, "\tmove.l %s, %s\n", dregs[alo], sd);
        wd(out, fn, i->dst, sd);
        (void)ahi;
        break;
    }

    case IR_ARG64:
        if (narg >= CF_MAX_ARGS)
            die("cf_emit: too many args");
        arg_is_float[narg] = 0;
        arg_is_i64[narg] = 1;
        arg_temps[narg++] = i->a;
        break;

    case IR_CALL64:
        emit_call_flush(out, fn, i, 0);
        break;
    case IR_CALLI64:
        emit_call_flush(out, fn, i, 1);
        break;

    case IR_RETV64: {
        int ahi, alo;

        i64_rs(out, fn, i->a, &ahi, &alo);
        if (ahi != 0)
            fprintf(out, "\tmove.l %s, %%d0\n", dregs[ahi]);
        if (alo != 1)
            fprintf(out, "\tmove.l %s, %%d1\n", dregs[alo]);
        emit_epilogue(out, fn);
        break;
    }

    default:
        die("cf_emit: unhandled op %d", i->op);
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
    uses_floats = func_uses_fpregs(fn);

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
        last->op != IR_RETV64 && last->op != IR_FRETV &&
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
   sized value (8-byte high word first, big-endian m68k), pad to the full size */
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
            fprintf(out, "\t.long %s\n", it->sym);
        else if (it->size == 8) {
            uint64_t b = (uint64_t)it->ival;
            fprintf(out, "\t.long 0x%08x\n", (unsigned)(b >> 32));
            fprintf(out, "\t.long 0x%08x\n", (unsigned)(b & 0xFFFFFFFF));
        } else if (it->size == 2)
            fprintf(out, "\t.short 0x%04x\n", (unsigned)(it->ival & 0xFFFF));
        else if (it->size == 1)
            fprintf(out, "\t.byte 0x%02x\n", (unsigned)(it->ival & 0xFF));
        else
            fprintf(out, "\t.long 0x%08x\n", (unsigned)it->ival);
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
        case IR_F64: elsz = 8; break;
        case IR_I64: elsz = 8; break;
        default:     elsz = 4; break;
        }

        {
            /* m68k aligns to 4; an _Alignas request (g->align) may raise it.
               On m68k (both m68k-linux-gnu-as and skj-as) the .align operand
               is a byte count, not a power-of-two exponent (RISC-V's is the
               exponent), so emit the alignment in bytes. */
            int alb = 4;
            if (g->align > alb)
                alb = g->align;
            fprintf(out, "\t.align %d\n", alb);
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
                    fprintf(out, "\t.long %s\n",
                        g->init_syms[k]);
                else if (g->base_type == IR_F64 ||
                     g->base_type == IR_I64) {
                    uint64_t bits = (uint64_t)g->init_ivals[k];
                    fprintf(out, "\t.long 0x%08x\n",
                        (unsigned)(bits >> 32));
                    fprintf(out, "\t.long 0x%08x\n",
                        (unsigned)(bits & 0xFFFFFFFF));
                } else if (elsz == 1)
                    fprintf(out, "\t.byte %" PRId64 "\n",
                        g->init_ivals[k]);
                else if (elsz == 2)
                    fprintf(out, "\t.short %" PRId64 "\n",
                        g->init_ivals[k]);
                else
                    fprintf(out, "\t.long %" PRId64 "\n",
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

    fputs("| generated ColdFire / m68k assembly\n", out);
    for (fn = prog->funcs; fn; fn = fn->next)
        emit_function(out, fn);
    emit_globals(out, prog);
}
