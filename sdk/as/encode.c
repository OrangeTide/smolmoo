/* encode.c : ColdFire instruction encoder */

#include "as.h"

#include <string.h>

/****************************************************************
 * Effective address encoding
 ****************************************************************/

static int
ea_mode_reg(struct operand *op, int *mode, int *reg)
{
    switch (op->type) {
    case OP_DREG:
        *mode = 0;
        *reg = op->reg;
        return 0;
    case OP_AREG:
        *mode = 1;
        *reg = op->reg;
        return 0;
    case OP_IND:
        *mode = 2;
        *reg = op->reg;
        return 0;
    case OP_POSTINC:
        *mode = 3;
        *reg = op->reg;
        return 0;
    case OP_PREDEC:
        *mode = 4;
        *reg = op->reg;
        return 0;
    case OP_DISP:
        *mode = 5;
        *reg = op->reg;
        return 0;
    case OP_ABS:
        *mode = 7;
        *reg = 1;
        return 0;
    case OP_IMM:
        *mode = 7;
        *reg = 4;
        return 0;
    default:
        return -1;
    }
}

static int
ea_ext_size(struct operand *op)
{
    switch (op->type) {
    case OP_DISP:
        return 2;
    case OP_ABS:
        return 4;
    case OP_IMM:
        return 4;
    default:
        return 0;
    }
}

static void
ea_emit_ext(struct assembler *a, struct operand *op)
{
    struct section *s = &a->sections[a->cur_section];

    switch (op->type) {
    case OP_DISP:
        sec_emit16(s, (uint16_t)(int16_t)op->imm);
        break;
    case OP_ABS:
        if (op->sym) {
            int idx = sym_lookup(&a->st, op->sym);
            if (idx < 0)
                idx = sym_add(&a->st, op->sym);
            sec_add_reloc(s, s->len, idx, 0);
        }
        sec_emit32(s, (uint32_t)op->imm);
        break;
    case OP_IMM:
        if (op->sym) {
            int idx = sym_lookup(&a->st, op->sym);
            if (idx < 0)
                idx = sym_add(&a->st, op->sym);
            sec_add_reloc(s, s->len, idx, (int32_t)op->imm);
            sec_emit32(s, 0);
        } else {
            sec_emit32(s, (uint32_t)op->imm);
        }
        break;
    default:
        break;
    }
}

static uint16_t
ea_bits(int mode, int reg)
{
    return (uint16_t)((mode << 3) | reg);
}

/****************************************************************
 * Size suffix → code
 ****************************************************************/

static int
move_size_code(int size)
{
    switch (size) {
    case 1: return 1;
    case 2: return 3;
    case 4: return 2;
    default: return 2;
    }
}

/****************************************************************
 * Instruction encoding
 ****************************************************************/

static int
encode_move(struct assembler *a, int size,
            struct operand *src, struct operand *dst, int emit)
{
    int sm, sr, dm, dr;
    int total;

    if (ea_mode_reg(src, &sm, &sr) < 0) return -1;
    if (ea_mode_reg(dst, &dm, &dr) < 0) return -1;

    total = 2 + ea_ext_size(src) + ea_ext_size(dst);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        uint16_t w = (uint16_t)(move_size_code(size) << 12)
                   | (uint16_t)(dr << 9)
                   | (uint16_t)(dm << 6)
                   | ea_bits(sm, sr);
        sec_emit16(s, w);
        ea_emit_ext(a, src);
        ea_emit_ext(a, dst);
    }
    return total;
}

static int
encode_movea(struct assembler *a, int size,
             struct operand *src, struct operand *dst, int emit)
{
    int sm, sr;
    int total;

    if (dst->type != OP_AREG) return -1;
    if (ea_mode_reg(src, &sm, &sr) < 0) return -1;

    total = 2 + ea_ext_size(src);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        uint16_t w = (uint16_t)(move_size_code(size) << 12)
                   | (uint16_t)(dst->reg << 9)
                   | (uint16_t)(1 << 6)
                   | ea_bits(sm, sr);
        sec_emit16(s, w);
        ea_emit_ext(a, src);
    }
    return total;
}

