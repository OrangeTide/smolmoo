/* lower.c : MooScript AST to IR lowering
 * Copyright (c) 2026 Jon Mayo
 * SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "moo.h"

#include <stdlib.h>
#include <string.h>

/****************************************************************
 * Convenience wrappers
 ****************************************************************/

static struct arena *lower_arena;
static struct ir_func *cur_fn;
static struct ir_program *cur_prog;

struct val {
    int temp;
    int type;
};

static struct ir_insn *
emit(int op)
{
    return ir_emit(cur_fn, op);
}

static int
new_temp(void)
{
    return ir_new_temp(cur_fn);
}

static int
new_label(void)
{
    return ir_new_label(cur_fn);
}

/****************************************************************
 * Per-function locals
 ****************************************************************/

struct lsym {
    char *name;
    int slot;
    int type;
};

static struct lsym *lsyms;
static int nlsyms;
static int lsym_cap;

static int *slot_sizes;
static int nslots;
static int slot_cap;

static int
alloc_slot(int bytes)
{
    if (nslots == slot_cap) {
        slot_cap = slot_cap ? slot_cap * 2 : 16;
        slot_sizes = realloc(slot_sizes, slot_cap * sizeof(*slot_sizes));
        if (!slot_sizes)
            die("oom");
    }
    slot_sizes[nslots] = bytes;
    return nslots++;
}

static void
add_lsym(const char *name, int slot, int type)
{
    if (nlsyms == lsym_cap) {
        lsym_cap = lsym_cap ? lsym_cap * 2 : 16;
        lsyms = realloc(lsyms, lsym_cap * sizeof(*lsyms));
        if (!lsyms)
            die("oom");
    }
    lsyms[nlsyms].name = arena_strdup(lower_arena,name);
    lsyms[nlsyms].slot = slot;
    lsyms[nlsyms].type = type;
    nlsyms++;
}

static struct lsym *
find_lsym(const char *name)
{
    for (int i = nlsyms - 1; i >= 0; i--)
        if (strcmp(lsyms[i].name, name) == 0)
            return &lsyms[i];
    return NULL;
}

/****************************************************************
 * Verb (function) table
 ****************************************************************/

static char **verbs;
static int nverbs;
static int verb_cap;

static void
register_verb(const char *name)
{
    if (nverbs == verb_cap) {
        verb_cap = verb_cap ? verb_cap * 2 : 16;
        verbs = realloc(verbs, verb_cap * sizeof(*verbs));
        if (!verbs)
            die("oom");
    }
    verbs[nverbs++] = arena_strdup(lower_arena,name);
}

static int
is_verb(const char *name)
{
    for (int i = 0; i < nverbs; i++)
        if (strcmp(verbs[i], name) == 0)
            return 1;
    return 0;
}

/****************************************************************
 * Extern link-name table
 ****************************************************************/

struct link_entry {
    char *script_name;
    char *link_name;
};

static struct link_entry *links;
static int nlinks;
static int link_cap;

static void
add_link(const char *script, const char *link)
{
    if (nlinks == link_cap) {
        link_cap = link_cap ? link_cap * 2 : 8;
        links = realloc(links, link_cap * sizeof(*links));
        if (!links)
            die("oom");
    }
    links[nlinks].script_name = arena_strdup(lower_arena,script);
    links[nlinks].link_name = arena_strdup(lower_arena,link);
    nlinks++;
}

static const char *
find_link(const char *script_name)
{
    for (int i = 0; i < nlinks; i++)
        if (strcmp(links[i].script_name, script_name) == 0)
            return links[i].link_name;
    return NULL;
}

/****************************************************************
 * Interface table (for is/as lowering)
 ****************************************************************/

static struct node **iface_defs;
static int niface_defs;
static int iface_def_cap;

static void
register_iface(struct node *iface)
{
    if (niface_defs == iface_def_cap) {
        iface_def_cap = iface_def_cap ? iface_def_cap * 2 : 8;
        iface_defs = realloc(iface_defs,
                             iface_def_cap * sizeof(*iface_defs));
        if (!iface_defs)
            die("oom");
    }
    iface_defs[niface_defs++] = iface;
}

static struct node *
find_iface(const char *name)
{
    for (int i = 0; i < niface_defs; i++)
        if (strcmp(iface_defs[i]->name, name) == 0)
            return iface_defs[i];
    return NULL;
}

/****************************************************************
 * Constants table
 ****************************************************************/

struct cval {
    char *name;
    long val;
};

static struct cval *cvals;
static int ncvals;
static int cval_cap;

static void
add_const(const char *name, long val)
{
    if (ncvals == cval_cap) {
        cval_cap = cval_cap ? cval_cap * 2 : 16;
        cvals = realloc(cvals, cval_cap * sizeof(*cvals));
        if (!cvals)
            die("oom");
    }
    cvals[ncvals].name = arena_strdup(lower_arena,name);
    cvals[ncvals].val = val;
    ncvals++;
}

static struct cval *
find_const(const char *name)
{
    for (int i = 0; i < ncvals; i++)
        if (strcmp(cvals[i].name, name) == 0)
            return &cvals[i];
    return NULL;
}

/****************************************************************
 * Break/continue stack
 ****************************************************************/

struct loop {
    int brk;
    int cont;
};

static struct loop loops[64];
static int nloops;

/****************************************************************
 * Defer/panic/recover state
 ****************************************************************/

#define MAX_DEFERS 64

static int defer_flags[MAX_DEFERS];
static struct node *defer_bodies[MAX_DEFERS];
static int ndefers;
static int panic_slot;
static int retval_slot;
static int ldefer_run;
static int use_defer;
static int in_defer;

static int use_return_push;
static int ret_base_slot;
static int ret_count_slot;

static int
has_return_push(struct node *n)
{
    for (; n; n = n->next) {
        if (n->kind == N_RETURN_PUSH)
            return 1;
        if (n->a && has_return_push(n->a))
            return 1;
        if (n->b && has_return_push(n->b))
            return 1;
        if (n->c && has_return_push(n->c))
            return 1;
    }
    return 0;
}

static int
count_defers_in(struct node *n)
{
    int c = 0;

    for (; n; n = n->next) {
        if (n->kind == N_DEFER) {
            c++;
            continue;
        }
        if (n->a)
            c += count_defers_in(n->a);
        if (n->b)
            c += count_defers_in(n->b);
        if (n->c)
            c += count_defers_in(n->c);
    }
    return c;
}

/****************************************************************
 * Error code lookup (LambdaMOO standard values)
 ****************************************************************/

static long
errcode_val(const char *name)
{
    if (strcmp(name, "E_NONE") == 0)    return 0;
    if (strcmp(name, "E_TYPE") == 0)    return 1;
    if (strcmp(name, "E_INVARG") == 0)  return 2;
    if (strcmp(name, "E_RANGE") == 0)   return 3;
    if (strcmp(name, "E_PERM") == 0)    return 4;
    if (strcmp(name, "E_PROPNF") == 0)  return 5;
    if (strcmp(name, "E_VERBNF") == 0)  return 6;
    if (strcmp(name, "E_ARGS") == 0)    return 7;
    if (strcmp(name, "E_RECMOVE") == 0) return 8;
    if (strcmp(name, "E_MAXREC") == 0)  return 9;
    if (strcmp(name, "E_INVOBJ") == 0)  return 10;
    die("unknown error code: $%s", name);
    return 0;
}

/****************************************************************
 * Emit helpers
 ****************************************************************/

static int
lower_const(long v)
{
    struct ir_insn *ins;

    ins = emit(IR_LIC);
    ins->dst = new_temp();
    ins->imm = v;
    return ins->dst;
}

static void
emit_label(int lab)
{
    struct ir_insn *ins;

    ins = emit(IR_LABEL);
    ins->label = lab;
}

static void
emit_jmp(int lab)
{
    struct ir_insn *ins;

    ins = emit(IR_JMP);
    ins->label = lab;
}

