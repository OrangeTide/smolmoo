/* typecheck.c : type checking pass for MooScript */

#include "moo.h"

#include <stdlib.h>
#include <string.h>

/****************************************************************
 * Static type singletons
 ****************************************************************/

#define T_TNIL (-1)

static struct moo_type t_int  = { T_TINT,  NULL, NULL };
static struct moo_type t_str  = { T_TSTR,  NULL, NULL };
static struct moo_type t_obj  = { T_TOBJ,  NULL, NULL };
static struct moo_type t_bool = { T_TBOOL, NULL, NULL };
static struct moo_type t_err  = { T_TERR,  NULL, NULL };
static struct moo_type t_float = { T_TFLOAT, NULL, NULL };
static struct moo_type t_prop = { T_TPROP, NULL, NULL };
static struct moo_type t_nil  = { T_TNIL,  NULL, NULL };

/****************************************************************
 * Symbol table
 ****************************************************************/

static struct arena *tc_arena;

struct tc_sym {
    char *name;
    struct moo_type *type;
};

static struct tc_sym *syms;
static int nsyms;
static int sym_cap;

static void
tc_add(const char *name, struct moo_type *type)
{
    if (nsyms == sym_cap) {
        sym_cap = sym_cap ? sym_cap * 2 : 16;
        syms = realloc(syms, sym_cap * sizeof(*syms));
        if (!syms)
            die("oom");
    }
    syms[nsyms].name = arena_strdup(tc_arena,name);
    syms[nsyms].type = type;
    nsyms++;
}

static struct moo_type *
tc_find(const char *name)
{
    for (int i = nsyms - 1; i >= 0; i--)
        if (strcmp(syms[i].name, name) == 0)
            return syms[i].type;
    return NULL;
}

static void
tc_pop_scope(int mark)
{
    nsyms = mark;
}

/****************************************************************
 * Verb table (for checking calls to user-defined verbs)
 ****************************************************************/

struct tc_verb {
    char *name;
    struct node *params;
    struct moo_type *ret_type;
};

static struct tc_verb *tc_verbs;
static int ntc_verbs;
static int tc_verb_cap;

static void
tc_add_verb(const char *name, struct node *params, struct moo_type *ret_type)
{
    if (ntc_verbs == tc_verb_cap) {
        tc_verb_cap = tc_verb_cap ? tc_verb_cap * 2 : 8;
        tc_verbs = realloc(tc_verbs,
                            tc_verb_cap * sizeof(*tc_verbs));
        if (!tc_verbs)
            die("oom");
    }
    tc_verbs[ntc_verbs].name = arena_strdup(tc_arena,name);
    tc_verbs[ntc_verbs].params = params;
    tc_verbs[ntc_verbs].ret_type = ret_type;
    ntc_verbs++;
}

static struct tc_verb *
tc_find_verb(const char *name)
{
    for (int i = 0; i < ntc_verbs; i++)
        if (strcmp(tc_verbs[i].name, name) == 0)
            return &tc_verbs[i];
    return NULL;
}

/****************************************************************
 * Interface table
 ****************************************************************/

static struct node **ifaces;
static int nifaces;
static int iface_cap;

static void
tc_add_iface(struct node *iface)
{
    if (nifaces == iface_cap) {
        iface_cap = iface_cap ? iface_cap * 2 : 8;
        ifaces = realloc(ifaces, iface_cap * sizeof(*ifaces));
        if (!ifaces)
            die("oom");
    }
    ifaces[nifaces++] = iface;
}

static struct node *
tc_find_iface(const char *name)
{
    for (int i = 0; i < nifaces; i++)
        if (strcmp(ifaces[i]->name, name) == 0)
            return ifaces[i];
    return NULL;
}

/****************************************************************
 * Type utilities
 ****************************************************************/

static const char *
type_name(struct moo_type *t)
{
    if (!t)
        return "unknown";
    switch (t->kind) {
    case T_TINT:  return "int";
    case T_TSTR:  return "str";
    case T_TOBJ:  return "obj";
    case T_TBOOL: return "bool";
    case T_TERR:  return "err";
    case T_TLIST:  return "list";
    case T_TFLOAT: return "float";
    case T_TPROP:  return "prop";
    case T_TIFACE: return t->name ? t->name : "interface";
    case T_TNIL:   return "nil";
    default:       return "??";
    }
}

static int
is_numeric(struct moo_type *t)
{
    return t && (t->kind == T_TINT || t->kind == T_TFLOAT);
}

static int
is_pointer_type(struct moo_type *t)
{
    if (!t)
        return 0;
    return t->kind == T_TSTR || t->kind == T_TOBJ ||
           t->kind == T_TLIST || t->kind == T_TERR ||
           t->kind == T_TPROP || t->kind == T_TIFACE;
}