static int
encode_moveq(struct assembler *a, struct operand *src, struct operand *dst,
             int emit)
{
    if (src->type != OP_IMM || dst->type != OP_DREG) return -1;

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        uint16_t w = (uint16_t)(0x7000
                   | (dst->reg << 9)
                   | ((uint8_t)src->imm));
        sec_emit16(s, w);
    }
    return 2;
}

static int
encode_addsuba(struct assembler *a, uint16_t base, int size,
               struct operand *src, struct operand *dst, int emit)
{
    int sm, sr;
    int total;
    int opmode;

    if (dst->type != OP_AREG) return -1;
    if (ea_mode_reg(src, &sm, &sr) < 0) return -1;

    opmode = (size == 2) ? 3 : 7;
    total = 2 + ea_ext_size(src);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        uint16_t w = (uint16_t)(base
                   | (dst->reg << 9)
                   | (opmode << 6)
                   | ea_bits(sm, sr));
        sec_emit16(s, w);
        ea_emit_ext(a, src);
    }
    return total;
}

static uint16_t
reverse_mask16(uint16_t m)
{
    uint16_t r = 0;
    int k;
    for (k = 0; k < 16; k++)
        if (m & (1 << k))
            r |= (uint16_t)(1 << (15 - k));
    return r;
}

static int
encode_movem(struct assembler *a, struct operand *op1, struct operand *op2,
             int emit)
{
    struct section *s = &a->sections[a->cur_section];

    if (op1->type == OP_REGLIST && op2->type == OP_PREDEC) {
        if (emit) {
            int dm, dr;
            ea_mode_reg(op2, &dm, &dr);
            sec_emit16(s, (uint16_t)(0x48C0 | ea_bits(dm, dr)));
            sec_emit16(s, reverse_mask16(op1->regmask));
        }
        return 4;
    }
    if (op1->type == OP_POSTINC && op2->type == OP_REGLIST) {
        if (emit) {
            int sm, sr;
            ea_mode_reg(op1, &sm, &sr);
            sec_emit16(s, (uint16_t)(0x4CC0 | ea_bits(sm, sr)));
            sec_emit16(s, op2->regmask);
        }
        return 4;
    }
    /* The control modes, (An) and d16(An), in both directions: the only
     * movem forms real ColdFire has (the predecrement/postincrement pair
     * above is 68k-only, kept for start.S), and what the backend emits.
     * The mask is the normal order (bit 0 = d0), unlike predecrement. */
    if (op1->type == OP_REGLIST &&
        (op2->type == OP_IND || op2->type == OP_DISP)) {
        int dm, dr;
        if (ea_mode_reg(op2, &dm, &dr) < 0) return -1;
        if (emit) {
            sec_emit16(s, (uint16_t)(0x48C0 | ea_bits(dm, dr)));
            sec_emit16(s, op1->regmask);
            ea_emit_ext(a, op2);
        }
        return 4 + ea_ext_size(op2);
    }
    if ((op1->type == OP_IND || op1->type == OP_DISP) &&
        op2->type == OP_REGLIST) {
        int sm, sr;
        if (ea_mode_reg(op1, &sm, &sr) < 0) return -1;
        if (emit) {
            sec_emit16(s, (uint16_t)(0x4CC0 | ea_bits(sm, sr)));
            sec_emit16(s, op2->regmask);
            ea_emit_ext(a, op1);
        }
        return 4 + ea_ext_size(op1);
    }
    return -1;
}

static int
encode_lea(struct assembler *a, struct operand *src, struct operand *dst,
           int emit)
{
    int sm, sr;
    int total;

    if (dst->type != OP_AREG) return -1;
    if (ea_mode_reg(src, &sm, &sr) < 0) return -1;

    total = 2 + ea_ext_size(src);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        uint16_t w = (uint16_t)(0x41C0
                   | (dst->reg << 9)
                   | ea_bits(sm, sr));
        sec_emit16(s, w);
        ea_emit_ext(a, src);
    }
    return total;
}

static int
encode_pea(struct assembler *a, struct operand *src, int emit)
{
    int sm, sr;
    int total;

    if (ea_mode_reg(src, &sm, &sr) < 0) return -1;

    total = 2 + ea_ext_size(src);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        sec_emit16(s, (uint16_t)(0x4840 | ea_bits(sm, sr)));
        ea_emit_ext(a, src);
    }
    return total;
}

