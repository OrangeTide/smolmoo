/* parse.c : two-pass GAS syntax parser for ColdFire assembler */

#include "as.h"

#include <stdlib.h>
#include <string.h>

/****************************************************************
 * Register parsing
 ****************************************************************/

static int
parse_dreg(const char *name, int *reg)
{
    if (name[0] == 'd' && name[1] >= '0' && name[1] <= '7' && name[2] == '\0') {
        *reg = name[1] - '0';
        return 1;
    }
    return 0;
}

static int
parse_areg(const char *name, int *reg)
{
    if (name[0] == 'a' && name[1] >= '0' && name[1] <= '7' && name[2] == '\0') {
        *reg = name[1] - '0';
        return 1;
    }
    if (strcmp(name, "sp") == 0) { *reg = 7; return 1; }
    if (strcmp(name, "fp") == 0) { *reg = 6; return 1; }
    return 0;
}

static int
parse_fpreg(const char *name, int *reg)
{
    if (name[0] == 'f' && name[1] == 'p'
        && name[2] >= '0' && name[2] <= '7' && name[3] == '\0') {
        *reg = name[2] - '0';
        return 1;
    }
    return 0;
}

/****************************************************************
 * Mnemonic + size suffix splitting
 ****************************************************************/

static int
is_size_suffix(const char *s)
{
    return s[0] == '.' && s[1] != '\0' && s[2] == '\0' &&
           (s[1] == 'b' || s[1] == 'w' || s[1] == 'l' ||
            s[1] == 's' || s[1] == 'd' || s[1] == 'x');
}

static int
split_mnemonic(const char *tok, char *base, int *size)
{
    int len = (int)strlen(tok);
    int dot_pos = -1;
    int k;

    for (k = 0; k < len; k++) {
        if (tok[k] == '.') {
            dot_pos = k;
            break;
        }
    }

    if (dot_pos < 0) {
        strcpy(base, tok);
        *size = 0;
        return 1;
    }

    memcpy(base, tok, dot_pos);
    base[dot_pos] = '\0';

    if (dot_pos + 1 < len) {
        switch (tok[dot_pos + 1]) {
        case 'b': *size = 1; break;
        case 'w': *size = 2; break;
        case 'l': *size = 4; break;
        case 's': *size = 4; break;
        case 'd': *size = 8; break;
        case 'x': *size = 0; break;
        default: *size = 0; break;
        }
    } else {
        *size = 0;
    }

    return 1;
}

/****************************************************************
 * Register list parsing (for movem)
 ****************************************************************/

static uint16_t
reglist_mask(int type, int lo, int hi)
{
    uint16_t mask = 0;
    int k;

    for (k = lo; k <= hi; k++) {
        if (type == 'd')
            mask |= (uint16_t)(1 << k);
        else
            mask |= (uint16_t)(1 << (k + 8));
    }
    return mask;
}

/****************************************************************
 * Operand parsing
 ****************************************************************/

