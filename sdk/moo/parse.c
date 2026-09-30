/* parse.c - Recursive-descent parser for MooScript.
 *
 * Grammar:
 *   program    = { verb_def | func_def | const_decl }
 *   verb_def   = "verb" IDENT "(" [ params ] ")" [ "->" type ] stmts "endverb"
 *   func_def   = "func" IDENT "(" [ params ] ")" [ "->" type ] stmts "endfunc"
 *   const_decl = "const" IDENT ":" type "=" expr ";"
 *
 * Blocks use MOO-style end-keywords (endif, endfor, endwhile, endverb,
 * enddefer) rather than braces. Braces are used for list literals.
 *
 * Expressions use precedence climbing for binary operators.
 * Postfix operators: .name  .(expr)  :verb(args)  [idx]  [lo..hi]  (args)
 */

#include <stdlib.h>
#include <string.h>

#include "moo.h"

/***** helpers *****/

static struct arena *parse_arena;
static struct token cur;
static int no_colon;

static void
advance(void)
{
    cur = lex_next();
}

static int
accept(int k)
{
    if (cur.kind == k) {
        advance();
        return 1;
    }
    return 0;
}

static void
expect(int k, const char *what)
{
    if (cur.kind != k)
        die("parse:%d: expected %s, got %s", cur.line, what, tok_str(cur.kind));
    advance();
}

static struct node *
new_node(int kind, int line)
{
    struct node *n;

    n = arena_zalloc(parse_arena, sizeof(*n));
    n->kind = kind;
    n->line = line;
    return n;
}

/***** types *****/

static struct moo_type *
parse_type(void)
{
    struct moo_type *t;
    int kind;

    switch (cur.kind) {
    case T_TINT:  kind = T_TINT; break;
    case T_TSTR:  kind = T_TSTR; break;
    case T_TOBJ:  kind = T_TOBJ; break;
    case T_TBOOL: kind = T_TBOOL; break;
    case T_TERR:  kind = T_TERR; break;
    case T_TPROP:  kind = T_TPROP; break;
    case T_TFLOAT: kind = T_TFLOAT; break;
    case T_TLIST:
        advance();
        expect(T_LT, "'<'");
        t = arena_zalloc(parse_arena, sizeof(*t));
        t->kind = T_TLIST;
        t->inner = parse_type();
        expect(T_GT, "'>'");
        return t;
    case T_IDENT:
        t = arena_zalloc(parse_arena, sizeof(*t));
        t->kind = T_TIFACE;
        t->name = cur.sval;
        advance();
        return t;
    default:
        die("parse:%d: expected type, got %s", cur.line, tok_str(cur.kind));
        return NULL;
    }
    advance();
    t = arena_zalloc(parse_arena, sizeof(*t));
    t->kind = kind;
    return t;
}

/***** expressions *****/

static struct node *parse_expr(void);

static struct node *
parse_primary(void)
{
    struct node *n, *head, **tail;
    struct token t;

    t = cur;

    if (t.kind == T_NUMBER) {
        advance();
        n = new_node(N_NUM, t.line);
        n->ival = t.nval;
        return n;
    }
    if (t.kind == T_FLOAT_LIT) {
        advance();
        n = new_node(N_FLOAT, t.line);
        n->fval = t.fval;
        return n;
    }
    if (t.kind == T_STRING) {
        advance();
        n = new_node(N_STR, t.line);
        n->sval = t.sval;
        n->slen = t.slen;
        return n;
    }
    if (t.kind == T_OBJLIT) {
        advance();
        n = new_node(N_OBJREF, t.line);
        n->sval = t.sval;
        n->slen = t.slen;
        return n;
    }
    if (t.kind == T_ERRCODE) {
        advance();
        n = new_node(N_ERRVAL, t.line);
        n->sval = t.sval;
        return n;
    }
    if (t.kind == T_TRUE) {
        advance();
        n = new_node(N_BOOL, t.line);
        n->ival = 1;
        return n;
    }
    if (t.kind == T_FALSE) {
        advance();
        n = new_node(N_BOOL, t.line);
        n->ival = 0;
        return n;
    }
    if (t.kind == T_NIL) {
        advance();
        return new_node(N_NIL, t.line);
    }
    if (t.kind == T_RECOVER) {
        advance();
        expect(T_LPAREN, "'('");
        expect(T_RPAREN, "')'");
        return new_node(N_RECOVER, t.line);
    }
    if (t.kind == T_IDENT) {
        advance();
        n = new_node(N_NAME, t.line);
        n->name = t.sval;
        return n;
    }
    if (t.kind == T_LBRACE) {
        advance();
        n = new_node(N_LISTLIT, t.line);
        head = NULL;
        tail = &head;
        if (cur.kind != T_RBRACE) {
            for (;;) {
                struct node *e = parse_expr();
                *tail = e;
                tail = &e->next;
                if (!accept(T_COMMA))
                    break;
            }
        }
        expect(T_RBRACE, "'}'");
        n->a = head;
        return n;
    }
    if (t.kind == T_LPAREN) {
        advance();
        n = parse_expr();
        expect(T_RPAREN, "')'");
        return n;
    }

    die("parse:%d: unexpected %s in expression", t.line, tok_str(t.kind));
    return NULL;
}