static int
encode_link(struct assembler *a, struct operand *reg, struct operand *disp,
            int emit)
{
    if (reg->type != OP_AREG || disp->type != OP_IMM) return -1;

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        sec_emit16(s, (uint16_t)(0x4E50 | reg->reg));
        sec_emit16(s, (uint16_t)(int16_t)disp->imm);
    }
    return 4;
}

static int
encode_unlk(struct assembler *a, struct operand *reg, int emit)
{
    if (reg->type != OP_AREG) return -1;

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        sec_emit16(s, (uint16_t)(0x4E58 | reg->reg));
    }
    return 2;
}

static int
encode_alu(struct assembler *a, uint16_t base_opcode, int size,
           struct operand *src, struct operand *dst, int emit)
{
    int sm, sr;
    int sz_code;
    int total;

    if (dst->type == OP_AREG)
        return encode_addsuba(a, base_opcode, size, src, dst, emit);
    if (dst->type != OP_DREG) return -1;
    if (ea_mode_reg(src, &sm, &sr) < 0) return -1;

    switch (size) {
    case 1: sz_code = 0; break;
    case 2: sz_code = 1; break;
    default: sz_code = 2; break;
    }

    total = 2 + ea_ext_size(src);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        uint16_t w = base_opcode
                   | (uint16_t)(dst->reg << 9)
                   | (uint16_t)(sz_code << 6)
                   | ea_bits(sm, sr);
        sec_emit16(s, w);
        ea_emit_ext(a, src);
    }
    return total;
}

static int
encode_eor(struct assembler *a, int size,
           struct operand *src, struct operand *dst, int emit)
{
    int dm, dr;
    int sz_code;
    int total;

    if (ea_mode_reg(dst, &dm, &dr) < 0) return -1;

    switch (size) {
    case 1: sz_code = 0; break;
    case 2: sz_code = 1; break;
    default: sz_code = 2; break;
    }

    if (src->type == OP_IMM) {
        int imm_size = (size <= 2) ? 2 : 4;
        total = 2 + imm_size + ea_ext_size(dst);
        if (emit) {
            struct section *s = &a->sections[a->cur_section];
            uint16_t w = (uint16_t)(0x0A00
                       | (sz_code << 6)
                       | ea_bits(dm, dr));
            sec_emit16(s, w);
            if (size <= 2)
                sec_emit16(s, (uint16_t)(src->imm & 0xFFFF));
            else
                sec_emit32(s, (uint32_t)src->imm);
            ea_emit_ext(a, dst);
        }
        return total;
    }

    if (src->type != OP_DREG) return -1;

    total = 2 + ea_ext_size(dst);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        uint16_t w = (uint16_t)(0xB100
                   | (src->reg << 9)
                   | (sz_code << 6)
                   | ea_bits(dm, dr));
        sec_emit16(s, w);
        ea_emit_ext(a, dst);
    }
    return total;
}

static int
encode_addq_subq(struct assembler *a, uint16_t base, int size,
                 struct operand *src, struct operand *dst, int emit)
{
    int dm, dr;
    int sz_code;
    int total;
    int imm;

    if (src->type != OP_IMM) return -1;
    if (ea_mode_reg(dst, &dm, &dr) < 0) return -1;

    imm = (int)src->imm;
    if (imm < 1 || imm > 8) return -1;
    if (imm == 8) imm = 0;

    switch (size) {
    case 1: sz_code = 0; break;
    case 2: sz_code = 1; break;
    default: sz_code = 2; break;
    }

    total = 2 + ea_ext_size(dst);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        uint16_t w = base
                   | (uint16_t)(imm << 9)
                   | (uint16_t)(sz_code << 6)
                   | ea_bits(dm, dr);
        sec_emit16(s, w);
        ea_emit_ext(a, dst);
    }
    return total;
}

/* The extension word carries the destination register twice: Dq in bits
 * 14-12 and Dr in bits 2-0.  For a divide the two must name the same
 * register, since Dq != Dr is the encoding of REMS.L/REMU.L, which write
 * the remainder to Dr and leave the dividend in Dq alone.  For a multiply
 * the low field is the high half of a 64-bit product ColdFire does not
 * have, so it stays zero. */
static int
encode_muldiv(struct assembler *a, uint16_t base, uint16_t ext_base,
              struct operand *src, struct operand *dst, int emit, int same_dr)
{
    int sm, sr;
    int total;