static int
parse_operand(struct lexer *l, struct operand *op, int allow_reglist)
{
    int reg;

    memset(op, 0, sizeof(*op));

    if (l->tok.type == T_HASH) {
        lex_next(l);
        op->type = OP_IMM;
        op->imm = 0;
        op->sym = NULL;
        if (l->tok.type == T_MINUS) {
            lex_next(l);
            if (l->tok.type == T_INT) {
                op->imm = -l->tok.ival;
                lex_next(l);
            } else {
                return 0;
            }
        } else if (l->tok.type == T_INT) {
            op->imm = l->tok.ival;
            lex_next(l);
        } else if (l->tok.type == T_IDENT || l->tok.type == T_DOT_IDENT) {
            op->sym = arena_strdup(l->arena, l->tok.str);
            lex_next(l);
        } else {
            return 0;
        }
        return 1;
    }

    if (l->tok.type == T_MINUS) {
        lex_next(l);
        if (l->tok.type == T_LPAREN) {
            lex_next(l);
            if (l->tok.type == T_IDENT && parse_areg(l->tok.str, &reg)) {
                lex_next(l);
                if (l->tok.type == T_RPAREN) {
                    lex_next(l);
                    op->type = OP_PREDEC;
                    op->reg = reg;
                    return 1;
                }
            }
            return 0;
        }
        if (l->tok.type == T_INT) {
            long val = -l->tok.ival;
            lex_next(l);
            if (l->tok.type == T_LPAREN) {
                lex_next(l);
                if (l->tok.type == T_IDENT && parse_areg(l->tok.str, &reg)) {
                    lex_next(l);
                    if (l->tok.type == T_RPAREN) {
                        lex_next(l);
                        op->type = OP_DISP;
                        op->reg = reg;
                        op->imm = val;
                        return 1;
                    }
                }
            }
            return 0;
        }
        return 0;
    }

    if (l->tok.type == T_LPAREN) {
        lex_next(l);
        if (l->tok.type == T_IDENT && parse_areg(l->tok.str, &reg)) {
            lex_next(l);
            if (l->tok.type == T_RPAREN) {
                lex_next(l);
                if (l->tok.type == T_PLUS) {
                    lex_next(l);
                    op->type = OP_POSTINC;
                    op->reg = reg;
                    return 1;
                }
                op->type = OP_IND;
                op->reg = reg;
                return 1;
            }
        }
        return 0;
    }

    if (l->tok.type == T_INT) {
        long val = l->tok.ival;
        lex_next(l);
        if (l->tok.type == T_LPAREN) {
            lex_next(l);
            if (l->tok.type == T_IDENT && parse_areg(l->tok.str, &reg)) {
                lex_next(l);
                if (l->tok.type == T_RPAREN) {
                    lex_next(l);
                    op->type = OP_DISP;
                    op->reg = reg;
                    op->imm = val;
                    return 1;
                }
            }
            return 0;
        }
        op->type = OP_IMM;
        op->imm = val;
        return 1;
    }

    if (l->tok.type == T_IDENT) {
        if (parse_dreg(l->tok.str, &reg)) {
            lex_next(l);
            if (allow_reglist && l->tok.type == T_MINUS) {
                struct lexer saved = *l;
                lex_next(l);
                if (l->tok.type == T_IDENT) {
                    int hi;
                    if (parse_dreg(l->tok.str, &hi)) {
                        op->type = OP_REGLIST;
                        op->regmask = reglist_mask('d', reg, hi);
                        lex_next(l);
                        return 1;
                    }
                }
                *l = saved;
            }
            op->type = OP_DREG;
            op->reg = reg;
            return 1;
        }
        if (parse_areg(l->tok.str, &reg)) {
            lex_next(l);
            op->type = OP_AREG;
            op->reg = reg;
            return 1;
        }
        if (parse_fpreg(l->tok.str, &reg)) {
            lex_next(l);
            op->type = OP_FPREG;
            op->reg = reg;
            return 1;
        }
        op->type = OP_ABS;
        op->sym = arena_strdup(l->arena, l->tok.str);
        lex_next(l);
        return 1;
    }

    if (l->tok.type == T_DOT_IDENT) {
        op->type = OP_ABS;
        op->sym = arena_strdup(l->arena, l->tok.str);
        lex_next(l);
        return 1;
    }

    return 0;
}

/****************************************************************
 * Directive handling
 ****************************************************************/

static void
skip_to_eol(struct lexer *l)
{
    while (l->tok.type != T_NEWLINE && l->tok.type != T_EOF)
        lex_next(l);
}