static struct node *
parse_postfix(void)
{
    struct node *n, *idx, *args, **tail, *arg, *pn;
    int ln;
    char *vname;

    n = parse_primary();
    for (;;) {
        ln = cur.line;

        if (cur.kind == T_DOT) {
            advance();
            if (cur.kind == T_LPAREN) {
                /* computed property: obj.(expr) */
                advance();
                pn = new_node(N_CPROP, ln);
                pn->a = n;
                pn->b = parse_expr();
                expect(T_RPAREN, "')'");
                n = pn;
            } else if (cur.kind == T_IDENT) {
                /* static property: obj.name */
                pn = new_node(N_PROP, ln);
                pn->a = n;
                pn->name = cur.sval;
                advance();
                n = pn;
            } else {
                die("parse:%d: expected property name or '(' after '.'",
                    cur.line);
            }
            continue;
        }

        if (cur.kind == T_COLON && !no_colon) {
            /* verb call: obj:verb(args) */
            advance();
            if (cur.kind != T_IDENT)
                die("parse:%d: expected verb name after ':'", cur.line);
            vname = cur.sval;
            advance();
            expect(T_LPAREN, "'('");
            args = NULL;
            tail = &args;
            if (cur.kind != T_RPAREN) {
                for (;;) {
                    arg = parse_expr();
                    *tail = arg;
                    tail = &arg->next;
                    if (!accept(T_COMMA))
                        break;
                }
            }
            expect(T_RPAREN, "')'");
            pn = new_node(N_VCALL, ln);
            pn->a = n;
            pn->name = vname;
            pn->b = args;
            n = pn;
            continue;
        }

        if (cur.kind == T_LBRACK) {
            /* index or slice: obj[idx] or obj[lo..hi] */
            advance();
            idx = parse_expr();
            if (accept(T_DOTDOT)) {
                pn = new_node(N_SLICE, ln);
                pn->a = n;
                pn->b = idx;
                pn->c = parse_expr();
                expect(T_RBRACK, "']'");
                n = pn;
            } else {
                expect(T_RBRACK, "']'");
                pn = new_node(N_INDEX, ln);
                pn->a = n;
                pn->b = idx;
                n = pn;
            }
            continue;
        }

        if (cur.kind == T_AS) {
            /* narrowing cast: expr as InterfaceName */
            advance();
            if (cur.kind != T_IDENT)
                die("parse:%d: expected interface name after 'as'",
                    ln);
            pn = new_node(N_AS_EXPR, ln);
            pn->a = n;
            pn->name = cur.sval;
            advance();
            n = pn;
            continue;
        }

        if (cur.kind == T_LPAREN) {
            /* function call: func(args) */
            advance();
            args = NULL;
            tail = &args;
            if (cur.kind != T_RPAREN) {
                for (;;) {
                    arg = parse_expr();
                    *tail = arg;
                    tail = &arg->next;
                    if (!accept(T_COMMA))
                        break;
                }
            }
            expect(T_RPAREN, "')'");
            pn = new_node(N_CALL, ln);
            pn->a = n;
            pn->b = args;
            n = pn;
            continue;
        }

        break;
    }
    return n;
}

static struct node *
parse_unary(void)
{
    struct node *n, *u;
    int op, ln;

    if (cur.kind == T_BANG || cur.kind == T_MINUS) {
        op = cur.kind;
        ln = cur.line;
        advance();
        n = parse_unary();
        u = new_node(N_UNOP, ln);
        u->op = op;
        u->a = n;
        return u;
    }
    return parse_postfix();
}

