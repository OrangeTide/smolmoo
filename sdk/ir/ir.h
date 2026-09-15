/* ir.h : portable compiler IR data structures, builder API, and shared declarations
 * Copyright (c) 2026 Jon Mayo
 * SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#ifndef IR_H
#define IR_H

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "arena.h"
#include "util.h"

/****************************************************************
 * Base types
 ****************************************************************/

enum ir_basetype {
    IR_I32 = 1,
    IR_I8  = 2,
    IR_I16 = 3,
    IR_F64 = 4,
    IR_I64 = 5,
    IR_F32 = 6,
};

/*
 * Float opcode width tag.  The floating-point opcodes (IR_FADD .. IR_FCALLI,
 * IR_ITOF/IR_FTOI, the F-loads/stores and F-slots) carry their precision in
 * the instruction's `imm` field: FWIDTH_F32 means single precision, anything
 * else (the default 0, or 8) means double.  F32 and F64 temps share the fp
 * register class, so only instruction selection differs; the two cross-width
 * conversions are IR_F32TOF64 / IR_F64TOF32.
 */
#define FWIDTH_F32 4

/****************************************************************
 * IR opcodes
 ****************************************************************/

enum ir_op {
    IR_NOP,
    IR_LIC,
    IR_LEA,
    IR_ADL,
    IR_MOV,

    IR_ADD, IR_SUB, IR_MUL,
    IR_DIVS, IR_DIVU, IR_MODS, IR_MODU,
    IR_AND, IR_OR, IR_XOR,
    IR_SHL, IR_SHRS, IR_SHRU,
    IR_NEG, IR_NOT,

    IR_LB, IR_LBS, IR_LH, IR_LHS, IR_LW,
    IR_SB, IR_SH, IR_SW,

    IR_LDL, IR_STL,
    IR_ALLOCA,

    IR_CMPEQ, IR_CMPNE,
    IR_CMPLTS, IR_CMPLES, IR_CMPGTS, IR_CMPGES,
    IR_CMPLTU, IR_CMPLEU, IR_CMPGTU, IR_CMPGEU,

    IR_JMP, IR_BZ, IR_BNZ,

    IR_ARG,
    IR_FARG,
    IR_CALL,
    IR_CALLI,
    IR_TAILCALL,
    IR_TAILCALLI,
    IR_RET, IR_RETV,

    IR_FUNC, IR_ENDF, IR_LABEL,

    IR_MARK, IR_CAPTURE, IR_RESUME,

    IR_FADD, IR_FSUB, IR_FMUL, IR_FDIV,
    IR_FNEG, IR_FABS,

    IR_FCMPEQ, IR_FCMPLT, IR_FCMPLE,

    IR_ITOF, IR_FTOI,
    IR_F32TOF64, IR_F64TOF32,

    IR_FLS, IR_FLD,
    IR_FSS, IR_FSD,
    IR_FLH, IR_FSH,
    IR_FLDL, IR_FSTL,

    IR_FRETV,
    IR_FCALL, IR_FCALLI,

    IR_LIC64,
    IR_ADD64, IR_SUB64, IR_MUL64,
    IR_AND64, IR_OR64, IR_XOR64,
    IR_SHL64, IR_SHRS64, IR_SHRU64,
    IR_NEG64,

    IR_CMP64EQ, IR_CMP64NE,
    IR_CMP64LTS, IR_CMP64LES, IR_CMP64GTS, IR_CMP64GES,
    IR_CMP64LTU, IR_CMP64LEU, IR_CMP64GTU, IR_CMP64GEU,

    IR_LD64, IR_ST64,
    IR_LDL64, IR_STL64,

    /* 64-bit address roots (LP64): a symbol / local-slot address as a full
       64-bit value.  Only emitted by cc under LP64; the ILP32 targets use the
       32-bit IR_LEA / IR_ADL. */
    IR_LEA64, IR_ADL64,

    /* va_start: initialize the va_list whose address is in operand `a` from
       the current function's SysV register save area and stack args.  The
       backend owns the frame layout, so it fills the four fields. */
    IR_VA_START,

    IR_SEXT64, IR_ZEXT64, IR_TRUNC64,

    IR_ARG64,
    IR_RETV64,
    IR_CALL64, IR_CALLI64,

    /* aggregate (struct-by-value) return and call result for the psABI
       register classification: RETV_AGG packs the struct at [a] into the
       return registers (using fn->ret_cls); CALL_AGG / CALLI_AGG make the
       call and store the returned eightbytes into slot (imm encodes the
       eightbyte count and classes) */
    IR_RETV_AGG,
    IR_CALL_AGG, IR_CALLI_AGG,