    if (dst->type != OP_DREG) return -1;
    if (ea_mode_reg(src, &sm, &sr) < 0) return -1;

    total = 4 + ea_ext_size(src);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        uint16_t ext = (uint16_t)(ext_base | (dst->reg << 12));
        if (same_dr)
            ext |= (uint16_t)(dst->reg & 7);
        sec_emit16(s, (uint16_t)(base | ea_bits(sm, sr)));
        sec_emit16(s, ext);
        ea_emit_ext(a, src);
    }
    return total;
}

static int
encode_unary(struct assembler *a, uint16_t opcode,
             struct operand *op, int emit)
{
    int m, r;
    int total;

    if (ea_mode_reg(op, &m, &r) < 0) return -1;

    total = 2 + ea_ext_size(op);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        sec_emit16(s, (uint16_t)(opcode | ea_bits(m, r)));
        ea_emit_ext(a, op);
    }
    return total;
}

static int
encode_addsubx(struct assembler *a, uint16_t base,
               struct operand *src, struct operand *dst, int emit)
{
    if (src->type != OP_DREG || dst->type != OP_DREG) return -1;

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        sec_emit16(s, (uint16_t)(base | (dst->reg << 9) | src->reg));
    }
    return 2;
}

static int
encode_ext(struct assembler *a, uint16_t opcode,
           struct operand *op, int emit)
{
    if (op->type != OP_DREG) return -1;

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        sec_emit16(s, (uint16_t)(opcode | op->reg));
    }
    return 2;
}

static int
encode_shift(struct assembler *a, uint16_t base,
             struct operand *src, struct operand *dst, int emit)
{
    if (dst->type != OP_DREG) return -1;

    if (src->type == OP_DREG) {
        if (emit) {
            struct section *s = &a->sections[a->cur_section];
            uint16_t w = base
                       | (uint16_t)(src->reg << 9)
                       | (uint16_t)(1 << 5)
                       | (uint16_t)dst->reg;
            sec_emit16(s, w);
        }
        return 2;
    }
    if (src->type == OP_IMM) {
        int count = (int)src->imm & 7;
        if (emit) {
            struct section *s = &a->sections[a->cur_section];
            uint16_t w = (base & ~(uint16_t)0x0E20)
                       | (uint16_t)(count << 9)
                       | (uint16_t)dst->reg;
            sec_emit16(s, w);
        }
        return 2;
    }
    return -1;
}

static int
encode_cmp(struct assembler *a, int size,
           struct operand *src, struct operand *dst, int emit)
{
    int sm, sr;
    int sz_code;
    int total;

    if (dst->type != OP_DREG) return -1;
    if (ea_mode_reg(src, &sm, &sr) < 0) return -1;

    switch (size) {
    case 1: sz_code = 0; break;
    case 2: sz_code = 1; break;
    default: sz_code = 2; break;
    }

    total = 2 + ea_ext_size(src);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        uint16_t w = (uint16_t)(0xB000
                   | (dst->reg << 9)
                   | (sz_code << 6)
                   | ea_bits(sm, sr));
        sec_emit16(s, w);
        ea_emit_ext(a, src);
    }
    return total;
}

static int
encode_cmpa(struct assembler *a, int size,
            struct operand *src, struct operand *dst, int emit)
{
    int sm, sr;
    int opmode;
    int total;

    if (dst->type != OP_AREG) return -1;
    if (ea_mode_reg(src, &sm, &sr) < 0) return -1;

    opmode = (size == 2) ? 3 : 7;
    total = 2 + ea_ext_size(src);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        uint16_t w = (uint16_t)(0xB000
                   | (dst->reg << 9)
                   | (opmode << 6)
                   | ea_bits(sm, sr));
        sec_emit16(s, w);
        ea_emit_ext(a, src);
    }
    return total;
}