static void
emit_bnz(int t, int lab)
{
    struct ir_insn *ins;

    ins = emit(IR_BNZ);
    ins->a = t;
    ins->label = lab;
}

static int
emit_rtcall(const char *name, int argc, int *argv)
{
    struct ir_insn *ins;

    for (int i = 0; i < argc; i++) {
        ins = emit(IR_ARG);
        ins->a = argv[i];
        ins->imm = i;
    }
    ins = emit(IR_CALL);
    ins->dst = new_temp();
    ins->sym = arena_strdup(lower_arena, name);
    ins->nargs = argc;
    return ins->dst;
}

static int
lower_strlit(const char *str, int len)
{
    static int strctr;
    struct ir_global *g;
    struct ir_insn *ins;
    char strbuf[32], hdrbuf[32];
    int id = strctr++;

    snprintf(strbuf, sizeof(strbuf), "__str_%d", id);
    g = arena_zalloc(lower_arena, sizeof(*g));
    g->name = arena_strdup(lower_arena, strbuf);
    g->base_type = IR_I8;
    g->arr_size = len + 1;
    g->is_local = 1;
    g->init_string = arena_alloc(lower_arena, len + 1);
    memcpy(g->init_string, str, len);
    g->init_string[len] = '\0';
    g->init_strlen = len + 1;
    g->next = cur_prog->globals;
    cur_prog->globals = g;

    snprintf(hdrbuf, sizeof(hdrbuf), "__shdr_%d", id);
    g = arena_zalloc(lower_arena, sizeof(*g));
    g->name = arena_strdup(lower_arena, hdrbuf);
    g->base_type = IR_I32;
    g->arr_size = 2;
    g->is_local = 1;
    g->init_count = 2;
    g->init_ivals = arena_alloc(lower_arena, 2 * sizeof(*g->init_ivals));
    g->init_ivals[0] = len;
    g->init_ivals[1] = 0;
    g->init_syms = arena_zalloc(lower_arena, 2 * sizeof(char *));
    g->init_syms[1] = arena_strdup(lower_arena, strbuf);
    g->next = cur_prog->globals;
    cur_prog->globals = g;

    ins = emit(IR_LEA);
    ins->dst = new_temp();
    ins->sym = arena_strdup(lower_arena, hdrbuf);
    return ins->dst;
}

/****************************************************************
 * Forward declarations
 ****************************************************************/

static struct val lower_expr(struct node *n);
static void lower_stmt(struct node *n);
static void lower_cond(struct node *n, int ltrue, int lfalse);

/****************************************************************
 * Boxed prop helpers
 ****************************************************************/

static struct val
deref_prop(struct val v)
{
    struct ir_insn *ins;
    int off, addr;

    if (v.type != T_TPROP)
        return v;
    off = lower_const(4);
    ins = emit(IR_ADD);
    ins->dst = new_temp();
    ins->a = v.temp;
    ins->b = off;
    addr = ins->dst;
    ins = emit(IR_LW);
    ins->dst = new_temp();
    ins->a = addr;
    return (struct val){ ins->dst, 0 };
}

static int
prop_tag_for_type(int type)
{
    switch (type) {
    case T_TINT:  return 0;
    case T_TSTR:  return 1;
    case T_TOBJ:  return 2;
    case T_TLIST: return 3;
    case T_TERR:  return 4;
    case T_TBOOL: return 5;
    case T_TFLOAT: return 6;
    default:      return -1;
    }
}

static int
emit_typed_unbox(struct val v, int target_type)
{
    struct ir_insn *ins;
    int tag, expected, lok, off, addr;

    if (v.type != T_TPROP)
        return v.temp;

    expected = prop_tag_for_type(target_type);
    if (expected < 0)
        return deref_prop(v).temp;

    ins = emit(IR_LW);
    ins->dst = new_temp();
    ins->a = v.temp;
    tag = ins->dst;

    lok = new_label();
    ins = emit(IR_CMPEQ);
    ins->dst = new_temp();
    ins->a = tag;
    ins->b = lower_const(expected);
    emit_bnz(ins->dst, lok);

    {
        int ev = lower_const(1);
        if (use_defer) {
            ins = emit(IR_STL);
            ins->a = ev;
            ins->slot = panic_slot;
            emit_jmp(ldefer_run);
        } else {
            ins = emit(IR_RETV);
            ins->a = ev;
        }
    }

    emit_label(lok);
    off = lower_const(4);
    ins = emit(IR_ADD);
    ins->dst = new_temp();
    ins->a = v.temp;
    ins->b = off;
    addr = ins->dst;
    ins = emit(IR_LW);
    ins->dst = new_temp();
    ins->a = addr;
    return ins->dst;
}

/****************************************************************
 * Condition lowering
 ****************************************************************/

static struct val
lower_shortcircuit(struct node *n)
{
    int t, ltrue, lfalse, lend;
    struct ir_insn *ins;

    ltrue = new_label();
    lfalse = new_label();
    lend = new_label();

    lower_cond(n, ltrue, lfalse);

    emit_label(ltrue);
    t = new_temp();
    ins = emit(IR_LIC);
    ins->dst = t;
    ins->imm = 1;
    emit_jmp(lend);

    emit_label(lfalse);
    ins = emit(IR_LIC);
    ins->dst = t;
    ins->imm = 0;

    emit_label(lend);
    return (struct val){ t, T_TBOOL };
}

static void
lower_cond(struct node *n, int ltrue, int lfalse)
{
    if (n->kind == N_BINOP && n->op == T_ANDAND) {
        int lmid = new_label();
        lower_cond(n->a, lmid, lfalse);
        emit_label(lmid);
        lower_cond(n->b, ltrue, lfalse);
        return;
    }
    if (n->kind == N_BINOP && n->op == T_OROR) {
        int lmid = new_label();
        lower_cond(n->a, ltrue, lmid);
        emit_label(lmid);
        lower_cond(n->b, ltrue, lfalse);
        return;
    }
    if (n->kind == N_UNOP && n->op == T_BANG) {
        lower_cond(n->a, lfalse, ltrue);
        return;
    }
    emit_bnz(deref_prop(lower_expr(n)).temp, ltrue);
    emit_jmp(lfalse);
}

/****************************************************************
 * Expression lowering
 ****************************************************************/