static int
binop_prec(int k)
{
    switch (k) {
    case T_STAR: case T_SLASH: case T_PERCENT:  return 7;
    case T_PLUS: case T_MINUS:                  return 6;
    case T_IN: case T_IS:                        return 5;
    case T_LT: case T_LE: case T_GT: case T_GE: return 4;
    case T_EQ: case T_NE:                       return 3;
    case T_ANDAND:                              return 2;
    case T_OROR:                                return 1;
    default:                                    return -1;
    }
}

static struct node *
parse_binop(int min_prec)
{
    struct node *lhs, *rhs, *b;
    int prec, op, ln;

    lhs = parse_unary();
    for (;;) {
        prec = binop_prec(cur.kind);
        if (prec < min_prec)
            return lhs;
        op = cur.kind;
        ln = cur.line;
        advance();
        if (op == T_IS) {
            if (cur.kind != T_IDENT)
                die("parse:%d: expected interface name after 'is'",
                    ln);
            b = new_node(N_IS_EXPR, ln);
            b->a = lhs;
            b->name = cur.sval;
            advance();
            lhs = b;
            continue;
        }
        rhs = parse_binop(prec + 1);
        b = new_node(N_BINOP, ln);
        b->op = op;
        b->a = lhs;
        b->b = rhs;
        lhs = b;
    }
}

static struct node *
parse_expr(void)
{
    return parse_binop(1);
}

/***** statements *****/

static struct node *parse_stmts(int t1, int t2, int t3);

static struct node *
parse_var_decl(void)
{
    struct node *n;
    int ln;

    ln = cur.line;
    advance(); /* eat T_VAR */
    if (cur.kind != T_IDENT)
        die("parse:%d: expected variable name", cur.line);
    n = new_node(N_VAR_DECL, ln);
    n->name = cur.sval;
    advance();
    expect(T_COLON, "':'");
    n->type = parse_type();
    if (accept(T_ASSIGN))
        n->a = parse_expr();
    expect(T_SEMI, "';'");
    return n;
}

static struct node *
parse_if_chain(void)
{
    struct node *n;
    int ln;

    ln = cur.line;
    expect(T_LPAREN, "'('");
    n = new_node(N_IF, ln);
    n->a = parse_expr();
    expect(T_RPAREN, "')'");
    n->b = parse_stmts(T_ELSEIF, T_ELSE, T_ENDIF);
    if (cur.kind == T_ELSEIF) {
        advance();
        n->c = parse_if_chain();
    } else if (cur.kind == T_ELSE) {
        advance();
        n->c = parse_stmts(-1, -1, T_ENDIF);
        expect(T_ENDIF, "'endif'");
    } else {
        expect(T_ENDIF, "'endif'");
    }
    return n;
}

static struct node *
parse_for(void)
{
    struct node *n;
    int ln;

    ln = cur.line;
    if (cur.kind != T_IDENT)
        die("parse:%d: expected variable name after 'for'", cur.line);
    n = new_node(N_FOR, ln);
    n->name = cur.sval;
    advance();
    expect(T_IN, "'in'");
    n->a = parse_expr();
    if (accept(T_DOTDOT))
        n->b = parse_expr();
    n->c = parse_stmts(-1, -1, T_ENDFOR);
    expect(T_ENDFOR, "'endfor'");
    return n;
}

static struct node *
parse_type_switch(void)
{
    struct node *n, *c, **ctail;
    int ln;

    ln = cur.line;
    expect(T_LPAREN, "'('");
    n = new_node(N_TYPESWITCH, ln);
    n->a = parse_expr();
    expect(T_RPAREN, "')'");

    ctail = &n->b;
    while (cur.kind != T_ENDSWITCH && cur.kind != T_EOF) {
        if (cur.kind == T_CASE) {
            advance();
            if (cur.kind != T_IDENT)
                die("parse:%d: expected interface name "
                    "after 'case'", cur.line);
            c = new_node(N_CASE, cur.line);
            c->name = cur.sval;
            advance();
            expect(T_COLON, "':'");
            c->b = parse_stmts(T_CASE, T_ELSE, T_ENDSWITCH);
            *ctail = c;
            ctail = &c->next;
        } else if (cur.kind == T_ELSE) {
            advance();
            expect(T_COLON, "':'");
            c = new_node(N_CASE, cur.line);
            c->ival = 1;
            c->b = parse_stmts(-1, -1, T_ENDSWITCH);
            *ctail = c;
            ctail = &c->next;
            break;
        } else {
            die("parse:%d: expected 'case', 'else', or "
                "'endswitch'", cur.line);
        }
    }
    expect(T_ENDSWITCH, "'endswitch'");
    return n;
}