static int
cc_code(const char *cc)
{
    if (strcmp(cc, "eq") == 0) return 0x7;
    if (strcmp(cc, "ne") == 0) return 0x6;
    if (strcmp(cc, "lt") == 0) return 0xD;
    if (strcmp(cc, "le") == 0) return 0xF;
    if (strcmp(cc, "gt") == 0) return 0xE;
    if (strcmp(cc, "ge") == 0) return 0xC;
    if (strcmp(cc, "cs") == 0) return 0x5;
    if (strcmp(cc, "lo") == 0) return 0x5;
    if (strcmp(cc, "ls") == 0) return 0x3;
    if (strcmp(cc, "hi") == 0) return 0x2;
    if (strcmp(cc, "hs") == 0) return 0x4;
    if (strcmp(cc, "cc") == 0) return 0x4;
    if (strcmp(cc, "pl") == 0) return 0xA;
    if (strcmp(cc, "mi") == 0) return 0xB;
    return -1;
}

static int
encode_scc(struct assembler *a, int cc,
           struct operand *op, int emit)
{
    int m, r;
    int total;

    if (ea_mode_reg(op, &m, &r) < 0) return -1;

    total = 2 + ea_ext_size(op);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        sec_emit16(s, (uint16_t)(0x50C0 | (cc << 8) | ea_bits(m, r)));
        ea_emit_ext(a, op);
    }
    return total;
}

static int
encode_branch(struct assembler *a, uint16_t base,
              struct operand *target, int emit)
{
    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        int32_t disp;

        if (target->sym) {
            int idx = sym_lookup(&a->st, target->sym);
            if (idx >= 0 && a->st.syms[idx].defined
                && a->st.syms[idx].section == a->cur_section) {
                disp = (int32_t)a->st.syms[idx].value - (int32_t)s->len - 2;
            } else {
                die("line %d: unresolved branch target '%s'",
                    a->lex.tok.line, target->sym);
                disp = 0;
            }
        } else {
            disp = (int32_t)target->imm;
        }
        sec_emit16(s, base);
        sec_emit16(s, (uint16_t)(int16_t)disp);
    }
    return 4;
}

static int
encode_jsr_jmp(struct assembler *a, uint16_t base,
               struct operand *op, int emit)
{
    int m, r;
    int total;

    if (ea_mode_reg(op, &m, &r) < 0) return -1;

    total = 2 + ea_ext_size(op);

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        sec_emit16(s, (uint16_t)(base | ea_bits(m, r)));
        ea_emit_ext(a, op);
    }
    return total;
}

static int
encode_trap(struct assembler *a, struct operand *op, int emit)
{
    if (op->type != OP_IMM) return -1;

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        sec_emit16(s, (uint16_t)(0x4E40 | ((int)op->imm & 0xF)));
    }
    return 2;
}

/****************************************************************
 * FPU instruction encoding
 ****************************************************************/

static int
encode_fpu_rr(struct assembler *a, uint16_t fpu_op,
              struct operand *src, struct operand *dst, int emit)
{
    if (src->type != OP_FPREG || dst->type != OP_FPREG) return -1;

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        sec_emit16(s, 0xF200);
        sec_emit16(s, (uint16_t)(fpu_op | (src->reg << 10) | (dst->reg << 7)));
    }
    return 4;
}

static int
encode_fpu_unary(struct assembler *a, uint16_t fpu_op,
                 struct operand *op, int emit)
{
    if (op->type != OP_FPREG) return -1;

    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        sec_emit16(s, 0xF200);
        sec_emit16(s, (uint16_t)(fpu_op | (op->reg << 10) | (op->reg << 7)));
    }
    return 4;
}

static int
fmove_fmt_code(int size, int is_store)
{
    if (size == 4 && !is_store) return 0x44;
    if (size == 4 && is_store) return 0x64;
    if (size == 8 && !is_store) return 0x54;
    if (size == 8 && is_store) return 0x74;
    return -1;
}

static int
encode_fmove(struct assembler *a, int size,
             struct operand *src, struct operand *dst, int emit)
{
    struct section *s = &a->sections[a->cur_section];

    if (src->type == OP_FPREG && dst->type == OP_FPREG) {
        if (emit) {
            sec_emit16(s, 0xF200);
            sec_emit16(s, (uint16_t)((src->reg << 10) | (dst->reg << 7)));
        }
        return 4;
    }

    if (size == 0) {
        return -1;
    }

    if (src->type == OP_DREG && dst->type == OP_FPREG && size == 4) {
        int sm, sr;
        ea_mode_reg(src, &sm, &sr);
        if (emit) {
            sec_emit16(s, (uint16_t)(0xF200 | ea_bits(sm, sr)));
            sec_emit16(s, (uint16_t)(0x4000 | (dst->reg << 7)));
        }
        return 4;
    }