static struct val
lower_expr(struct node *n)
{
    struct ir_insn *ins;
    struct lsym *ls;
    struct cval *cv;
    struct val lv, rv;
    int irop, rtype;

    if (!n)
        die("lower: null expression");

    switch (n->kind) {
    case N_NUM:
        return (struct val){ lower_const(n->ival), T_TINT };

    case N_FLOAT: {
        static int fltctr;
        struct ir_global *g;
        char buf[32];
        union { double d; int64_t l; } u;
        int id = fltctr++;

        snprintf(buf, sizeof(buf), "__flt_%d", id);
        u.d = n->fval;
        g = arena_zalloc(lower_arena, sizeof(*g));
        g->name = arena_strdup(lower_arena, buf);
        g->base_type = IR_F64;
        g->arr_size = 1;
        g->is_local = 1;
        g->init_count = 1;
        g->init_ivals = arena_alloc(lower_arena, sizeof(*g->init_ivals));
        g->init_ivals[0] = u.l;
        g->next = cur_prog->globals;
        cur_prog->globals = g;

        ins = emit(IR_LEA);
        ins->dst = new_temp();
        ins->sym = arena_strdup(lower_arena, buf);
        int addr = ins->dst;
        ins = emit(IR_FLD);
        ins->dst = new_temp();
        ins->a = addr;
        return (struct val){ ins->dst, T_TFLOAT };
    }

    case N_BOOL:
        return (struct val){ lower_const(n->ival ? 1 : 0), T_TBOOL };

    case N_NIL:
        return (struct val){ lower_const(0), 0 };

    case N_ERRVAL:
        return (struct val){ lower_const(errcode_val(n->sval)), T_TERR };

    case N_STR:
        return (struct val){ lower_strlit(n->sval, n->slen), T_TSTR };

    case N_NAME:
        cv = find_const(n->name);
        if (cv)
            return (struct val){ lower_const(cv->val), T_TINT };
        ls = find_lsym(n->name);
        if (ls) {
            ins = emit(ls->type == T_TFLOAT ? IR_FLDL : IR_LDL);
            ins->dst = new_temp();
            ins->slot = ls->slot;
            return (struct val){ ins->dst, ls->type };
        }
        if (is_verb(n->name)) {
            ins = emit(IR_LEA);
            ins->dst = new_temp();
            ins->sym = arena_strdup(lower_arena, n->name);
            return (struct val){ ins->dst, 0 };
        }
        die("lower:%d: undefined '%s'", n->line, n->name);
        return (struct val){ 0, 0 };

    case N_UNOP:
        lv = deref_prop(lower_expr(n->a));
        switch (n->op) {
        case T_MINUS:
            if (lv.type == T_TFLOAT) {
                ins = emit(IR_FNEG);
                ins->dst = new_temp();
                ins->a = lv.temp;
                return (struct val){ ins->dst, T_TFLOAT };
            }
            ins = emit(IR_NEG);
            ins->dst = new_temp();
            ins->a = lv.temp;
            return (struct val){ ins->dst, T_TINT };
        case T_BANG: {
            int zero = lower_const(0);
            ins = emit(IR_CMPEQ);
            ins->dst = new_temp();
            ins->a = lv.temp;
            ins->b = zero;
            return (struct val){ ins->dst, T_TBOOL };
        }
        default:
            die("lower:%d: unhandled unary op %s",
                n->line, tok_str(n->op));
            return (struct val){ 0, 0 };
        }

    case N_BINOP: {
        int argv[2], t;

        if (n->op == T_ANDAND || n->op == T_OROR)
            return lower_shortcircuit(n);

        lv = deref_prop(lower_expr(n->a));
        rv = deref_prop(lower_expr(n->b));

        if ((lv.type == T_TSTR || rv.type == T_TSTR) &&
            n->op == T_PLUS) {
            argv[0] = lv.temp;
            argv[1] = rv.temp;
            t = emit_rtcall("__moo_str_concat", 2, argv);
            return (struct val){ t, T_TSTR };
        }
        if (n->op == T_IN && rv.type == T_TLIST) {
            argv[0] = rv.temp;
            argv[1] = lv.temp;
            t = emit_rtcall("__moo_list_contains", 2, argv);
            return (struct val){ t, T_TBOOL };
        }
        if ((lv.type == T_TSTR || rv.type == T_TSTR) &&
            (n->op == T_EQ || n->op == T_NE)) {
            argv[0] = lv.temp;
            argv[1] = rv.temp;
            t = emit_rtcall("__moo_str_eq", 2, argv);
            if (n->op == T_NE) {
                int zero = lower_const(0);
                ins = emit(IR_CMPEQ);
                ins->dst = new_temp();
                ins->a = t;
                ins->b = zero;
                t = ins->dst;
            }
            return (struct val){ t, T_TBOOL };
        }

        if (lv.type == T_TFLOAT || rv.type == T_TFLOAT) {
            if (lv.type == T_TINT) {
                ins = emit(IR_ITOF);
                ins->dst = new_temp();
                ins->a = lv.temp;
                lv.temp = ins->dst;
                lv.type = T_TFLOAT;
            }
            if (rv.type == T_TINT) {
                ins = emit(IR_ITOF);
                ins->dst = new_temp();
                ins->a = rv.temp;
                rv.temp = ins->dst;
                rv.type = T_TFLOAT;
            }

            switch (n->op) {
            case T_PLUS:    irop = IR_FADD; rtype = T_TFLOAT; break;
            case T_MINUS:   irop = IR_FSUB; rtype = T_TFLOAT; break;
            case T_STAR:    irop = IR_FMUL; rtype = T_TFLOAT; break;
            case T_SLASH:   irop = IR_FDIV; rtype = T_TFLOAT; break;
            case T_PERCENT: {
                int q, ti, tf, p;
                ins = emit(IR_FDIV);
                ins->dst = new_temp();
                ins->a = lv.temp;
                ins->b = rv.temp;
                q = ins->dst;
                ins = emit(IR_FTOI);
                ins->dst = new_temp();
                ins->a = q;
                ti = ins->dst;
                ins = emit(IR_ITOF);
                ins->dst = new_temp();
                ins->a = ti;
                tf = ins->dst;
                ins = emit(IR_FMUL);
                ins->dst = new_temp();
                ins->a = tf;
                ins->b = rv.temp;
                p = ins->dst;
                ins = emit(IR_FSUB);
                ins->dst = new_temp();
                ins->a = lv.temp;
                ins->b = p;
                return (struct val){ ins->dst, T_TFLOAT };
            }
            case T_EQ:      irop = IR_FCMPEQ; rtype = T_TBOOL; break;
            case T_LT:      irop = IR_FCMPLT; rtype = T_TBOOL; break;
            case T_LE:      irop = IR_FCMPLE; rtype = T_TBOOL; break;
            case T_NE: {
                int eq;
                ins = emit(IR_FCMPEQ);
                ins->dst = new_temp();
                ins->a = lv.temp;
                ins->b = rv.temp;
                eq = ins->dst;
                ins = emit(IR_CMPEQ);
                ins->dst = new_temp();
                ins->a = eq;
                ins->b = lower_const(0);
                return (struct val){ ins->dst, T_TBOOL };
            }
            case T_GT:
                irop = IR_FCMPLT;
                rtype = T_TBOOL;
                { int sw = lv.temp; lv.temp = rv.temp; rv.temp = sw; }
                break;
            case T_GE:
                irop = IR_FCMPLE;
                rtype = T_TBOOL;
                { int sw = lv.temp; lv.temp = rv.temp; rv.temp = sw; }
                break;
            default:
                die("lower:%d: unhandled float binary op %s",
                    n->line, tok_str(n->op));
                return (struct val){ 0, 0 };
            }

            ins = emit(irop);
            ins->dst = new_temp();
            ins->a = lv.temp;
            ins->b = rv.temp;
            return (struct val){ ins->dst, rtype };
        }

        switch (n->op) {
        case T_PLUS:    irop = IR_ADD; rtype = T_TINT; break;
        case T_MINUS:   irop = IR_SUB; rtype = T_TINT; break;
        case T_STAR:    irop = IR_MUL; rtype = T_TINT; break;
        case T_SLASH:   irop = IR_DIVS; rtype = T_TINT; break;
        case T_PERCENT: irop = IR_MODS; rtype = T_TINT; break;
        case T_EQ:      irop = IR_CMPEQ; rtype = T_TBOOL; break;
        case T_NE:      irop = IR_CMPNE; rtype = T_TBOOL; break;
        case T_LT:      irop = IR_CMPLTS; rtype = T_TBOOL; break;
        case T_LE:      irop = IR_CMPLES; rtype = T_TBOOL; break;
        case T_GT:      irop = IR_CMPGTS; rtype = T_TBOOL; break;
        case T_GE:      irop = IR_CMPGES; rtype = T_TBOOL; break;
        default:
            die("lower:%d: unhandled binary op %s",
                n->line, tok_str(n->op));
            return (struct val){ 0, 0 };
        }

        ins = emit(irop);
        ins->dst = new_temp();
        ins->a = lv.temp;
        ins->b = rv.temp;
        return (struct val){ ins->dst, rtype };
    }

    case N_CALL: {
        struct val vargs[16];
        int args[16];
        int nargs = 0;

        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "typeof") == 0) {
            struct val v = lower_expr(n->b);
            if (v.type == T_TPROP) {
                ins = emit(IR_LW);
                ins->dst = new_temp();
                ins->a = v.temp;
                return (struct val){ ins->dst, T_TINT };
            }
            int tag = prop_tag_for_type(v.type);
            if (tag < 0)
                tag = 0;
            return (struct val){ lower_const(tag), T_TINT };
        }

        for (struct node *a = n->b; a; a = a->next) {
            if (nargs >= 16)
                die("lower: too many arguments");
            vargs[nargs] = deref_prop(lower_expr(a));
            args[nargs] = vargs[nargs].temp;
            nargs++;
        }

        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "length") == 0 && nargs == 1) {
            if (vargs[0].type == T_TSTR)
                return (struct val){
                    emit_rtcall("__moo_str_len", 1, args),
                    T_TINT,
                };
            if (vargs[0].type == T_TLIST)
                return (struct val){
                    emit_rtcall("__moo_list_len", 1, args),
                    T_TINT,
                };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "listappend") == 0 &&
            nargs == 2) {
            return (struct val){
                emit_rtcall("__moo_list_append", 2, args),
                T_TLIST,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "listdelete") == 0 &&
            nargs == 2) {
            return (struct val){
                emit_rtcall("__moo_list_delete", 2, args),
                T_TLIST,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "listset") == 0 &&
            nargs == 3) {
            return (struct val){
                emit_rtcall("__moo_list_set", 3, args),
                T_TLIST,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "tostr") == 0 && nargs == 1) {
            if (vargs[0].type == T_TERR)
                return (struct val){
                    emit_rtcall("__moo_tostr_err", 1, args),
                    T_TSTR,
                };
            if (vargs[0].type == T_TFLOAT) {
                int fs = alloc_slot(8);
                int pa;
                ins = emit(IR_FSTL);
                ins->a = args[0];
                ins->slot = fs;
                ins = emit(IR_ADL);
                ins->dst = new_temp();
                ins->slot = fs;
                pa = ins->dst;
                return (struct val){
                    emit_rtcall("__moo_tostr_float",
                                1, &pa),
                    T_TSTR,
                };
            }
            return (struct val){
                emit_rtcall("__moo_tostr", 1, args),
                T_TSTR,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "toint") == 0 && nargs == 1) {
            if (vargs[0].type == T_TFLOAT) {
                ins = emit(IR_FTOI);
                ins->dst = new_temp();
                ins->a = args[0];
                return (struct val){ ins->dst, T_TINT };
            }
            return (struct val){
                emit_rtcall("__moo_toint", 1, args),
                T_TINT,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "tofloat") == 0 && nargs == 1) {
            if (vargs[0].type == T_TINT) {
                ins = emit(IR_ITOF);
                ins->dst = new_temp();
                ins->a = args[0];
                return (struct val){ ins->dst, T_TFLOAT };
            }
            /* str -> float: runtime writes result via pointer */
            {
                int fs = alloc_slot(8);
                int ca[2];
                ins = emit(IR_ADL);
                ins->dst = new_temp();
                ins->slot = fs;
                ca[0] = args[0];
                ca[1] = ins->dst;
                emit_rtcall("__moo_tofloat", 2, ca);
                ins = emit(IR_FLDL);
                ins->dst = new_temp();
                ins->slot = fs;
                return (struct val){ ins->dst, T_TFLOAT };
            }
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "index") == 0 && nargs == 2) {
            return (struct val){
                emit_rtcall("__moo_str_index", 2, args),
                T_TINT,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "substr") == 0 && nargs == 3) {
            return (struct val){
                emit_rtcall("__moo_str_substr", 3, args),
                T_TSTR,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "strsub") == 0 && nargs == 3) {
            return (struct val){
                emit_rtcall("__moo_str_strsub", 3, args),
                T_TSTR,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "valid") == 0 && nargs == 1) {
            return (struct val){
                emit_rtcall("__moo_obj_valid", 1, args),
                T_TBOOL,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "move") == 0 && nargs == 2) {
            return (struct val){
                emit_rtcall("__moo_obj_move", 2, args),
                T_TINT,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "contents") == 0 && nargs == 1) {
            return (struct val){
                emit_rtcall("__moo_obj_contents", 1, args),
                T_TLIST,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "location") == 0 && nargs == 1) {
            return (struct val){
                emit_rtcall("__moo_obj_location", 1, args),
                T_TOBJ,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "create") == 0 && nargs == 1) {
            return (struct val){
                emit_rtcall("__moo_obj_create", 1, args),
                T_TOBJ,
            };
        }
        if (n->a->kind == N_NAME &&
            strcmp(n->a->name, "recycle") == 0 && nargs == 1) {
            return (struct val){
                emit_rtcall("__moo_obj_recycle", 1, args),
                T_TINT,
            };
        }

        for (int i = 0; i < nargs; i++) {
            ins = emit(vargs[i].type == T_TFLOAT ?
                   IR_FARG : IR_ARG);
            ins->a = args[i];
            ins->imm = i;
        }
        if (n->a->kind == N_NAME) {
            const char *sym = n->a->name;
            const char *ln = find_link(sym);
            ins = emit(IR_CALL);
            ins->dst = new_temp();
            ins->sym = arena_strdup(lower_arena,
                ln ? ln : sym);
            ins->nargs = nargs;
            return (struct val){ ins->dst, T_TINT };
        }
        ins = emit(IR_CALLI);
        ins->dst = new_temp();
        ins->a = lower_expr(n->a).temp;
        ins->nargs = nargs;
        return (struct val){ ins->dst, T_TINT };
    }

    case N_RECOVER: {
        int old, zero;
        if (!in_defer)
            die("lower:%d: recover() outside defer block", n->line);
        ins = emit(IR_LDL);
        ins->dst = new_temp();
        ins->slot = panic_slot;
        old = ins->dst;
        zero = lower_const(0);
        ins = emit(IR_STL);
        ins->a = zero;
        ins->slot = panic_slot;
        return (struct val){ old, T_TERR };
    }

    case N_OBJREF: {
        struct ir_global *g;
        static int objctr;
        char buf[32];
        int id = objctr++;

        snprintf(buf, sizeof(buf), "__obj_%d", id);
        g = arena_zalloc(lower_arena, sizeof(*g));
        g->name = arena_strdup(lower_arena, buf);
        g->base_type = IR_I8;
        g->arr_size = n->slen + 1;
        g->is_local = 1;
        g->init_string = arena_alloc(lower_arena, n->slen + 1);
        memcpy(g->init_string, n->sval, n->slen);
        g->init_string[n->slen] = '\0';
        g->init_strlen = n->slen + 1;
        g->next = cur_prog->globals;
        cur_prog->globals = g;

        ins = emit(IR_LEA);
        ins->dst = new_temp();
        ins->sym = arena_strdup(lower_arena, buf);
        return (struct val){ ins->dst, T_TOBJ };
    }

    case N_VCALL: {
        struct val recv;
        int argc;
        int args[18];
        int argtype[18];

        recv = deref_prop(lower_expr(n->a));
        argc = 0;
        for (struct node *a = n->b; a; a = a->next) {
            struct val av;

            if (argc >= 16)
                die("lower: too many verb call arguments");
            av = deref_prop(lower_expr(a));
            args[argc] = av.temp;
            argtype[argc] = av.type;
            argc++;
        }

        if (recv.type == T_TSTR) {
            if (strcmp(n->name, "length") == 0)
                return (struct val){
                    emit_rtcall("__moo_str_len", 1,
                                &recv.temp),
                    T_TINT,
                };
            if (strcmp(n->name, "index") == 0) {
                int ca[2] = { recv.temp, args[0] };
                return (struct val){
                    emit_rtcall("__moo_str_index", 2, ca),
                    T_TINT,
                };
            }
            if (strcmp(n->name, "substr") == 0) {
                int ca[3] = { recv.temp, args[0], args[1] };
                return (struct val){
                    emit_rtcall("__moo_str_substr", 3, ca),
                    T_TSTR,
                };
            }
            if (strcmp(n->name, "strsub") == 0) {
                int ca[3] = { recv.temp, args[0], args[1] };
                return (struct val){
                    emit_rtcall("__moo_str_strsub", 3, ca),
                    T_TSTR,
                };
            }
        }
        if (recv.type == T_TLIST) {
            if (strcmp(n->name, "length") == 0)
                return (struct val){
                    emit_rtcall("__moo_list_len", 1,
                                &recv.temp),
                    T_TINT,
                };
            if (strcmp(n->name, "append") == 0) {
                int ca[2] = { recv.temp, args[0] };
                return (struct val){
                    emit_rtcall("__moo_list_append", 2, ca),
                    T_TLIST,
                };
            }
            if (strcmp(n->name, "delete") == 0) {
                int ca[2] = { recv.temp, args[0] };
                return (struct val){
                    emit_rtcall("__moo_list_delete", 2, ca),
                    T_TLIST,
                };
            }
            if (strcmp(n->name, "set") == 0) {
                int ca[3] = { recv.temp, args[0], args[1] };
                return (struct val){
                    emit_rtcall("__moo_list_set", 3, ca),
                    T_TLIST,
                };
            }
        }

        if (recv.type == T_TINT) {
            if (strcmp(n->name, "tostr") == 0)
                return (struct val){
                    emit_rtcall("__moo_tostr", 1,
                                &recv.temp),
                    T_TSTR,
                };
            if (strcmp(n->name, "tofloat") == 0) {
                ins = emit(IR_ITOF);
                ins->dst = new_temp();
                ins->a = recv.temp;
                return (struct val){ ins->dst, T_TFLOAT };
            }
        }
        if (recv.type == T_TFLOAT) {
            if (strcmp(n->name, "tostr") == 0) {
                int fs = alloc_slot(8);
                int pa;
                ins = emit(IR_FSTL);
                ins->a = recv.temp;
                ins->slot = fs;
                ins = emit(IR_ADL);
                ins->dst = new_temp();
                ins->slot = fs;
                pa = ins->dst;
                return (struct val){
                    emit_rtcall("__moo_tostr_float",
                                1, &pa),
                    T_TSTR,
                };
            }
            if (strcmp(n->name, "toint") == 0) {
                ins = emit(IR_FTOI);
                ins->dst = new_temp();
                ins->a = recv.temp;
                return (struct val){ ins->dst, T_TINT };
            }
        }
        if (recv.type == T_TERR) {
            if (strcmp(n->name, "tostr") == 0)
                return (struct val){
                    emit_rtcall("__moo_tostr_err", 1,
                                &recv.temp),
                    T_TSTR,
                };
        }

        {
            int vname = lower_strlit(n->name,
                                     strlen(n->name));
            int callargs[20];
            int typemask = 0;
            int i;
            /* Two routing bits per argument so the host can place values in
             * the verb's context: 2 = object (-> dobj then iobj), 1 = string
             * (-> argstr), 0 = anything else (not marshalled). */
            for (i = 0; i < argc; i++) {
                int cat = argtype[i] == T_TOBJ ? 2 :
                          argtype[i] == T_TSTR ? 1 : 0;
                typemask |= cat << (2 * i);
            }
            callargs[0] = recv.temp;
            callargs[1] = vname;
            callargs[2] = lower_const(argc);
            callargs[3] = lower_const(typemask);
            for (i = 0; i < argc; i++)
                callargs[4 + i] = args[i];
            emit_rtcall("__moo_verb_call", 4 + argc, callargs);
        }
        return (struct val){ lower_const(0), T_TINT };
    }

    case N_PROP: {
        int obj, prop;
        int argv[2];

        obj = lower_expr(n->a).temp;
        prop = lower_strlit(n->name, strlen(n->name));
        argv[0] = obj;
        argv[1] = prop;
        return (struct val){
            emit_rtcall("__moo_prop_get", 2, argv),
            T_TPROP,
        };
    }

    case N_CPROP: {
        int obj, prop;
        int argv[2];

        obj = lower_expr(n->a).temp;
        prop = lower_expr(n->b).temp;
        argv[0] = obj;
        argv[1] = prop;
        return (struct val){
            emit_rtcall("__moo_prop_get", 2, argv),
            T_TPROP,
        };
    }

    case N_INDEX: {
        int argv[2];

        lv = deref_prop(lower_expr(n->a));
        rv = deref_prop(lower_expr(n->b));
        argv[0] = lv.temp;
        argv[1] = rv.temp;
        return (struct val){
            emit_rtcall("__moo_list_index", 2, argv),
            T_TINT,
        };
    }

    case N_SLICE: {
        struct val sv;
        int argv[3];

        lv = deref_prop(lower_expr(n->a));
        rv = deref_prop(lower_expr(n->b));
        sv = deref_prop(lower_expr(n->c));
        argv[0] = lv.temp;
        argv[1] = rv.temp;
        argv[2] = sv.temp;
        return (struct val){
            emit_rtcall("__moo_list_slice", 3, argv),
            T_TLIST,
        };
    }

    case N_LISTLIT: {
        struct val elems[256];
        int count = 0;
        int argv[1], p, addr, cnt, off;

        for (struct node *e = n->a; e; e = e->next) {
            if (count >= 256)
                die("lower: list literal too large");
            elems[count++] = deref_prop(lower_expr(e));
        }

        argv[0] = lower_const(4 + count * 4);
        p = emit_rtcall("__moo_arena_alloc", 1, argv);

        cnt = lower_const(count);
        ins = emit(IR_SW);
        ins->a = p;
        ins->b = cnt;

        for (int i = 0; i < count; i++) {
            off = lower_const(4 + i * 4);
            ins = emit(IR_ADD);
            ins->dst = new_temp();
            ins->a = p;
            ins->b = off;
            addr = ins->dst;

            ins = emit(IR_SW);
            ins->a = addr;
            ins->b = elems[i].temp;
        }

        return (struct val){ p, T_TLIST };
    }

    case N_IS_EXPR: {
        struct node *idef = find_iface(n->name);
        int obj, result, lpass, lfail, lend;

        if (!idef)
            die("lower:%d: unknown interface '%s'",
                n->line, n->name);

        obj = lower_expr(n->a).temp;
        result = new_temp();
        lpass = new_label();
        lfail = new_label();
        lend = new_label();

        for (struct node *m = idef->a; m; m = m->next) {
            int nm, argv[2], chk, lnext;

            nm = lower_strlit(m->name, strlen(m->name));
            argv[0] = obj;
            argv[1] = nm;
            if (m->kind == N_IFACE_PROP)
                chk = emit_rtcall("__moo_obj_has_prop",
                                  2, argv);
            else
                chk = emit_rtcall("__moo_obj_has_verb",
                                  2, argv);
            lnext = new_label();
            emit_bnz(chk, lnext);
            emit_jmp(lfail);
            emit_label(lnext);
        }
        emit_jmp(lpass);

        emit_label(lfail);
        ins = emit(IR_LIC);
        ins->dst = result;
        ins->imm = 0;
        emit_jmp(lend);

        emit_label(lpass);
        ins = emit(IR_LIC);
        ins->dst = result;
        ins->imm = 1;

        emit_label(lend);
        return (struct val){ result, T_TBOOL };
    }

    case N_AS_EXPR: {
        struct node *idef = find_iface(n->name);
        int obj, chk;

        if (!idef)
            die("lower:%d: unknown interface '%s'",
                n->line, n->name);

        obj = lower_expr(n->a).temp;

        for (struct node *m = idef->a; m; m = m->next) {
            int nm, argv[2], lok;

            nm = lower_strlit(m->name, strlen(m->name));
            argv[0] = obj;
            argv[1] = nm;
            if (m->kind == N_IFACE_PROP)
                chk = emit_rtcall("__moo_obj_has_prop",
                                  2, argv);
            else
                chk = emit_rtcall("__moo_obj_has_verb",
                                  2, argv);
            lok = new_label();
            emit_bnz(chk, lok);
            /* panic $E_TYPE */
            {
                int ev = lower_const(1); /* E_TYPE */
                if (use_defer) {
                    ins = emit(IR_STL);
                    ins->a = ev;
                    ins->slot = panic_slot;
                    emit_jmp(ldefer_run);
                } else {
                    ins = emit(IR_RETV);
                    ins->a = ev;
                }
            }
            emit_label(lok);
        }

        return (struct val){ obj, T_TIFACE };
    }

    default:
        die("lower:%d: unhandled expr kind %d", n->line, n->kind);
        return (struct val){ 0, 0 };
    }
}