static struct node *
parse_switch(void)
{
    struct node *n, *c, **ctail, *v, **vtail;
    int ln;

    ln = cur.line;
    if (cur.kind == T_IDENT && strcmp(cur.sval, "type") == 0) {
        advance();
        return parse_type_switch();
    }
    expect(T_LPAREN, "'('");
    n = new_node(N_SWITCH, ln);
    n->a = parse_expr();
    expect(T_RPAREN, "')'");

    ctail = &n->b;
    while (cur.kind != T_ENDSWITCH && cur.kind != T_EOF) {
        if (cur.kind == T_CASE) {
            advance();
            c = new_node(N_CASE, cur.line);
            vtail = &c->a;
            no_colon = 1;
            for (;;) {
                v = new_node(N_CASE_VAL, cur.line);
                v->a = parse_expr();
                if (accept(T_DOTDOT))
                    v->b = parse_expr();
                *vtail = v;
                vtail = &v->next;
                if (!accept(T_COMMA))
                    break;
            }
            no_colon = 0;
            expect(T_COLON, "':'");
            c->b = parse_stmts(T_CASE, T_ELSE, T_ENDSWITCH);
            *ctail = c;
            ctail = &c->next;
        } else if (cur.kind == T_ELSE) {
            advance();
            expect(T_COLON, "':'");
            c = new_node(N_CASE, cur.line);
            c->ival = 1;
            c->b = parse_stmts(-1, -1, T_ENDSWITCH);
            *ctail = c;
            ctail = &c->next;
            break;
        } else {
            die("parse:%d: expected 'case', 'else', or "
                "'endswitch'", cur.line);
        }
    }
    expect(T_ENDSWITCH, "'endswitch'");
    return n;
}

static struct node *
parse_stmt(void)
{
    struct node *n, *e;
    int ln, op;

    ln = cur.line;

    if (cur.kind == T_VAR)
        return parse_var_decl();

    if (cur.kind == T_IF) {
        advance();
        return parse_if_chain();
    }
    if (cur.kind == T_FOR) {
        advance();
        return parse_for();
    }
    if (cur.kind == T_WHILE) {
        advance();
        expect(T_LPAREN, "'('");
        n = new_node(N_WHILE, ln);
        n->a = parse_expr();
        expect(T_RPAREN, "')'");
        n->b = parse_stmts(-1, -1, T_ENDWHILE);
        expect(T_ENDWHILE, "'endwhile'");
        return n;
    }
    if (cur.kind == T_RETURN) {
        advance();
        if (accept(T_DOTDOT)) {
            n = new_node(N_RETURN_PUSH, ln);
            n->a = parse_expr();
            expect(T_SEMI, "';'");
            return n;
        }
        n = new_node(N_RETURN, ln);
        if (cur.kind != T_SEMI)
            n->a = parse_expr();
        expect(T_SEMI, "';'");
        return n;
    }
    if (cur.kind == T_DEFER) {
        advance();
        n = new_node(N_DEFER, ln);
        n->a = parse_stmts(-1, -1, T_ENDDEFER);
        expect(T_ENDDEFER, "'enddefer'");
        return n;
    }
    if (cur.kind == T_PANIC) {
        advance();
        expect(T_LPAREN, "'('");
        n = new_node(N_PANIC_STMT, ln);
        n->a = parse_expr();
        expect(T_RPAREN, "')'");
        expect(T_SEMI, "';'");
        return n;
    }
    if (cur.kind == T_TRACE) {
        advance();
        n = new_node(N_TRACE_STMT, ln);
        n->a = parse_expr();
        expect(T_SEMI, "';'");
        return n;
    }
    if (cur.kind == T_TRACE_COMMENT) {
        n = new_node(N_TRACE_CMT, ln);
        n->sval = cur.sval;
        n->slen = cur.slen;
        advance();
        return n;
    }
    if (cur.kind == T_SWITCH) {
        advance();
        return parse_switch();
    }
    if (cur.kind == T_BREAK) {
        advance();
        expect(T_SEMI, "';'");
        return new_node(N_BREAK, ln);
    }
    if (cur.kind == T_CONTINUE) {
        advance();
        expect(T_SEMI, "';'");
        return new_node(N_CONTINUE, ln);
    }

    /* expression or assignment */
    e = parse_expr();
    if (cur.kind == T_ASSIGN || cur.kind == T_PLUSEQ ||
        cur.kind == T_MINUSEQ) {
        op = cur.kind;
        advance();
        n = new_node(N_ASSIGN, ln);
        n->op = op;
        n->a = e;
        n->b = parse_expr();
        expect(T_SEMI, "';'");
        return n;
    }
    expect(T_SEMI, "';'");
    n = new_node(N_EXPR_STMT, ln);
    n->a = e;
    return n;
}