static void
handle_directive(struct assembler *a, const char *dir)
{
    struct section *s = &a->sections[a->cur_section];

    if (strcmp(dir, ".text") == 0) {
        a->cur_section = SEC_TEXT;
        return;
    }
    if (strcmp(dir, ".data") == 0) {
        a->cur_section = SEC_DATA;
        return;
    }
    if (strcmp(dir, ".bss") == 0) {
        a->cur_section = SEC_BSS;
        return;
    }

    if (strcmp(dir, ".align") == 0) {
        lex_next(&a->lex);
        if (a->lex.tok.type == T_INT) {
            int align = (int)a->lex.tok.ival;
            /* record the section's strongest alignment so the ELF writer can
               set sh_addralign and the linker places the section aligned */
            if (align > s->align)
                s->align = align;
            if (a->pass == 2)
                sec_align(s, align);
            else {
                while (s->len % align) s->len++;
            }
            lex_next(&a->lex);
        }
        return;
    }

    if (strcmp(dir, ".globl") == 0) {
        lex_next(&a->lex);
        if (a->lex.tok.type == T_IDENT || a->lex.tok.type == T_DOT_IDENT) {
            int idx = sym_lookup(&a->st, a->lex.tok.str);
            if (idx < 0)
                idx = sym_add(&a->st, a->lex.tok.str);
            sym_set_global(&a->st, idx);
            lex_next(&a->lex);
        }
        return;
    }

    if (strcmp(dir, ".long") == 0) {
        for (;;) {
            lex_next(&a->lex);
            if (a->lex.tok.type == T_INT) {
                if (a->pass == 2)
                    sec_emit32(s, (uint32_t)a->lex.tok.ival);
                else
                    s->len += 4;
                lex_next(&a->lex);
            } else if (a->lex.tok.type == T_MINUS) {
                lex_next(&a->lex);
                if (a->lex.tok.type == T_INT) {
                    if (a->pass == 2)
                        sec_emit32(s, (uint32_t)(-(int32_t)a->lex.tok.ival));
                    else
                        s->len += 4;
                    lex_next(&a->lex);
                }
            } else if (a->lex.tok.type == T_IDENT
                       || a->lex.tok.type == T_DOT_IDENT) {
                if (a->pass == 2) {
                    int idx = sym_lookup(&a->st, a->lex.tok.str);
                    if (idx < 0)
                        idx = sym_add(&a->st, a->lex.tok.str);
                    sec_add_reloc(s, s->len, idx, 0);
                    sec_emit32(s, 0);
                } else {
                    s->len += 4;
                }
                lex_next(&a->lex);
            } else {
                break;
            }
            if (a->lex.tok.type != T_COMMA) break;
        }
        return;
    }

    if (strcmp(dir, ".short") == 0) {
        for (;;) {
            lex_next(&a->lex);
            if (a->lex.tok.type == T_INT) {
                if (a->pass == 2)
                    sec_emit16(s, (uint16_t)a->lex.tok.ival);
                else
                    s->len += 2;
                lex_next(&a->lex);
            } else if (a->lex.tok.type == T_MINUS) {
                lex_next(&a->lex);
                if (a->lex.tok.type == T_INT) {
                    if (a->pass == 2)
                        sec_emit16(s, (uint16_t)(-(int16_t)a->lex.tok.ival));
                    else
                        s->len += 2;
                    lex_next(&a->lex);
                }
            } else {
                break;
            }
            if (a->lex.tok.type != T_COMMA) break;
        }
        return;
    }

    if (strcmp(dir, ".byte") == 0) {
        for (;;) {
            lex_next(&a->lex);
            if (a->lex.tok.type == T_INT) {
                if (a->pass == 2)
                    sec_emit8(s, (uint8_t)a->lex.tok.ival);
                else
                    s->len += 1;
                lex_next(&a->lex);
            } else if (a->lex.tok.type == T_MINUS) {
                lex_next(&a->lex);
                if (a->lex.tok.type == T_INT) {
                    if (a->pass == 2)
                        sec_emit8(s, (uint8_t)(-(int8_t)a->lex.tok.ival));
                    else
                        s->len += 1;
                    lex_next(&a->lex);
                }
            } else {
                break;
            }
            if (a->lex.tok.type != T_COMMA) break;
        }
        return;
    }

    if (strcmp(dir, ".ascii") == 0) {
        lex_next(&a->lex);
        if (a->lex.tok.type == T_STRING) {
            if (a->pass == 2) {
                int k;
                for (k = 0; k < a->lex.tok.str_len; k++)
                    sec_emit8(s, (uint8_t)a->lex.tok.str[k]);
            } else {
                s->len += a->lex.tok.str_len;
            }
            lex_next(&a->lex);
        }
        return;
    }

    if (strcmp(dir, ".asciz") == 0 || strcmp(dir, ".string") == 0) {
        lex_next(&a->lex);
        if (a->lex.tok.type == T_STRING) {
            if (a->pass == 2) {
                int k;
                for (k = 0; k < a->lex.tok.str_len; k++)
                    sec_emit8(s, (uint8_t)a->lex.tok.str[k]);
                sec_emit8(s, 0);
            } else {
                s->len += a->lex.tok.str_len + 1;
            }
            lex_next(&a->lex);
        }
        return;
    }

    if (strcmp(dir, ".space") == 0) {
        lex_next(&a->lex);
        if (a->lex.tok.type == T_INT) {
            if (a->pass == 2)
                sec_space(s, (int)a->lex.tok.ival);
            else
                s->len += (int)a->lex.tok.ival;
            lex_next(&a->lex);
        }
        return;
    }

    warn("line %d: unknown directive '%s'", a->lex.tok.line, dir);
    skip_to_eol(&a->lex);
}