/****************************************************************
 * Statement lowering
 ****************************************************************/

static void
lower_stmt(struct node *n)
{
    struct ir_insn *ins;
    int ltrue, lfalse, lend, ltop, lcont, lbrk;
    int t;

    if (!n)
        return;

    switch (n->kind) {
    case N_BLOCK:
        for (struct node *s = n->a; s; s = s->next)
            lower_stmt(s);
        return;

    case N_EXPR_STMT:
        if (n->a)
            (void)lower_expr(n->a);
        return;

    case N_VAR_DECL: {
        int vtype = n->type ? n->type->kind : 0;
        int slot = alloc_slot(vtype == T_TFLOAT ? 8 : 4);
        add_lsym(n->name, slot, vtype);
        if (n->a) {
            struct val ev = lower_expr(n->a);
            if (vtype == T_TFLOAT) {
                if (ev.type == T_TINT) {
                    ins = emit(IR_ITOF);
                    ins->dst = new_temp();
                    ins->a = ev.temp;
                    t = ins->dst;
                } else {
                    t = ev.temp;
                }
                ins = emit(IR_FSTL);
                ins->a = t;
                ins->slot = slot;
            } else {
                if (vtype && vtype != T_TPROP)
                    t = emit_typed_unbox(ev, vtype);
                else if (vtype == T_TPROP)
                    t = ev.temp;
                else
                    t = deref_prop(ev).temp;
                ins = emit(IR_STL);
                ins->a = t;
                ins->slot = slot;
            }
        }
        return;
    }

    case N_ASSIGN: {
        struct lsym *ls;

        if (n->a->kind == N_PROP) {
            int obj, prop, val;
            int argv[3];
            obj = lower_expr(n->a->a).temp;
            prop = lower_strlit(n->a->name,
                                strlen(n->a->name));
            val = deref_prop(lower_expr(n->b)).temp;
            argv[0] = obj;
            argv[1] = prop;
            argv[2] = val;
            emit_rtcall("__moo_prop_set", 3, argv);
            return;
        }
        if (n->a->kind == N_CPROP) {
            int obj, prop, val;
            int argv[3];
            obj = lower_expr(n->a->a).temp;
            prop = lower_expr(n->a->b).temp;
            val = deref_prop(lower_expr(n->b)).temp;
            argv[0] = obj;
            argv[1] = prop;
            argv[2] = val;
            emit_rtcall("__moo_prop_set", 3, argv);
            return;
        }
        if (n->a->kind == N_INDEX) {
            int list, idx, val;
            int argv[3];
            struct lsym *tgt;
            if (n->a->a->kind != N_NAME)
                die("lower:%d: indexed assignment to "
                    "non-variable", n->line);
            tgt = find_lsym(n->a->a->name);
            if (!tgt)
                die("lower:%d: undefined '%s'",
                    n->line, n->a->a->name);
            list = lower_expr(n->a->a).temp;
            idx = lower_expr(n->a->b).temp;
            val = deref_prop(lower_expr(n->b)).temp;
            argv[0] = list;
            argv[1] = idx;
            argv[2] = val;
            t = emit_rtcall("__moo_list_set", 3, argv);
            ins = emit(IR_STL);
            ins->a = t;
            ins->slot = tgt->slot;
            return;
        }

        {
            struct val ev = lower_expr(n->b);
            if (n->a->kind != N_NAME)
                die("lower:%d: assignment to unsupported "
                    "target", n->line);
            ls = find_lsym(n->a->name);
            if (!ls)
                die("lower:%d: undefined '%s'",
                    n->line, n->a->name);
            if (ls->type == T_TFLOAT) {
                if (ev.type == T_TINT) {
                    ins = emit(IR_ITOF);
                    ins->dst = new_temp();
                    ins->a = ev.temp;
                    t = ins->dst;
                } else {
                    t = ev.temp;
                }
            } else if (ls->type && ls->type != T_TPROP) {
                t = emit_typed_unbox(ev, ls->type);
            } else if (ls->type == T_TPROP) {
                t = ev.temp;
            } else {
                t = deref_prop(ev).temp;
            }
        }

        if (ls->type == T_TFLOAT &&
            (n->op == T_PLUSEQ || n->op == T_MINUSEQ)) {
            int cur;
            ins = emit(IR_FLDL);
            ins->dst = new_temp();
            ins->slot = ls->slot;
            cur = ins->dst;
            ins = emit(n->op == T_PLUSEQ ? IR_FADD : IR_FSUB);
            ins->dst = new_temp();
            ins->a = cur;
            ins->b = t;
            t = ins->dst;
        } else if (n->op == T_PLUSEQ || n->op == T_MINUSEQ) {
            int cur;
            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = ls->slot;
            cur = ins->dst;
            ins = emit(n->op == T_PLUSEQ ? IR_ADD : IR_SUB);
            ins->dst = new_temp();
            ins->a = cur;
            ins->b = t;
            t = ins->dst;
        }

        ins = emit(ls->type == T_TFLOAT ? IR_FSTL : IR_STL);
        ins->a = t;
        ins->slot = ls->slot;
        return;
    }

    case N_IF:
        ltrue = new_label();
        lfalse = new_label();
        lend = new_label();
        lower_cond(n->a, ltrue, lfalse);
        emit_label(ltrue);
        lower_stmt(n->b);
        if (n->c) {
            emit_jmp(lend);
            emit_label(lfalse);
            lower_stmt(n->c);
            emit_label(lend);
        } else {
            emit_label(lfalse);
        }
        return;

    case N_WHILE:
        ltop = new_label();
        lbrk = new_label();
        lcont = ltop;
        emit_label(ltop);
        if (n->a->kind == N_BOOL && n->a->ival) {
            /* while true: no condition test */
        } else {
            int lbody = new_label();
            lower_cond(n->a, lbody, lbrk);
            emit_label(lbody);
        }
        if (nloops >= 64)
            die("lower: nested loops too deep");
        loops[nloops].brk = lbrk;
        loops[nloops].cont = lcont;
        nloops++;
        lower_stmt(n->b);
        nloops--;
        emit_jmp(ltop);
        emit_label(lbrk);
        return;

    case N_FOR: {
        int slot, lo, hi, hislot, curval, hilim, next, one;
        int ltop2, lbody, lincr;

        if (!n->b) {
            int cslot, lslot, islot, cidx, clen, elem;
            int argv[2];
            struct val coll;

            coll = lower_expr(n->a);

            cslot = alloc_slot(4);
            ins = emit(IR_STL);
            ins->a = coll.temp;
            ins->slot = cslot;

            argv[0] = coll.temp;
            lslot = alloc_slot(4);
            clen = emit_rtcall("__moo_list_len", 1, argv);
            ins = emit(IR_STL);
            ins->a = clen;
            ins->slot = lslot;

            slot = alloc_slot(4);
            add_lsym(n->name, slot, T_TINT);

            islot = alloc_slot(4);
            cidx = lower_const(1);
            ins = emit(IR_STL);
            ins->a = cidx;
            ins->slot = islot;

            ltop2 = new_label();
            lbrk = new_label();
            lincr = new_label();

            emit_label(ltop2);

            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = islot;
            cidx = ins->dst;

            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = lslot;
            clen = ins->dst;

            ins = emit(IR_CMPGTS);
            ins->dst = new_temp();
            ins->a = cidx;
            ins->b = clen;
            emit_bnz(ins->dst, lbrk);

            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = cslot;
            argv[0] = ins->dst;

            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = islot;
            argv[1] = ins->dst;

            elem = emit_rtcall("__moo_list_index", 2, argv);
            ins = emit(IR_STL);
            ins->a = elem;
            ins->slot = slot;

            if (nloops >= 64)
                die("lower: nested loops too deep");
            loops[nloops].brk = lbrk;
            loops[nloops].cont = lincr;
            nloops++;
            lower_stmt(n->c);
            nloops--;

            emit_label(lincr);
            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = islot;
            curval = ins->dst;
            one = lower_const(1);
            ins = emit(IR_ADD);
            ins->dst = new_temp();
            ins->a = curval;
            ins->b = one;
            next = ins->dst;
            ins = emit(IR_STL);
            ins->a = next;
            ins->slot = islot;
            emit_jmp(ltop2);
            emit_label(lbrk);
            return;
        }

        slot = alloc_slot(4);
        add_lsym(n->name, slot, T_TINT);

        lo = lower_expr(n->a).temp;
        ins = emit(IR_STL);
        ins->a = lo;
        ins->slot = slot;

        hi = lower_expr(n->b).temp;
        hislot = alloc_slot(4);
        ins = emit(IR_STL);
        ins->a = hi;
        ins->slot = hislot;

        ltop2 = new_label();
        lbody = new_label();
        lbrk = new_label();
        lincr = new_label();

        emit_label(ltop2);

        ins = emit(IR_LDL);
        ins->dst = new_temp();
        ins->slot = slot;
        curval = ins->dst;

        ins = emit(IR_LDL);
        ins->dst = new_temp();
        ins->slot = hislot;
        hilim = ins->dst;

        ins = emit(IR_CMPGTS);
        ins->dst = new_temp();
        ins->a = curval;
        ins->b = hilim;
        emit_bnz(ins->dst, lbrk);

        emit_label(lbody);

        if (nloops >= 64)
            die("lower: nested loops too deep");
        loops[nloops].brk = lbrk;
        loops[nloops].cont = lincr;
        nloops++;
        lower_stmt(n->c);
        nloops--;

        emit_label(lincr);
        ins = emit(IR_LDL);
        ins->dst = new_temp();
        ins->slot = slot;
        curval = ins->dst;
        one = lower_const(1);
        ins = emit(IR_ADD);
        ins->dst = new_temp();
        ins->a = curval;
        ins->b = one;
        next = ins->dst;
        ins = emit(IR_STL);
        ins->a = next;
        ins->slot = slot;
        emit_jmp(ltop2);
        emit_label(lbrk);
        return;
    }

    case N_BREAK:
        if (nloops == 0)
            die("lower:%d: break outside loop", n->line);
        emit_jmp(loops[nloops - 1].brk);
        return;

    case N_CONTINUE:
        if (nloops == 0)
            die("lower:%d: continue outside loop", n->line);
        emit_jmp(loops[nloops - 1].cont);
        return;

    case N_RETURN:
        if (in_defer)
            die("lower:%d: return inside defer not supported",
                n->line);
        if (use_defer) {
            t = n->a ? deref_prop(lower_expr(n->a)).temp
                     : lower_const(0);
            ins = emit(IR_STL);
            ins->a = t;
            ins->slot = retval_slot;
            emit_jmp(ldefer_run);
        } else if (n->a) {
            t = deref_prop(lower_expr(n->a)).temp;
            ins = emit(IR_RETV);
            ins->a = t;
        } else if (use_return_push) {
            int base, cnt;
            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = ret_count_slot;
            cnt = ins->dst;
            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = ret_base_slot;
            base = ins->dst;
            ins = emit(IR_SW);
            ins->a = base;
            ins->b = cnt;
            ins = emit(IR_RETV);
            ins->a = base;
        } else {
            emit(IR_RET);
        }
        return;

    case N_RETURN_PUSH: {
        int val, argv[1], addr, cnt, one, cnt1;
        val = deref_prop(lower_expr(n->a)).temp;
        argv[0] = lower_const(4);
        addr = emit_rtcall("__moo_arena_alloc", 1, argv);
        ins = emit(IR_SW);
        ins->a = addr;
        ins->b = val;
        ins = emit(IR_LDL);
        ins->dst = new_temp();
        ins->slot = ret_count_slot;
        cnt = ins->dst;
        one = lower_const(1);
        ins = emit(IR_ADD);
        ins->dst = new_temp();
        ins->a = cnt;
        ins->b = one;
        cnt1 = ins->dst;
        ins = emit(IR_STL);
        ins->a = cnt1;
        ins->slot = ret_count_slot;
        return;
    }

    case N_DEFER:
        if (ndefers >= MAX_DEFERS)
            die("lower:%d: too many defer blocks", n->line);
        {
            int one = lower_const(1);
            ins = emit(IR_STL);
            ins->a = one;
            ins->slot = defer_flags[ndefers];
        }
        defer_bodies[ndefers] = n->a;
        ndefers++;
        return;

    case N_PANIC_STMT:
        if (in_defer)
            die("lower:%d: panic inside defer not supported",
                n->line);
        t = deref_prop(lower_expr(n->a)).temp;
        if (use_defer) {
            ins = emit(IR_STL);
            ins->a = t;
            ins->slot = panic_slot;
            emit_jmp(ldefer_run);
        } else {
            ins = emit(IR_RETV);
            ins->a = t;
        }
        return;

    case N_SWITCH: {
        struct val sv;
        int sslot, lend;

        sv = lower_expr(n->a);
        sslot = alloc_slot(4);
        ins = emit(IR_STL);
        ins->a = sv.temp;
        ins->slot = sslot;

        lend = new_label();

        for (struct node *c = n->b; c; c = c->next) {
            if (c->ival) {
                lower_stmt(c->b);
            } else {
                int lbody = new_label();
                int lnext = new_label();

                for (struct node *v = c->a; v; v = v->next) {
                    int sval;
                    ins = emit(IR_LDL);
                    ins->dst = new_temp();
                    ins->slot = sslot;
                    sval = ins->dst;

                    if (v->b) {
                        int lo, hi, lfail;
                        lo = lower_expr(v->a).temp;
                        hi = lower_expr(v->b).temp;
                        lfail = new_label();

                        ins = emit(IR_CMPLTS);
                        ins->dst = new_temp();
                        ins->a = sval;
                        ins->b = lo;
                        emit_bnz(ins->dst, lfail);

                        ins = emit(IR_CMPGTS);
                        ins->dst = new_temp();
                        ins->a = sval;
                        ins->b = hi;
                        emit_bnz(ins->dst, lfail);

                        emit_jmp(lbody);
                        emit_label(lfail);
                    } else if (sv.type == T_TSTR) {
                        int pval, argv[2], eq;
                        pval = lower_expr(v->a).temp;
                        argv[0] = sval;
                        argv[1] = pval;
                        eq = emit_rtcall(
                            "__moo_str_eq", 2, argv);
                        emit_bnz(eq, lbody);
                    } else {
                        int pval;
                        pval = lower_expr(v->a).temp;
                        ins = emit(IR_CMPEQ);
                        ins->dst = new_temp();
                        ins->a = sval;
                        ins->b = pval;
                        emit_bnz(ins->dst, lbody);
                    }
                }
                emit_jmp(lnext);

                emit_label(lbody);
                lower_stmt(c->b);
                emit_jmp(lend);

                emit_label(lnext);
            }
        }

        emit_label(lend);
        return;
    }

    case N_TYPESWITCH: {
        struct val sv;
        int sslot, lend2;

        sv = lower_expr(n->a);
        sslot = alloc_slot(4);
        ins = emit(IR_STL);
        ins->a = sv.temp;
        ins->slot = sslot;

        lend2 = new_label();

        for (struct node *c = n->b; c; c = c->next) {
            if (c->ival) {
                lower_stmt(c->b);
            } else {
                struct node *idef = find_iface(c->name);
                int lnext2 = new_label();
                int obj2;

                if (!idef)
                    die("lower:%d: unknown interface "
                        "'%s'", c->line, c->name);

                ins = emit(IR_LDL);
                ins->dst = new_temp();
                ins->slot = sslot;
                obj2 = ins->dst;

                for (struct node *m = idef->a; m;
                     m = m->next) {
                    int nm, argv[2], chk, lok;

                    nm = lower_strlit(m->name,
                                      strlen(m->name));
                    argv[0] = obj2;
                    argv[1] = nm;
                    if (m->kind == N_IFACE_PROP)
                        chk = emit_rtcall(
                            "__moo_obj_has_prop",
                            2, argv);
                    else
                        chk = emit_rtcall(
                            "__moo_obj_has_verb",
                            2, argv);
                    lok = new_label();
                    emit_bnz(chk, lok);
                    emit_jmp(lnext2);
                    emit_label(lok);
                }

                lower_stmt(c->b);
                emit_jmp(lend2);

                emit_label(lnext2);
            }
        }

        emit_label(lend2);
        return;
    }

    case N_TRACE_STMT:
    case N_TRACE_CMT:
        return;

    default:
        die("lower:%d: unhandled stmt kind %d", n->line, n->kind);
    }
}