    if (src->type == OP_FPREG && dst->type == OP_DREG && size == 4) {
        int dm, dr;
        ea_mode_reg(dst, &dm, &dr);
        if (emit) {
            sec_emit16(s, (uint16_t)(0xF200 | ea_bits(dm, dr)));
            sec_emit16(s, (uint16_t)(0x6000 | (src->reg << 7)));
        }
        return 4;
    }

    if (src->type != OP_FPREG && dst->type == OP_FPREG) {
        int sm, sr;
        int fmt;
        int total;

        if (ea_mode_reg(src, &sm, &sr) < 0) return -1;
        fmt = fmove_fmt_code(size, 0);
        if (fmt < 0) return -1;

        total = 4 + ea_ext_size(src);
        if (emit) {
            sec_emit16(s, (uint16_t)(0xF200 | ea_bits(sm, sr)));
            sec_emit16(s, (uint16_t)((fmt << 8) | (dst->reg << 7)));
            ea_emit_ext(a, src);
        }
        return total;
    }

    if (src->type == OP_FPREG && dst->type != OP_FPREG) {
        int dm, dr;
        int fmt;
        int total;

        if (ea_mode_reg(dst, &dm, &dr) < 0) return -1;
        fmt = fmove_fmt_code(size, 1);
        if (fmt < 0) return -1;

        total = 4 + ea_ext_size(dst);
        if (emit) {
            sec_emit16(s, (uint16_t)(0xF200 | ea_bits(dm, dr)));
            sec_emit16(s, (uint16_t)((fmt << 8) | (src->reg << 7)));
            ea_emit_ext(a, dst);
        }
        return total;
    }

    return -1;
}

static int
encode_fbranch(struct assembler *a, uint16_t cond_word,
               struct operand *target, int emit)
{
    if (emit) {
        struct section *s = &a->sections[a->cur_section];
        int32_t disp;

        if (target->sym) {
            int idx = sym_lookup(&a->st, target->sym);
            if (idx >= 0 && a->st.syms[idx].defined
                && a->st.syms[idx].section == a->cur_section) {
                disp = (int32_t)a->st.syms[idx].value - (int32_t)s->len - 2;
            } else {
                die("line %d: unresolved fbranch target '%s'",
                    a->lex.tok.line, target->sym);
                disp = 0;
            }
        } else {
            disp = (int32_t)target->imm;
        }
        sec_emit16(s, cond_word);
        sec_emit16(s, (uint16_t)(int16_t)disp);
    }
    return 4;
}

/****************************************************************
 * Public interface
 ****************************************************************/

static int
do_encode(struct assembler *a, const char *mnemonic, int size,
          struct operand *op1, struct operand *op2, int emit)
{
    if (strcmp(mnemonic, "move") == 0)
        return encode_move(a, size, op1, op2, emit);
    if (strcmp(mnemonic, "movea") == 0)
        return encode_movea(a, size, op1, op2, emit);
    if (strcmp(mnemonic, "moveq") == 0)
        return encode_moveq(a, op1, op2, emit);
    if (strcmp(mnemonic, "movem") == 0)
        return encode_movem(a, op1, op2, emit);
    if (strcmp(mnemonic, "lea") == 0)
        return encode_lea(a, op1, op2, emit);
    if (strcmp(mnemonic, "pea") == 0)
        return encode_pea(a, op1, emit);
    if (strcmp(mnemonic, "link") == 0)
        return encode_link(a, op1, op2, emit);
    if (strcmp(mnemonic, "unlk") == 0)
        return encode_unlk(a, op1, emit);
    if (strcmp(mnemonic, "rts") == 0) {
        if (emit) sec_emit16(&a->sections[a->cur_section], 0x4E75);
        return 2;
    }
    if (strcmp(mnemonic, "nop") == 0) {
        if (emit) sec_emit16(&a->sections[a->cur_section], 0x4E71);
        return 2;
    }
    if (strcmp(mnemonic, "trap") == 0)
        return encode_trap(a, op1, emit);