static int
types_compatible(struct moo_type *a, struct moo_type *b)
{
    if (!a || !b)
        return 1;
    if (a->kind == T_TNIL)
        return is_pointer_type(b);
    if (b->kind == T_TNIL)
        return is_pointer_type(a);
    if (a->kind == T_TPROP || b->kind == T_TPROP)
        return 1;
    if (a->kind == b->kind) {
        if (a->kind == T_TLIST && a->inner && b->inner)
            return types_compatible(a->inner, b->inner);
        if (a->kind == T_TIFACE)
            return a->name && b->name &&
                   strcmp(a->name, b->name) == 0;
        return 1;
    }
    /* interface <-> obj are compatible */
    if ((a->kind == T_TIFACE && b->kind == T_TOBJ) ||
        (a->kind == T_TOBJ && b->kind == T_TIFACE))
        return 1;
    return 0;
}

/****************************************************************
 * Expression type inference
 ****************************************************************/

static void tc_stmt(struct node *n);
static struct moo_type *tc_call(struct node *n);
static struct moo_type *tc_vcall(struct node *n);

static struct moo_type *
tc_expr(struct node *n)
{
    struct moo_type *lt, *rt;

    if (!n)
        return NULL;

    switch (n->kind) {
    case N_NUM:
        return &t_int;

    case N_FLOAT:
        return &t_float;

    case N_BOOL:
        return &t_bool;

    case N_NIL:
        return &t_nil;

    case N_ERRVAL:
        return &t_err;

    case N_STR:
        return &t_str;

    case N_OBJREF:
        return &t_obj;

    case N_RECOVER:
        return &t_err;

    case N_NAME: {
        struct moo_type *t = tc_find(n->name);
        if (!t)
            die("typecheck:%d: undefined '%s'", n->line, n->name);
        return t;
    }

    case N_LISTLIT: {
        struct moo_type *elem = NULL;

        for (struct node *e = n->a; e; e = e->next) {
            struct moo_type *et = tc_expr(e);
            if (!et)
                continue;
            if (!elem) {
                elem = et;
            } else if (!types_compatible(elem, et)) {
                die("typecheck:%d: list elements have "
                    "mixed types: %s and %s",
                    n->line, type_name(elem), type_name(et));
            }
        }
        struct moo_type *t = arena_zalloc(tc_arena, sizeof(*t));
        t->kind = T_TLIST;
        t->inner = elem;
        return t;
    }

    case N_UNOP:
        lt = tc_expr(n->a);
        switch (n->op) {
        case T_MINUS:
            if (lt && !is_numeric(lt))
                die("typecheck:%d: unary '-' requires "
                    "numeric, got %s", n->line, type_name(lt));
            if (lt && lt->kind == T_TFLOAT)
                return &t_float;
            return &t_int;
        case T_BANG:
            return &t_bool;
        default:
            return NULL;
        }

    case N_BINOP:
        lt = tc_expr(n->a);
        rt = tc_expr(n->b);

        switch (n->op) {
        case T_PLUS:
            if (lt && (lt->kind == T_TSTR || lt->kind == T_TPROP)) {
                if (rt && rt->kind != T_TSTR &&
                    rt->kind != T_TPROP)
                    die("typecheck:%d: cannot add %s "
                        "to str", n->line, type_name(rt));
                return &t_str;
            }
            if (rt && (rt->kind == T_TSTR || rt->kind == T_TPROP)) {
                if (lt && lt->kind != T_TSTR &&
                    lt->kind != T_TPROP)
                    die("typecheck:%d: cannot add str "
                        "to %s", n->line, type_name(lt));
                return &t_str;
            }
            if (lt && !is_numeric(lt))
                die("typecheck:%d: '+' requires numeric or "
                    "str, got %s", n->line, type_name(lt));
            if (rt && !is_numeric(rt))
                die("typecheck:%d: '+' requires numeric or "
                    "str, got %s", n->line, type_name(rt));
            if ((lt && lt->kind == T_TFLOAT) ||
                (rt && rt->kind == T_TFLOAT))
                return &t_float;
            return &t_int;

        case T_MINUS:
        case T_STAR:
        case T_SLASH:
            if (lt && !is_numeric(lt))
                die("typecheck:%d: '%s' requires numeric, "
                    "got %s", n->line, tok_str(n->op),
                    type_name(lt));
            if (rt && !is_numeric(rt))
                die("typecheck:%d: '%s' requires numeric, "
                    "got %s", n->line, tok_str(n->op),
                    type_name(rt));
            if ((lt && lt->kind == T_TFLOAT) ||
                (rt && rt->kind == T_TFLOAT))
                return &t_float;
            return &t_int;

        case T_PERCENT:
            if (lt && !is_numeric(lt))
                die("typecheck:%d: '%%' requires numeric, "
                    "got %s", n->line, type_name(lt));
            if (rt && !is_numeric(rt))
                die("typecheck:%d: '%%' requires numeric, "
                    "got %s", n->line, type_name(rt));
            if ((lt && lt->kind == T_TFLOAT) ||
                (rt && rt->kind == T_TFLOAT))
                return &t_float;
            return &t_int;

        case T_EQ:
        case T_NE:
            if (lt && lt->kind != T_TPROP &&
                rt && rt->kind != T_TPROP &&
                !types_compatible(lt, rt))
                die("typecheck:%d: cannot compare "
                    "%s with %s", n->line,
                    type_name(lt), type_name(rt));
            return &t_bool;

        case T_LT:
        case T_LE:
        case T_GT:
        case T_GE:
            if (lt && !is_numeric(lt))
                die("typecheck:%d: '%s' requires numeric, "
                    "got %s", n->line, tok_str(n->op),
                    type_name(lt));
            if (rt && !is_numeric(rt))
                die("typecheck:%d: '%s' requires numeric, "
                    "got %s", n->line, tok_str(n->op),
                    type_name(rt));
            return &t_bool;

        case T_ANDAND:
        case T_OROR:
            return &t_bool;

        case T_IN:
            if (rt && rt->kind != T_TLIST)
                die("typecheck:%d: 'in' requires list on "
                    "right side, got %s",
                    n->line, type_name(rt));
            return &t_bool;

        default:
            return NULL;
        }

    case N_INDEX:
        lt = tc_expr(n->a);
        rt = tc_expr(n->b);
        if (rt && !is_numeric(rt))
            die("typecheck:%d: index must be int, got %s",
                n->line, type_name(rt));
        if (lt && lt->kind == T_TSTR)
            return &t_str;
        if (lt && lt->kind == T_TLIST)
            return lt->inner ? lt->inner : NULL;
        if (lt)
            die("typecheck:%d: cannot index into %s",
                n->line, type_name(lt));
        return NULL;

    case N_SLICE:
        lt = tc_expr(n->a);
        rt = tc_expr(n->b);
        if (n->c)
            (void)tc_expr(n->c);
        if (rt && !is_numeric(rt))
            die("typecheck:%d: slice bounds must be int",
                n->line);
        if (lt && lt->kind == T_TSTR)
            return &t_str;
        if (lt && lt->kind == T_TLIST)
            return lt;
        if (lt)
            die("typecheck:%d: cannot slice %s",
                n->line, type_name(lt));
        return NULL;

    case N_PROP: {
        struct moo_type *ot = tc_expr(n->a);
        if (ot && ot->kind == T_TIFACE && ot->name) {
            struct node *idef = tc_find_iface(ot->name);
            if (idef) {
                for (struct node *m = idef->a; m;
                     m = m->next) {
                    if (m->kind == N_IFACE_PROP &&
                        strcmp(m->name, n->name) == 0)
                        return m->type;
                }
                die("typecheck:%d: interface '%s' has no "
                    "property '%s'", n->line, ot->name,
                    n->name);
            }
        }
        return &t_prop;
    }

    case N_CPROP:
        (void)tc_expr(n->a);
        (void)tc_expr(n->b);
        return &t_prop;

    case N_IS_EXPR: {
        struct moo_type *ot = tc_expr(n->a);
        if (ot && ot->kind != T_TOBJ && ot->kind != T_TIFACE)
            die("typecheck:%d: 'is' requires obj, got %s",
                n->line, type_name(ot));
        if (!tc_find_iface(n->name))
            die("typecheck:%d: unknown interface '%s'",
                n->line, n->name);
        return &t_bool;
    }

    case N_AS_EXPR: {
        struct moo_type *ot = tc_expr(n->a);
        if (ot && ot->kind != T_TOBJ && ot->kind != T_TIFACE)
            die("typecheck:%d: 'as' requires obj, got %s",
                n->line, type_name(ot));
        struct node *idef = tc_find_iface(n->name);
        if (!idef)
            die("typecheck:%d: unknown interface '%s'",
                n->line, n->name);
        struct moo_type *t = arena_zalloc(tc_arena, sizeof(*t));
        t->kind = T_TIFACE;
        t->name = n->name;
        return t;
    }

    case N_VCALL:
        return tc_vcall(n);

    case N_CALL:
        return tc_call(n);

    default:
        return NULL;
    }
}