/****************************************************************
 * Function lowering
 ****************************************************************/

static struct ir_func *
lower_function(struct node *fn_ast)
{
    struct ir_func *fn;
    struct ir_insn *ins;
    int nparams = 0;
    int nd, z;

    fn = ir_new_func(lower_arena, fn_ast->name);
    fn->is_local = (fn_ast->kind == N_FUNC && !fn_ast->exported);
    cur_fn = fn;
    nloops = 0;
    nlsyms = 0;
    nslots = 0;
    ndefers = 0;
    in_defer = 0;
    use_return_push = has_return_push(fn_ast->b);

    for (struct node *p = fn_ast->a; p; p = p->next) {
        int pk = p->type ? p->type->kind : 0;
        int slot = alloc_slot(pk == T_TFLOAT ? 8 : 4);
        add_lsym(p->name, slot, pk);
        nparams++;
    }
    fn->nparams = nparams;

    nd = count_defers_in(fn_ast->b);
    use_defer = (nd > 0);

    ins = emit(IR_FUNC);
    ins->sym = arena_strdup(lower_arena, fn_ast->name);
    ins->nargs = nparams;

    if (use_return_push) {
        int argv[1], base, zero;
        ret_base_slot = alloc_slot(4);
        ret_count_slot = alloc_slot(4);
        argv[0] = lower_const(4);
        base = emit_rtcall("__moo_arena_alloc", 1, argv);
        ins = emit(IR_STL);
        ins->a = base;
        ins->slot = ret_base_slot;
        zero = lower_const(0);
        ins = emit(IR_SW);
        ins->a = base;
        ins->b = zero;
        ins = emit(IR_STL);
        ins->a = zero;
        ins->slot = ret_count_slot;
    }

    if (use_defer) {
        panic_slot = alloc_slot(4);
        retval_slot = alloc_slot(4);
        z = lower_const(0);
        ins = emit(IR_STL);
        ins->a = z;
        ins->slot = panic_slot;
        z = lower_const(0);
        ins = emit(IR_STL);
        ins->a = z;
        ins->slot = retval_slot;

        for (int i = 0; i < nd; i++) {
            defer_flags[i] = alloc_slot(4);
            z = lower_const(0);
            ins = emit(IR_STL);
            ins->a = z;
            ins->slot = defer_flags[i];
        }

        ldefer_run = new_label();
    }

    lower_stmt(fn_ast->b);

    if (use_defer) {
        z = lower_const(0);
        ins = emit(IR_STL);
        ins->a = z;
        ins->slot = retval_slot;
        emit_jmp(ldefer_run);

        emit_label(ldefer_run);
        for (int i = ndefers - 1; i >= 0; i--) {
            int lskip = new_label();
            int flag;
            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = defer_flags[i];
            flag = ins->dst;
            ins = emit(IR_BZ);
            ins->a = flag;
            ins->label = lskip;
            in_defer = 1;
            lower_stmt(defer_bodies[i]);
            in_defer = 0;
            emit_label(lskip);
        }

        {
            int pv, rv, lpanic;
            lpanic = new_label();
            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = panic_slot;
            pv = ins->dst;
            emit_bnz(pv, lpanic);

            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = retval_slot;
            rv = ins->dst;
            ins = emit(IR_RETV);
            ins->a = rv;

            emit_label(lpanic);
            ins = emit(IR_LDL);
            ins->dst = new_temp();
            ins->slot = panic_slot;
            pv = ins->dst;
            ins = emit(IR_RETV);
            ins->a = pv;
        }
    } else {
        if (!fn->tail ||
            (fn->tail->op != IR_RET && fn->tail->op != IR_RETV)) {
            z = lower_const(0);
            ins = emit(IR_RETV);
            ins->a = z;
        }
    }

    emit(IR_ENDF);

    fn->nslots = nslots;
    fn->slot_size = arena_alloc(lower_arena, nslots * sizeof(int));
    memcpy(fn->slot_size, slot_sizes, nslots * sizeof(int));
    return fn;
}