static struct node *
parse_stmts(int t1, int t2, int t3)
{
    struct node *n, *head, **tail, *s;
    int ln;

    ln = cur.line;
    head = NULL;
    tail = &head;
    while (cur.kind != T_EOF &&
           cur.kind != t1 && cur.kind != t2 && cur.kind != t3) {
        s = parse_stmt();
        *tail = s;
        tail = &s->next;
    }
    n = new_node(N_BLOCK, ln);
    n->a = head;
    return n;
}

/***** top level *****/

static struct node *
parse_callable(void)
{
    struct node *fn, *params, **ptail, *p;
    int ln, kind, end_tok;
    const char *what;

    ln = cur.line;
    if (cur.kind == T_VERB) {
        kind = N_VERB;
        end_tok = T_ENDVERB;
        what = "'endverb'";
    } else {
        kind = N_FUNC;
        end_tok = T_ENDFUNC;
        what = "'endfunc'";
    }
    advance();
    if (cur.kind != T_IDENT)
        die("parse:%d: expected function name", cur.line);
    fn = new_node(kind, ln);
    fn->name = cur.sval;
    advance();

    expect(T_LPAREN, "'('");
    params = NULL;
    ptail = &params;
    if (cur.kind != T_RPAREN) {
        for (;;) {
            if (cur.kind != T_IDENT)
                die("parse:%d: expected parameter name", cur.line);
            p = new_node(N_PARAM, cur.line);
            p->name = cur.sval;
            advance();
            expect(T_COLON, "':'");
            p->type = parse_type();
            *ptail = p;
            ptail = &p->next;
            if (!accept(T_COMMA))
                break;
        }
    }
    expect(T_RPAREN, "')'");

    if (accept(T_ARROW))
        fn->type = parse_type();

    fn->a = params;
    fn->b = parse_stmts(-1, -1, end_tok);
    expect(end_tok, what);
    return fn;
}

static struct node *
parse_const(void)
{
    struct node *n;
    int ln;

    ln = cur.line;
    advance(); /* eat T_CONST */
    if (cur.kind != T_IDENT)
        die("parse:%d: expected constant name", cur.line);
    n = new_node(N_CONST_DECL, ln);
    n->name = cur.sval;
    advance();
    expect(T_COLON, "':'");
    n->type = parse_type();
    expect(T_ASSIGN, "'='");
    n->a = parse_expr();
    expect(T_SEMI, "';'");
    return n;
}

static struct node *
parse_interface(void)
{
    struct node *n, *m, **tail;
    int ln;

    ln = cur.line;
    advance(); /* eat T_INTERFACE */
    if (cur.kind != T_IDENT)
        die("parse:%d: expected interface name", cur.line);
    n = new_node(N_INTERFACE, ln);
    n->name = cur.sval;
    advance();

    tail = &n->a;
    while (cur.kind != T_ENDINTERFACE && cur.kind != T_EOF) {
        if (cur.kind != T_IDENT)
            die("parse:%d: expected member name in interface",
                cur.line);
        ln = cur.line;
        m = NULL;
        if (lex_peek().kind == T_COLON) {
            /* property: name: type; */
            m = new_node(N_IFACE_PROP, ln);
            m->name = cur.sval;
            advance();
            expect(T_COLON, "':'");
            m->type = parse_type();
            expect(T_SEMI, "';'");
        } else if (lex_peek().kind == T_LPAREN) {
            /* verb: name(params); */
            m = new_node(N_IFACE_VERB, ln);
            m->name = cur.sval;
            advance();
            expect(T_LPAREN, "'('");
            if (cur.kind != T_RPAREN) {
                struct node **ptail = &m->a;
                for (;;) {
                    struct node *p;
                    if (cur.kind != T_IDENT)
                        die("parse:%d: expected parameter name",
                            cur.line);
                    p = new_node(N_PARAM, cur.line);
                    p->name = cur.sval;
                    advance();
                    expect(T_COLON, "':'");
                    p->type = parse_type();
                    *ptail = p;
                    ptail = &p->next;
                    if (!accept(T_COMMA))
                        break;
                }
            }
            expect(T_RPAREN, "')'");
            expect(T_SEMI, "';'");
        } else {
            die("parse:%d: expected ':' or '(' after member name",
                cur.line);
        }
        *tail = m;
        tail = &m->next;
    }
    expect(T_ENDINTERFACE, "'endinterface'");
    return n;
}