/****************************************************************
 * Builtin function type checking
 ****************************************************************/

static struct moo_type *
tc_call(struct node *n)
{
    struct moo_type *argtypes[16];
    int nargs = 0;

    for (struct node *a = n->b; a; a = a->next) {
        if (nargs >= 16)
            die("typecheck:%d: too many arguments", n->line);
        argtypes[nargs++] = tc_expr(a);
    }

    if (n->a->kind != N_NAME)
        return NULL;

    const char *name = n->a->name;

    if (strcmp(name, "length") == 0) {
        if (nargs != 1)
            die("typecheck:%d: length() takes 1 argument, "
                "got %d", n->line, nargs);
        if (argtypes[0] &&
            argtypes[0]->kind != T_TSTR &&
            argtypes[0]->kind != T_TLIST)
            die("typecheck:%d: length() requires str or list, "
                "got %s", n->line, type_name(argtypes[0]));
        return &t_int;
    }
    if (strcmp(name, "tostr") == 0) {
        if (nargs != 1)
            die("typecheck:%d: tostr() takes 1 argument, "
                "got %d", n->line, nargs);
        return &t_str;
    }
    if (strcmp(name, "toint") == 0) {
        if (nargs != 1)
            die("typecheck:%d: toint() takes 1 argument, "
                "got %d", n->line, nargs);
        if (argtypes[0] && argtypes[0]->kind != T_TSTR &&
            argtypes[0]->kind != T_TFLOAT)
            die("typecheck:%d: toint() requires str or float, "
                "got %s", n->line, type_name(argtypes[0]));
        return &t_int;
    }
    if (strcmp(name, "tofloat") == 0) {
        if (nargs != 1)
            die("typecheck:%d: tofloat() takes 1 argument, "
                "got %d", n->line, nargs);
        if (argtypes[0] && argtypes[0]->kind != T_TINT &&
            argtypes[0]->kind != T_TSTR)
            die("typecheck:%d: tofloat() requires int or str, "
                "got %s", n->line, type_name(argtypes[0]));
        return &t_float;
    }
    if (strcmp(name, "index") == 0) {
        if (nargs != 2)
            die("typecheck:%d: index() takes 2 arguments, "
                "got %d", n->line, nargs);
        if (argtypes[0] && argtypes[0]->kind != T_TSTR)
            die("typecheck:%d: index() first argument must be "
                "str, got %s", n->line, type_name(argtypes[0]));
        if (argtypes[1] && argtypes[1]->kind != T_TSTR)
            die("typecheck:%d: index() second argument must be "
                "str, got %s", n->line, type_name(argtypes[1]));
        return &t_int;
    }
    if (strcmp(name, "substr") == 0) {
        if (nargs != 3)
            die("typecheck:%d: substr() takes 3 arguments, "
                "got %d", n->line, nargs);
        if (argtypes[0] && argtypes[0]->kind != T_TSTR)
            die("typecheck:%d: substr() first argument must be "
                "str, got %s", n->line, type_name(argtypes[0]));
        if (argtypes[1] && !is_numeric(argtypes[1]))
            die("typecheck:%d: substr() start must be int, "
                "got %s", n->line, type_name(argtypes[1]));
        if (argtypes[2] && !is_numeric(argtypes[2]))
            die("typecheck:%d: substr() end must be int, "
                "got %s", n->line, type_name(argtypes[2]));
        return &t_str;
    }
    if (strcmp(name, "strsub") == 0) {
        if (nargs != 3)
            die("typecheck:%d: strsub() takes 3 arguments, "
                "got %d", n->line, nargs);
        if (argtypes[0] && argtypes[0]->kind != T_TSTR)
            die("typecheck:%d: strsub() first argument must be "
                "str, got %s", n->line, type_name(argtypes[0]));
        if (argtypes[1] && argtypes[1]->kind != T_TSTR)
            die("typecheck:%d: strsub() second argument must be "
                "str, got %s", n->line, type_name(argtypes[1]));
        if (argtypes[2] && argtypes[2]->kind != T_TSTR)
            die("typecheck:%d: strsub() third argument must be "
                "str, got %s", n->line, type_name(argtypes[2]));
        return &t_str;
    }
    if (strcmp(name, "listappend") == 0) {
        if (nargs != 2)
            die("typecheck:%d: listappend() takes 2 arguments, "
                "got %d", n->line, nargs);
        if (argtypes[0] && argtypes[0]->kind != T_TLIST)
            die("typecheck:%d: listappend() first argument "
                "must be list, got %s",
                n->line, type_name(argtypes[0]));
        return argtypes[0] ? argtypes[0] : NULL;
    }
    if (strcmp(name, "listdelete") == 0) {
        if (nargs != 2)
            die("typecheck:%d: listdelete() takes 2 arguments, "
                "got %d", n->line, nargs);
        if (argtypes[0] && argtypes[0]->kind != T_TLIST)
            die("typecheck:%d: listdelete() first argument "
                "must be list, got %s",
                n->line, type_name(argtypes[0]));
        if (argtypes[1] && !is_numeric(argtypes[1]))
            die("typecheck:%d: listdelete() index must be int, "
                "got %s", n->line, type_name(argtypes[1]));
        return argtypes[0] ? argtypes[0] : NULL;
    }
    if (strcmp(name, "listset") == 0) {
        if (nargs != 3)
            die("typecheck:%d: listset() takes 3 arguments, "
                "got %d", n->line, nargs);
        if (argtypes[0] && argtypes[0]->kind != T_TLIST)
            die("typecheck:%d: listset() first argument "
                "must be list, got %s",
                n->line, type_name(argtypes[0]));
        if (argtypes[1] && !is_numeric(argtypes[1]))
            die("typecheck:%d: listset() index must be int, "
                "got %s", n->line, type_name(argtypes[1]));
        return argtypes[0] ? argtypes[0] : NULL;
    }
    if (strcmp(name, "valid") == 0) {
        if (nargs != 1)
            die("typecheck:%d: valid() takes 1 argument, "
                "got %d", n->line, nargs);
        if (argtypes[0] && argtypes[0]->kind != T_TOBJ)
            die("typecheck:%d: valid() requires obj, got %s",
                n->line, type_name(argtypes[0]));
        return &t_bool;
    }
    if (strcmp(name, "move") == 0) {
        if (nargs != 2)
            die("typecheck:%d: move() takes 2 arguments, "
                "got %d", n->line, nargs);
        return NULL;
    }
    if (strcmp(name, "create") == 0) {
        if (nargs != 1)
            die("typecheck:%d: create() takes 1 argument, "
                "got %d", n->line, nargs);
        return &t_obj;
    }
    if (strcmp(name, "recycle") == 0) {
        if (nargs != 1)
            die("typecheck:%d: recycle() takes 1 argument, "
                "got %d", n->line, nargs);
        return NULL;
    }
    if (strcmp(name, "contents") == 0) {
        if (nargs != 1)
            die("typecheck:%d: contents() takes 1 argument, "
                "got %d", n->line, nargs);
        if (argtypes[0] && argtypes[0]->kind != T_TOBJ)
            die("typecheck:%d: contents() requires obj, got %s",
                n->line, type_name(argtypes[0]));
        struct moo_type *t = arena_zalloc(tc_arena, sizeof(*t));
        t->kind = T_TLIST;
        t->inner = &t_obj;
        return t;
    }
    if (strcmp(name, "location") == 0) {
        if (nargs != 1)
            die("typecheck:%d: location() takes 1 argument, "
                "got %d", n->line, nargs);
        if (argtypes[0] && argtypes[0]->kind != T_TOBJ)
            die("typecheck:%d: location() requires obj, got %s",
                n->line, type_name(argtypes[0]));
        return &t_obj;
    }

    if (strcmp(name, "typeof") == 0) {
        if (nargs != 1)
            die("typecheck:%d: typeof() takes 1 argument, "
                "got %d", n->line, nargs);
        return &t_int;
    }

    /* user-defined verb/func call */
    struct tc_verb *v = tc_find_verb(name);
    if (v) {
        int pi = 0;
        for (struct node *p = v->params; p; p = p->next, pi++) {
            if (pi >= nargs)
                die("typecheck:%d: %s() expects more "
                    "arguments (got %d)", n->line, name,
                    nargs);
            if (p->type && argtypes[pi] &&
                !types_compatible(p->type, argtypes[pi]))
                die("typecheck:%d: %s() argument %d: "
                    "expected %s, got %s",
                    n->line, name, pi + 1,
                    type_name(p->type),
                    type_name(argtypes[pi]));
        }
        if (pi != nargs)
            die("typecheck:%d: %s() expects %d arguments, "
                "got %d", n->line, name, pi, nargs);
        if (v->ret_type)
            return v->ret_type;
    }

    return &t_int;
}