    if (strcmp(mnemonic, "adda") == 0)
        return encode_addsuba(a, 0xD000, size, op1, op2, emit);
    if (strcmp(mnemonic, "suba") == 0)
        return encode_addsuba(a, 0x9000, size, op1, op2, emit);
    if (strcmp(mnemonic, "add") == 0)
        return encode_alu(a, 0xD000, size, op1, op2, emit);
    if (strcmp(mnemonic, "sub") == 0)
        return encode_alu(a, 0x9000, size, op1, op2, emit);
    if (strcmp(mnemonic, "and") == 0)
        return encode_alu(a, 0xC000, size, op1, op2, emit);
    if (strcmp(mnemonic, "or") == 0)
        return encode_alu(a, 0x8000, size, op1, op2, emit);
    if (strcmp(mnemonic, "cmp") == 0)
        return encode_cmp(a, size, op1, op2, emit);
    if (strcmp(mnemonic, "cmpa") == 0)
        return encode_cmpa(a, size, op1, op2, emit);
    if (strcmp(mnemonic, "eor") == 0)
        return encode_eor(a, size, op1, op2, emit);

    if (strcmp(mnemonic, "addx") == 0)
        return encode_addsubx(a, 0xD180, op1, op2, emit);
    if (strcmp(mnemonic, "subx") == 0)
        return encode_addsubx(a, 0x9180, op1, op2, emit);

    if (strcmp(mnemonic, "addq") == 0)
        return encode_addq_subq(a, 0x5000, size, op1, op2, emit);
    if (strcmp(mnemonic, "subq") == 0)
        return encode_addq_subq(a, 0x5100, size, op1, op2, emit);

    if (strcmp(mnemonic, "muls") == 0)
        return encode_muldiv(a, 0x4C00, 0x0800, op1, op2, emit, 0);
    if (strcmp(mnemonic, "mulu") == 0)
        return encode_muldiv(a, 0x4C00, 0x0000, op1, op2, emit, 0);
    if (strcmp(mnemonic, "divs") == 0)
        return encode_muldiv(a, 0x4C40, 0x0800, op1, op2, emit, 1);
    if (strcmp(mnemonic, "divu") == 0)
        return encode_muldiv(a, 0x4C40, 0x0000, op1, op2, emit, 1);

    if (strcmp(mnemonic, "clr") == 0) {
        uint16_t base = (size == 1) ? 0x4200 : (size == 2) ? 0x4240 : 0x4280;
        return encode_unary(a, base, op1, emit);
    }
    if (strcmp(mnemonic, "neg") == 0) {
        uint16_t base = (size == 1) ? 0x4400 : (size == 2) ? 0x4440 : 0x4480;
        return encode_unary(a, base, op1, emit);
    }
    if (strcmp(mnemonic, "not") == 0) {
        uint16_t base = (size == 1) ? 0x4600 : (size == 2) ? 0x4640 : 0x4680;
        return encode_unary(a, base, op1, emit);
    }
    if (strcmp(mnemonic, "tst") == 0) {
        uint16_t base = (size == 1) ? 0x4A00 : (size == 2) ? 0x4A40 : 0x4A80;
        return encode_unary(a, base, op1, emit);
    }

    if (strcmp(mnemonic, "ext") == 0) {
        if (size == 2) return encode_ext(a, 0x4880, op1, emit);
        if (size == 4) return encode_ext(a, 0x48C0, op1, emit);
        return -1;
    }

    if (strcmp(mnemonic, "extb") == 0) {
        if (size == 4) return encode_ext(a, 0x49C0, op1, emit);
        return -1;
    }

    if (strcmp(mnemonic, "lsl") == 0)
        return encode_shift(a, 0xE1A8, op1, op2, emit);
    if (strcmp(mnemonic, "lsr") == 0)
        return encode_shift(a, 0xE0A8, op1, op2, emit);
    if (strcmp(mnemonic, "asr") == 0)
        return encode_shift(a, 0xE0A0, op1, op2, emit);

    if (strcmp(mnemonic, "jsr") == 0)
        return encode_jsr_jmp(a, 0x4E80, op1, emit);
    if (strcmp(mnemonic, "jmp") == 0)
        return encode_jsr_jmp(a, 0x4EC0, op1, emit);