static struct node *
parse_extern_decl(void)
{
    struct node *n, *params, **ptail, *p;
    int ln, kind;
    char *link_name;

    ln = cur.line;
    advance(); /* eat T_EXTERN */

    link_name = NULL;
    if (cur.kind == T_IDENT && lex_peek().kind == T_AS) {
        link_name = cur.sval;
        advance(); /* eat link name */
        advance(); /* eat 'as' */
    }

    if (cur.kind == T_VERB)
        kind = N_EXTERN_VERB;
    else if (cur.kind == T_FUNC)
        kind = N_EXTERN_FUNC;
    else
        die("parse:%d: expected 'verb' or 'func' after 'extern'",
            cur.line);
    advance();

    if (cur.kind != T_IDENT)
        die("parse:%d: expected function name", cur.line);
    n = new_node(kind, ln);
    n->name = cur.sval;
    n->link_name = link_name;
    advance();

    expect(T_LPAREN, "'('");
    params = NULL;
    ptail = &params;
    if (cur.kind != T_RPAREN) {
        for (;;) {
            if (cur.kind != T_IDENT)
                die("parse:%d: expected parameter name", cur.line);
            p = new_node(N_PARAM, cur.line);
            p->name = cur.sval;
            advance();
            expect(T_COLON, "':'");
            p->type = parse_type();
            *ptail = p;
            ptail = &p->next;
            if (!accept(T_COMMA))
                break;
        }
    }
    expect(T_RPAREN, "')'");

    if (kind == N_EXTERN_FUNC) {
        expect(T_ARROW, "'->'");
        n->type = parse_type();
    }

    n->a = params;
    expect(T_SEMI, "';'");
    return n;
}

static struct node *
parse_top(void)
{
    struct node *n;
    int exported;

    if (cur.kind == T_EXPORT) {
        exported = 1;
        advance();
        if (cur.kind == T_VERB || cur.kind == T_FUNC) {
            n = parse_callable();
            n->exported = exported;
            return n;
        }
        if (cur.kind == T_CONST) {
            n = parse_const();
            n->exported = exported;
            return n;
        }
        die("parse:%d: expected 'verb', 'func', or 'const' "
            "after 'export'", cur.line);
    }
    if (cur.kind == T_VERB || cur.kind == T_FUNC)
        return parse_callable();
    if (cur.kind == T_CONST)
        return parse_const();
    if (cur.kind == T_INTERFACE)
        return parse_interface();
    if (cur.kind == T_EXTERN)
        return parse_extern_decl();
    die("parse:%d: expected top-level declaration, got %s",
        cur.line, tok_str(cur.kind));
    return NULL;
}

/***** entry point *****/

struct node *
parse_program(struct arena *a)
{
    struct node *prog, *head, **tail, *d;

    parse_arena = a;
    advance();
    head = NULL;
    tail = &head;

    if (cur.kind == T_MODULE) {
        int ln = cur.line;
        advance();
        if (cur.kind != T_IDENT)
            die("parse:%d: expected module name", cur.line);
        d = new_node(N_MODULE, ln);
        d->name = cur.sval;
        advance();
        expect(T_SEMI, "';'");
        *tail = d;
        tail = &d->next;
    }

    while (cur.kind == T_IMPORT) {
        int ln = cur.line;
        advance();
        for (;;) {
            if (cur.kind != T_IDENT)
                die("parse:%d: expected module name after 'import'",
                    cur.line);
            d = new_node(N_IMPORT, ln);
            d->name = cur.sval;
            advance();
            *tail = d;
            tail = &d->next;
            if (!accept(T_COMMA))
                break;
        }
        expect(T_SEMI, "';'");
    }

    while (cur.kind != T_EOF) {
        d = parse_top();
        *tail = d;
        tail = &d->next;
    }
    prog = new_node(N_PROGRAM, 1);
    prog->a = head;
    return prog;
}