/****************************************************************
 * Local numeric label support (1:, 1b, 1f)
 ****************************************************************/

#define MAX_LOCAL_LABELS 10

struct local_label {
    uint32_t offsets[256];
    int count;
    int section[256];
};

static struct local_label local_labels[MAX_LOCAL_LABELS];

static void
local_label_reset(void)
{
    memset(local_labels, 0, sizeof(local_labels));
}

static void
local_label_define(int n, int section, uint32_t offset)
{
    struct local_label *ll;
    if (n < 0 || n >= MAX_LOCAL_LABELS) return;
    ll = &local_labels[n];
    if (ll->count < 256) {
        ll->section[ll->count] = section;
        ll->offsets[ll->count] = offset;
        ll->count++;
    }
}

static int
local_label_find_back(int n, int section, uint32_t offset, uint32_t *result)
{
    struct local_label *ll;
    int k;

    if (n < 0 || n >= MAX_LOCAL_LABELS) return 0;
    ll = &local_labels[n];
    for (k = ll->count - 1; k >= 0; k--) {
        if (ll->section[k] == section && ll->offsets[k] <= offset) {
            *result = ll->offsets[k];
            return 1;
        }
    }
    return 0;
}

static int
local_label_find_fwd(int n, int section, uint32_t offset, uint32_t *result)
{
    struct local_label *ll;
    int k;

    if (n < 0 || n >= MAX_LOCAL_LABELS) return 0;
    ll = &local_labels[n];
    for (k = 0; k < ll->count; k++) {
        if (ll->section[k] == section && ll->offsets[k] > offset) {
            *result = ll->offsets[k];
            return 1;
        }
    }
    return 0;
}

/****************************************************************
 * Instruction line parsing + emission
 ****************************************************************/