/****************************************************************
 * Entry point
 ****************************************************************/

struct ir_program *
lower_program(struct arena *a, struct node *ast)
{
    struct ir_program *p;
    struct node *d;
    struct ir_func *fn, **ftail;

    lower_arena = a;
    p = arena_zalloc(lower_arena, sizeof(*p));
    cur_prog = p;
    nverbs = 0;
    ncvals = 0;
    nlinks = 0;

    niface_defs = 0;
    for (d = ast->a; d; d = d->next) {
        if (d->kind == N_VERB || d->kind == N_FUNC)
            register_verb(d->name);
        else if (d->kind == N_EXTERN_VERB ||
                 d->kind == N_EXTERN_FUNC) {
            const char *sym;
            register_verb(d->name);
            sym = d->link_name ? d->link_name : d->name;
            if (d->link_name)
                add_link(d->name, sym);
        } else if (d->kind == N_INTERFACE)
            register_iface(d);
        else if (d->kind == N_CONST_DECL) {
            if (!d->a || d->a->kind != N_NUM)
                die("lower:%d: constant '%s' requires "
                    "integer initializer",
                    d->line, d->name);
            add_const(d->name, d->a->ival);
        }
    }

    {
        const char *rt[] = {
            "write", "exit",
            "__moo_arena_alloc", "__moo_arena_reset",
            "__moo_str_concat", "__moo_str_eq", "__moo_str_len",
            "__moo_str_index", "__moo_str_substr", "__moo_str_strsub",
            "__moo_tostr", "__moo_tostr_err", "__moo_tostr_float",
            "__moo_toint", "__moo_tofloat",
            "__moo_list_index", "__moo_list_len",
            "__moo_list_append", "__moo_list_delete",
            "__moo_list_set", "__moo_list_slice",
            "__moo_list_contains",
            "__moo_prop_get", "__moo_prop_set",
            "__moo_verb_call",
            "__moo_obj_valid", "__moo_obj_move",
            "__moo_obj_create", "__moo_obj_recycle",
            "__moo_obj_location", "__moo_obj_contents",
            "__moo_obj_has_prop", "__moo_obj_has_verb",
            NULL,
        };
        for (int i = 0; rt[i]; i++)
            if (!is_verb(rt[i]))
                register_verb(rt[i]);
    }

    ftail = &p->funcs;
    for (d = ast->a; d; d = d->next) {
        if (d->kind != N_VERB && d->kind != N_FUNC)
            continue;
        fn = lower_function(d);
        *ftail = fn;
        ftail = &fn->next;
    }

    free(lsyms);
    lsyms = NULL;
    nlsyms = lsym_cap = 0;

    free(slot_sizes);
    slot_sizes = NULL;
    nslots = slot_cap = 0;

    free(verbs);
    verbs = NULL;
    nverbs = verb_cap = 0;

    free(links);
    links = NULL;
    nlinks = link_cap = 0;

    free(iface_defs);
    iface_defs = NULL;
    niface_defs = iface_def_cap = 0;

    free(cvals);
    cvals = NULL;
    ncvals = cval_cap = 0;

    return p;
}