    if (strcmp(mnemonic, "bra") == 0)
        return encode_branch(a, 0x6000, op1, emit);
    if (strcmp(mnemonic, "bsr") == 0)
        return encode_branch(a, 0x6100, op1, emit);
    if (strcmp(mnemonic, "beq") == 0)
        return encode_branch(a, 0x6700, op1, emit);
    if (strcmp(mnemonic, "bne") == 0)
        return encode_branch(a, 0x6600, op1, emit);
    if (strcmp(mnemonic, "bpl") == 0)
        return encode_branch(a, 0x6A00, op1, emit);
    if (strcmp(mnemonic, "bmi") == 0)
        return encode_branch(a, 0x6B00, op1, emit);
    if (strcmp(mnemonic, "blt") == 0)
        return encode_branch(a, 0x6D00, op1, emit);
    if (strcmp(mnemonic, "bgt") == 0)
        return encode_branch(a, 0x6E00, op1, emit);
    if (strcmp(mnemonic, "ble") == 0)
        return encode_branch(a, 0x6F00, op1, emit);
    if (strcmp(mnemonic, "bge") == 0)
        return encode_branch(a, 0x6C00, op1, emit);
    if (strcmp(mnemonic, "bcs") == 0 || strcmp(mnemonic, "blo") == 0)
        return encode_branch(a, 0x6500, op1, emit);
    if (strcmp(mnemonic, "bcc") == 0 || strcmp(mnemonic, "bhs") == 0)
        return encode_branch(a, 0x6400, op1, emit);
    if (strcmp(mnemonic, "bhi") == 0)
        return encode_branch(a, 0x6200, op1, emit);
    if (strcmp(mnemonic, "bls") == 0)
        return encode_branch(a, 0x6300, op1, emit);

    if (mnemonic[0] == 's') {
        int cc = cc_code(mnemonic + 1);
        if (cc >= 0)
            return encode_scc(a, cc, op1, emit);
    }

    if (strcmp(mnemonic, "fmove") == 0)
        return encode_fmove(a, size, op1, op2, emit);
    if (strcmp(mnemonic, "fadd") == 0)
        return encode_fpu_rr(a, 0x0022, op1, op2, emit);
    if (strcmp(mnemonic, "fsub") == 0)
        return encode_fpu_rr(a, 0x0028, op1, op2, emit);
    if (strcmp(mnemonic, "fmul") == 0)
        return encode_fpu_rr(a, 0x0023, op1, op2, emit);
    if (strcmp(mnemonic, "fdiv") == 0)
        return encode_fpu_rr(a, 0x0020, op1, op2, emit);
    if (strcmp(mnemonic, "fneg") == 0)
        return encode_fpu_unary(a, 0x001A, op1, emit);
    if (strcmp(mnemonic, "fabs") == 0)
        return encode_fpu_unary(a, 0x0018, op1, emit);
    if (strcmp(mnemonic, "fintrz") == 0)
        return encode_fpu_rr(a, 0x0003, op1, op2, emit);
    if (strcmp(mnemonic, "fcmp") == 0)
        return encode_fpu_rr(a, 0x0038, op1, op2, emit);

    if (strcmp(mnemonic, "fbeq") == 0)
        return encode_fbranch(a, 0xF281, op1, emit);
    if (strcmp(mnemonic, "fbne") == 0)
        return encode_fbranch(a, 0xF28E, op1, emit);
    if (strcmp(mnemonic, "fblt") == 0)
        return encode_fbranch(a, 0xF294, op1, emit);
    if (strcmp(mnemonic, "fble") == 0)
        return encode_fbranch(a, 0xF295, op1, emit);
    if (strcmp(mnemonic, "fbgt") == 0)
        return encode_fbranch(a, 0xF292, op1, emit);
    if (strcmp(mnemonic, "fbge") == 0)
        return encode_fbranch(a, 0xF293, op1, emit);

    return -1;
}

int
encode_insn(struct assembler *a, const char *mnemonic, int size,
            struct operand *op1, struct operand *op2)
{
    return do_encode(a, mnemonic, size, op1, op2, 1);
}

int
encode_size(const char *mnemonic, int size,
            struct operand *op1, struct operand *op2)
{
    struct assembler dummy;
    memset(&dummy, 0, sizeof(dummy));
    return do_encode(&dummy, mnemonic, size, op1, op2, 0);
}