static void
parse_instruction(struct assembler *a, const char *premnemonic)
{
    char mnemonic[32];
    int size;
    struct operand op1, op2;
    int nops = 0;
    int result;
    int is_movem;

    memset(&op1, 0, sizeof(op1));
    memset(&op2, 0, sizeof(op2));

    if (premnemonic) {
        if (strchr(premnemonic, '.') != NULL) {
            split_mnemonic(premnemonic, mnemonic, &size);
        } else {
            strncpy(mnemonic, premnemonic, sizeof(mnemonic) - 1);
            mnemonic[sizeof(mnemonic) - 1] = '\0';
            size = 0;
            if (a->lex.tok.type == T_DOT_IDENT &&
                is_size_suffix(a->lex.tok.str)) {
                char combined[64];
                snprintf(combined, sizeof(combined), "%s%s", mnemonic,
                         a->lex.tok.str);
                split_mnemonic(combined, mnemonic, &size);
                lex_next(&a->lex);
            }
        }
    } else if (a->lex.tok.type == T_IDENT
               && strchr(a->lex.tok.str, '.') != NULL) {
        split_mnemonic(a->lex.tok.str, mnemonic, &size);
        lex_next(&a->lex);
    } else if (a->lex.tok.type == T_IDENT) {
        strncpy(mnemonic, a->lex.tok.str, sizeof(mnemonic) - 1);
        mnemonic[sizeof(mnemonic) - 1] = '\0';
        size = 0;
        lex_next(&a->lex);
        if (a->lex.tok.type == T_DOT_IDENT &&
            is_size_suffix(a->lex.tok.str)) {
            char combined[64];
            snprintf(combined, sizeof(combined), "%s%s", mnemonic,
                     a->lex.tok.str);
            split_mnemonic(combined, mnemonic, &size);
            lex_next(&a->lex);
        }
    } else {
        skip_to_eol(&a->lex);
        return;
    }

    is_movem = (strcmp(mnemonic, "movem") == 0);

    if (a->lex.tok.type != T_NEWLINE && a->lex.tok.type != T_EOF) {
        if (!parse_operand(&a->lex, &op1, is_movem)) {
            die("line %d: bad operand for '%s'",
                a->lex.tok.line, mnemonic);
        }
        nops = 1;

        if (a->lex.tok.type == T_COMMA) {
            lex_next(&a->lex);
            if (!parse_operand(&a->lex, &op2, is_movem)) {
                die("line %d: bad second operand for '%s'",
                    a->lex.tok.line, mnemonic);
            }
            nops = 2;
        }
    }

    if (op1.type == OP_ABS && op1.sym) {
        int slen = (int)strlen(op1.sym);
        if (slen >= 2 && (op1.sym[slen - 1] == 'b' || op1.sym[slen - 1] == 'f')
            && op1.sym[0] >= '0' && op1.sym[0] <= '9' && slen <= 3) {
            int n = op1.sym[0] - '0';
            char dir = op1.sym[slen - 1];
            struct section *s = &a->sections[a->cur_section];
            uint32_t target;

            if (dir == 'b') {
                if (local_label_find_back(n, a->cur_section, s->len, &target)) {
                    op1.type = OP_ABS;
                    op1.imm = (long)target - (long)s->len - 2;
                    op1.sym = NULL;
                }
            } else {
                if (local_label_find_fwd(n, a->cur_section, s->len, &target)) {
                    op1.type = OP_ABS;
                    op1.imm = (long)target - (long)s->len - 2;
                    op1.sym = NULL;
                }
            }
        }
    }

    if (a->pass == 1) {
        struct section *s = &a->sections[a->cur_section];
        result = encode_size(mnemonic, size,
                             nops >= 1 ? &op1 : NULL,
                             nops >= 2 ? &op2 : NULL);
        if (result < 0) {
            die("line %d: cannot encode '%s'", a->lex.tok.line, mnemonic);
        }
        s->len += result;
    } else {
        result = encode_insn(a, mnemonic, size,
                             nops >= 1 ? &op1 : NULL,
                             nops >= 2 ? &op2 : NULL);
        if (result < 0) {
            die("line %d: cannot encode '%s'", a->lex.tok.line, mnemonic);
        }
    }

    skip_to_eol(&a->lex);
}

/****************************************************************
 * Main pass logic
 ****************************************************************/