/****************************************************************
 * Value-type method call checking
 ****************************************************************/

static struct moo_type *
tc_vcall(struct node *n)
{
    struct moo_type *recv;
    struct moo_type *argtypes[16];
    int nargs = 0;

    recv = tc_expr(n->a);
    for (struct node *a = n->b; a; a = a->next) {
        if (nargs >= 16)
            die("typecheck:%d: too many arguments", n->line);
        argtypes[nargs++] = tc_expr(a);
    }

    if (!recv || recv->kind == T_TOBJ)
        return NULL;

    if (recv->kind == T_TIFACE) {
        if (recv->name) {
            struct node *idef = tc_find_iface(recv->name);
            if (idef) {
                for (struct node *m = idef->a; m;
                     m = m->next) {
                    if (m->kind == N_IFACE_VERB &&
                        strcmp(m->name, n->name) == 0)
                        return NULL;
                }
                die("typecheck:%d: interface '%s' "
                    "has no verb '%s'",
                    n->line, recv->name, n->name);
            }
        }
        return NULL;
    }

    const char *meth = n->name;

    if (recv->kind == T_TSTR) {
        if (strcmp(meth, "length") == 0) {
            if (nargs != 0)
                die("typecheck:%d: str:length() takes no "
                    "arguments", n->line);
            return &t_int;
        }
        if (strcmp(meth, "index") == 0) {
            if (nargs != 1)
                die("typecheck:%d: str:index() takes 1 "
                    "argument, got %d", n->line, nargs);
            if (argtypes[0] && argtypes[0]->kind != T_TSTR)
                die("typecheck:%d: str:index() argument must "
                    "be str, got %s",
                    n->line, type_name(argtypes[0]));
            return &t_int;
        }
        if (strcmp(meth, "substr") == 0) {
            if (nargs != 2)
                die("typecheck:%d: str:substr() takes 2 "
                    "arguments, got %d", n->line, nargs);
            if (argtypes[0] && !is_numeric(argtypes[0]))
                die("typecheck:%d: str:substr() start must "
                    "be int, got %s",
                    n->line, type_name(argtypes[0]));
            if (argtypes[1] && !is_numeric(argtypes[1]))
                die("typecheck:%d: str:substr() end must "
                    "be int, got %s",
                    n->line, type_name(argtypes[1]));
            return &t_str;
        }
        if (strcmp(meth, "strsub") == 0) {
            if (nargs != 2)
                die("typecheck:%d: str:strsub() takes 2 "
                    "arguments, got %d", n->line, nargs);
            if (argtypes[0] && argtypes[0]->kind != T_TSTR)
                die("typecheck:%d: str:strsub() first "
                    "argument must be str, got %s",
                    n->line, type_name(argtypes[0]));
            if (argtypes[1] && argtypes[1]->kind != T_TSTR)
                die("typecheck:%d: str:strsub() second "
                    "argument must be str, got %s",
                    n->line, type_name(argtypes[1]));
            return &t_str;
        }
        die("typecheck:%d: str has no method '%s'",
            n->line, meth);
    }

    if (recv->kind == T_TLIST) {
        if (strcmp(meth, "length") == 0) {
            if (nargs != 0)
                die("typecheck:%d: list:length() takes no "
                    "arguments", n->line);
            return &t_int;
        }
        if (strcmp(meth, "append") == 0) {
            if (nargs != 1)
                die("typecheck:%d: list:append() takes 1 "
                    "argument, got %d", n->line, nargs);
            return recv;
        }
        if (strcmp(meth, "delete") == 0) {
            if (nargs != 1)
                die("typecheck:%d: list:delete() takes 1 "
                    "argument, got %d", n->line, nargs);
            if (argtypes[0] && !is_numeric(argtypes[0]))
                die("typecheck:%d: list:delete() index must "
                    "be int, got %s",
                    n->line, type_name(argtypes[0]));
            return recv;
        }
        if (strcmp(meth, "set") == 0) {
            if (nargs != 2)
                die("typecheck:%d: list:set() takes 2 "
                    "arguments, got %d", n->line, nargs);
            if (argtypes[0] && !is_numeric(argtypes[0]))
                die("typecheck:%d: list:set() index must "
                    "be int, got %s",
                    n->line, type_name(argtypes[0]));
            return recv;
        }
        die("typecheck:%d: list has no method '%s'",
            n->line, meth);
    }

    if (recv->kind == T_TINT) {
        if (strcmp(meth, "tostr") == 0) {
            if (nargs != 0)
                die("typecheck:%d: int:tostr() takes no "
                    "arguments", n->line);
            return &t_str;
        }
        if (strcmp(meth, "tofloat") == 0) {
            if (nargs != 0)
                die("typecheck:%d: int:tofloat() takes no "
                    "arguments", n->line);
            return &t_float;
        }
        die("typecheck:%d: int has no method '%s'",
            n->line, meth);
    }
    if (recv->kind == T_TFLOAT) {
        if (strcmp(meth, "tostr") == 0) {
            if (nargs != 0)
                die("typecheck:%d: float:tostr() takes no "
                    "arguments", n->line);
            return &t_str;
        }
        if (strcmp(meth, "toint") == 0) {
            if (nargs != 0)
                die("typecheck:%d: float:toint() takes no "
                    "arguments", n->line);
            return &t_int;
        }
        die("typecheck:%d: float has no method '%s'",
            n->line, meth);
    }
    if (recv->kind == T_TBOOL) {
        die("typecheck:%d: bool has no method '%s'",
            n->line, meth);
    }
    if (recv->kind == T_TERR) {
        if (strcmp(meth, "tostr") == 0) {
            if (nargs != 0)
                die("typecheck:%d: err:tostr() takes no "
                    "arguments", n->line);
            return &t_str;
        }
        die("typecheck:%d: err has no method '%s'",
            n->line, meth);
    }

    return NULL;
}