    /* a MEMORY-class (>16 byte) struct argument passed by value: a = the
       struct's address, imm = its byte size; the caller copies it onto the
       stack.  A MEMORY return uses a hidden pointer, an ordinary leading
       integer parameter cc adds, so it needs no new opcode. */
    IR_ARG_MEM,

    /* AAPCS64: the indirect-result pointer for a memory-class struct return,
       placed in x8 (a = the result address) rather than a normal arg register */
    IR_ARG_X8,

    /* basic inline assembly: sym holds the verbatim asm text (one string, the
       body of an asm(...) statement).  A backend emits it as-is.  It is an
       opaque barrier: no operands, no register allocation, no scheduling. */
    IR_ASM,
};

/****************************************************************
 * IR data structures
 ****************************************************************/

struct ir_insn {
    int op;
    int dst;
    int a, b;
    long imm;
    char *sym;
    int slot;
    int label;
    int nargs;
    struct ir_insn *next;
};

struct ir_func {
    struct arena *arena;
    char *name;
    int is_local;
    int nparams;
    int nslots;
    int *slot_size;
    int *param_class;   /* per-param: 0 = integer/pointer, 1 = float (for the
                           register calling convention); NULL if unset */
    signed char *param_neb; /* psABI: register slots for param i (1 for a
                           scalar; 1-4 for a struct in registers, up to 2 SysV
                           eightbytes or 4 AAPCS64 HFA members; 0 = on the
                           stack).  NULL unless the psABI is in effect */
    signed char *param_cls; /* psABI: 4 entries per param, the class of each
                           slot (0 = INTEGER/gp, 1 = FLOAT/fp); a -2 in slot 0
                           marks a MEMORY struct read from the stack */
    signed char *param_fw; /* psABI: per-param 1 = a single-precision (4-byte)
                           float scalar, 0 otherwise.  The RISC-V ilp32 ABI
                           passes a single float in one integer register and a
                           double in two, a distinction the local slot size
                           loses (a float slot is widened to 8), so the RV
                           backend reads it here; NULL unless set */
    signed char ret_neb; /* psABI: return-struct register slots (0 = scalar/void
                           via the ordinary RETV paths; 1-4 = struct in
                           registers; -1 = MEMORY, returned via a hidden ptr) */
    signed char ret_cls[4]; /* psABI: class of each return slot */
    int is_variadic;    /* function takes `...`; the prologue saves the arg
                           registers for va_arg (SysV register save area) */
    int ntemps;
    int nlabels;
    int nspills;
    int nfspills;
    int ni64spills;
    int *temp_reg;
    int *temp_spill;
    struct ir_insn *head;
    struct ir_insn *tail;
    struct ir_func *next;
};

/*
 * One initializer chunk of an aggregate global's byte image.  Items are held
 * in ascending offset order; a backend pads with zeros between them and to the
 * global's full size.  A chunk is either a scalar of `size` bytes (1/2/4/8,
 * value in `ival`) or, when `sym` is set, a 4-byte reference to that symbol.
 * Keeping it typed (not a raw byte array) lets the assembler apply the target
 * endianness and lets a pointer field carry a relocation.
 */
struct ir_init {
    int offset;         /* byte offset within the global image */
    int size;           /* 1, 2, 4, or 8 */
    int64_t ival;       /* scalar value bits (used when sym is null) */
    char *sym;          /* non-null: a 4-byte reference to this symbol */
    struct ir_init *next;
};

struct ir_global {
    char *name;
    int base_type;
    int arr_size;
    int align;          /* required byte alignment; 0 = base_type default */
    struct ir_init *inits;  /* non-null: aggregate byte image (see ir_init) */
    int is_ptr;
    int is_local;
    int64_t *init_ivals;
    char **init_syms;
    int init_count;
    char *init_string;
    int init_strlen;
    struct ir_global *next;
};

struct ir_program {
    struct ir_func *funcs;
    struct ir_global *globals;
};

/****************************************************************
 * IR builder API
 ****************************************************************/

struct ir_func *ir_new_func(struct arena *a, const char *name);
int ir_new_temp(struct ir_func *fn);
int ir_new_label(struct ir_func *fn);
struct ir_insn *ir_emit(struct ir_func *fn, int op);
int ir_op_is_float_def(int op);
int ir_op_is_i64_def(int op);

/****************************************************************
 * Register allocation
 ****************************************************************/

void regalloc(struct ir_func *fn);

/****************************************************************
 * Back-end interface
 *
 * regalloc() and target_emit() are provided by the linked backend.
 * For compile-time target selection, link a different backend:
 *   ColdFire: backend/regalloc_cf.c + backend/cf_emit.c
 *   RISC-V:   backend/regalloc_rv.c + backend/rv_emit.c  (planned)
 ****************************************************************/

void target_emit(FILE *out, struct ir_program *prog);

#endif /* IR_H */