static void
run_pass(struct assembler *a)
{
    a->cur_section = SEC_TEXT;

    lex_next(&a->lex);

    while (a->lex.tok.type != T_EOF) {
        if (a->lex.tok.type == T_NEWLINE) {
            lex_next(&a->lex);
            continue;
        }

        if (a->lex.tok.type == T_DOT_IDENT) {
            const char *dir = a->lex.tok.str;

            if (dir[1] == 'L' || (dir[1] >= '0' && dir[1] <= '9')) {
                const char *label = dir;
                lex_next(&a->lex);
                if (a->lex.tok.type == T_COLON) {
                    lex_next(&a->lex);
                    if (a->pass == 1) {
                        int idx = sym_lookup(&a->st, label);
                        if (idx < 0)
                            idx = sym_add(&a->st, label);
                        sym_define(&a->st, idx, a->cur_section,
                                   a->sections[a->cur_section].len);
                    }
                    continue;
                }
                handle_directive(a, dir);
                skip_to_eol(&a->lex);
                if (a->lex.tok.type == T_NEWLINE)
                    lex_next(&a->lex);
                continue;
            }

            handle_directive(a, dir);
            skip_to_eol(&a->lex);
            if (a->lex.tok.type == T_NEWLINE)
                lex_next(&a->lex);
            continue;
        }

        if (a->lex.tok.type == T_IDENT) {
            const char *name = a->lex.tok.str;
            lex_next(&a->lex);

            if (a->lex.tok.type == T_COLON) {
                lex_next(&a->lex);
                if (a->pass == 1) {
                    int idx = sym_lookup(&a->st, name);
                    if (idx < 0)
                        idx = sym_add(&a->st, name);
                    sym_define(&a->st, idx, a->cur_section,
                               a->sections[a->cur_section].len);
                }
                continue;
            }

            parse_instruction(a, name);
            if (a->lex.tok.type == T_NEWLINE)
                lex_next(&a->lex);
            continue;
        }

        if (a->lex.tok.type == T_INT) {
            long n = a->lex.tok.ival;
            lex_next(&a->lex);
            if (a->lex.tok.type == T_COLON) {
                lex_next(&a->lex);
                if (a->pass == 1 && n >= 0 && n < MAX_LOCAL_LABELS) {
                    local_label_define((int)n, a->cur_section,
                                      a->sections[a->cur_section].len);
                }
                continue;
            }
            skip_to_eol(&a->lex);
            if (a->lex.tok.type == T_NEWLINE)
                lex_next(&a->lex);
            continue;
        }

        skip_to_eol(&a->lex);
        if (a->lex.tok.type == T_NEWLINE)
            lex_next(&a->lex);
    }
}

/****************************************************************
 * Public interface
 ****************************************************************/

void
as_init(struct assembler *a, const char *src)
{
    memset(a, 0, sizeof(*a));
    arena_init(&a->arena);
    a->lex.arena = &a->arena;
    a->st.arena = &a->arena;
    lex_init(&a->lex, src, '|', 0);
}

void
as_free(struct assembler *a)
{
    int k;

    for (k = 0; k < SEC_COUNT; k++) {
        free(a->sections[k].data);
        free(a->sections[k].relocs);
    }
    free(a->st.syms);
    free(a->lex.str_buf);
    arena_free(&a->arena);
}

void
as_pass1(struct assembler *a)
{
    local_label_reset();
    a->pass = 1;
    a->lex.pos = a->lex.src;
    a->lex.line = 1;
    a->lex.tok.type = T_NEWLINE;
    run_pass(a);
}

void
as_pass2(struct assembler *a)
{
    int k;

    for (k = 0; k < SEC_COUNT; k++)
        a->sections[k].len = 0;

    a->pass = 2;
    a->lex.pos = a->lex.src;
    a->lex.line = 1;
    a->lex.tok.type = T_NEWLINE;
    run_pass(a);
}