/****************************************************************
 * Statement type checking
 ****************************************************************/

static int in_defer;

static void
tc_stmt(struct node *n)
{
    if (!n)
        return;

    switch (n->kind) {
    case N_BLOCK:
        for (struct node *s = n->a; s; s = s->next)
            tc_stmt(s);
        return;

    case N_EXPR_STMT:
        if (n->a)
            (void)tc_expr(n->a);
        return;

    case N_VAR_DECL: {
        struct moo_type *vt = n->type;
        tc_add(n->name, vt);
        if (n->a) {
            struct moo_type *et = tc_expr(n->a);
            if (vt && et && !types_compatible(vt, et))
                die("typecheck:%d: cannot assign %s to "
                    "variable '%s' of type %s",
                    n->line, type_name(et), n->name,
                    type_name(vt));
        }
        return;
    }

    case N_CONST_DECL: {
        struct moo_type *vt = n->type;
        tc_add(n->name, vt);
        if (n->a) {
            struct moo_type *et = tc_expr(n->a);
            if (vt && et && !types_compatible(vt, et))
                die("typecheck:%d: cannot assign %s to "
                    "constant '%s' of type %s",
                    n->line, type_name(et), n->name,
                    type_name(vt));
        }
        return;
    }

    case N_ASSIGN: {
        struct moo_type *tt, *et;

        if (n->a->kind == N_PROP || n->a->kind == N_CPROP) {
            (void)tc_expr(n->a);
            (void)tc_expr(n->b);
            return;
        }
        if (n->a->kind == N_INDEX) {
            struct moo_type *lt = tc_expr(n->a->a);
            (void)tc_expr(n->a->b);
            et = tc_expr(n->b);
            if (lt && lt->kind == T_TLIST && lt->inner &&
                et && !types_compatible(lt->inner, et))
                die("typecheck:%d: cannot assign %s to "
                    "element of list<%s>",
                    n->line, type_name(et),
                    type_name(lt->inner));
            return;
        }

        et = tc_expr(n->b);
        if (n->a->kind != N_NAME)
            return;
        tt = tc_find(n->a->name);
        if (!tt)
            die("typecheck:%d: undefined '%s'",
                n->line, n->a->name);

        if (n->op == T_PLUSEQ || n->op == T_MINUSEQ) {
            if (!is_numeric(tt))
                die("typecheck:%d: '%s' requires int "
                    "variable, '%s' is %s", n->line,
                    tok_str(n->op), n->a->name,
                    type_name(tt));
            if (et && !is_numeric(et))
                die("typecheck:%d: '%s' requires int "
                    "value, got %s", n->line,
                    tok_str(n->op), type_name(et));
            return;
        }

        if (tt && et && !types_compatible(tt, et))
            die("typecheck:%d: cannot assign %s to '%s' "
                "of type %s", n->line, type_name(et),
                n->a->name, type_name(tt));
        return;
    }

    case N_IF:
        (void)tc_expr(n->a);
        tc_stmt(n->b);
        if (n->c)
            tc_stmt(n->c);
        return;

    case N_WHILE:
        (void)tc_expr(n->a);
        tc_stmt(n->b);
        return;

    case N_FOR: {
        int mark = nsyms;
        if (n->b) {
            /* for x in lo..hi */
            struct moo_type *lo = tc_expr(n->a);
            struct moo_type *hi = tc_expr(n->b);
            if (lo && !is_numeric(lo))
                die("typecheck:%d: range bound must be "
                    "int, got %s", n->line, type_name(lo));
            if (hi && !is_numeric(hi))
                die("typecheck:%d: range bound must be "
                    "int, got %s", n->line, type_name(hi));
            tc_add(n->name, &t_int);
        } else {
            /* for x in collection */
            struct moo_type *ct = tc_expr(n->a);
            if (ct && ct->kind != T_TLIST)
                die("typecheck:%d: for-in requires list, "
                    "got %s", n->line, type_name(ct));
            if (ct && ct->inner)
                tc_add(n->name, ct->inner);
            else
                tc_add(n->name, NULL);
        }
        tc_stmt(n->c);
        tc_pop_scope(mark);
        return;
    }

    case N_RETURN:
        if (n->a)
            (void)tc_expr(n->a);
        return;

    case N_RETURN_PUSH:
        if (n->a)
            (void)tc_expr(n->a);
        return;

    case N_DEFER: {
        int prev = in_defer;
        in_defer = 1;
        tc_stmt(n->a);
        in_defer = prev;
        return;
    }

    case N_PANIC_STMT:
        if (n->a) {
            struct moo_type *et = tc_expr(n->a);
            if (et && et->kind != T_TERR)
                die("typecheck:%d: panic() requires err, "
                    "got %s", n->line, type_name(et));
        }
        return;

    case N_TRACE_STMT:
        if (n->a) {
            struct moo_type *et = tc_expr(n->a);
            if (et && et->kind != T_TSTR)
                die("typecheck:%d: trace requires str, "
                    "got %s", n->line, type_name(et));
        }
        return;

    case N_SWITCH: {
        struct moo_type *st = tc_expr(n->a);
        for (struct node *c = n->b; c; c = c->next) {
            for (struct node *v = c->a; v; v = v->next) {
                struct moo_type *vt = tc_expr(v->a);
                if (st && vt && !types_compatible(st, vt))
                    die("typecheck:%d: case value type "
                        "%s doesn't match switch "
                        "type %s", v->line,
                        type_name(vt), type_name(st));
                if (v->b) {
                    struct moo_type *ht = tc_expr(v->b);
                    if (vt && !is_numeric(vt))
                        die("typecheck:%d: range bound "
                            "must be int, got %s",
                            v->line, type_name(vt));
                    if (ht && !is_numeric(ht))
                        die("typecheck:%d: range bound "
                            "must be int, got %s",
                            v->line, type_name(ht));
                }
            }
            tc_stmt(c->b);
        }
        return;
    }

    case N_TYPESWITCH: {
        struct moo_type *st = tc_expr(n->a);
        if (st && st->kind != T_TOBJ && st->kind != T_TIFACE)
            die("typecheck:%d: switch type requires obj, "
                "got %s", n->line, type_name(st));
        for (struct node *c = n->b; c; c = c->next) {
            if (!c->ival && c->name) {
                if (!tc_find_iface(c->name))
                    die("typecheck:%d: unknown "
                        "interface '%s'",
                        c->line, c->name);
                if (n->a->kind == N_NAME) {
                    int mark = nsyms;
                    struct moo_type *t =
                        arena_zalloc(tc_arena, sizeof(*t));
                    t->kind = T_TIFACE;
                    t->name = c->name;
                    tc_add(n->a->name, t);
                    tc_stmt(c->b);
                    tc_pop_scope(mark);
                } else {
                    tc_stmt(c->b);
                }
            } else {
                tc_stmt(c->b);
            }
        }
        return;
    }

    case N_TRACE_CMT:
    case N_BREAK:
    case N_CONTINUE:
        return;

    default:
        return;
    }
}

/****************************************************************
 * Top-level
 ****************************************************************/

void
typecheck_program(struct arena *a, struct node *ast)
{
    tc_arena = a;
    if (!ast || ast->kind != N_PROGRAM)
        return;

    /* first pass: register all verb/func and interface names */
    for (struct node *v = ast->a; v; v = v->next) {
        if (v->kind == N_VERB || v->kind == N_FUNC)
            tc_add_verb(v->name, v->a, v->type);
        else if (v->kind == N_EXTERN_VERB ||
                 v->kind == N_EXTERN_FUNC)
            tc_add_verb(v->name, v->a, v->type);
        else if (v->kind == N_INTERFACE)
            tc_add_iface(v);
    }

    /* second pass: type-check each verb/func body */
    for (struct node *v = ast->a; v; v = v->next) {
        if (v->kind != N_VERB && v->kind != N_FUNC)
            continue;

        nsyms = 0;
        in_defer = 0;
        for (struct node *p = v->a; p; p = p->next)
            tc_add(p->name, p->type);

        tc_stmt(v->b);
    }

    /* cleanup working arrays */
    free(syms);
    syms = NULL;
    nsyms = sym_cap = 0;

    free(tc_verbs);
    tc_verbs = NULL;
    ntc_verbs = tc_verb_cap = 0;

    free(ifaces);
    ifaces = NULL;
    nifaces = iface_cap = 0;
}
