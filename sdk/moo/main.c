/* main.c : MooScript compiler driver */

#include "moo.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
cleanup_arena(void *p)
{
    arena_free(p);
}

static void
cleanup_free(void *p)
{
    free(p);
}

/****************************************************************
 * Token dump
 ****************************************************************/

static void
dump_tokens(struct arena *a, const char *src, const char *filename, int trace)
{
    struct token t;

    lex_init(a, src, filename, trace);
    for (;;) {
        t = lex_next();
        if (t.kind == T_EOF)
            break;
        printf("%4d  %-16s", t.line, tok_str(t.kind));
        if (t.sval)
            printf("  %s", t.sval);
        else if (t.kind == T_NUMBER)
            printf("  %ld", t.nval);
        putchar('\n');
    }
}

/****************************************************************
 * AST dump
 ****************************************************************/

static void
print_type(struct moo_type *t)
{
    if (!t) {
        printf("??");
        return;
    }
    switch (t->kind) {
    case T_TINT:  printf("int"); break;
    case T_TSTR:  printf("str"); break;
    case T_TOBJ:  printf("obj"); break;
    case T_TBOOL: printf("bool"); break;
    case T_TERR:  printf("err"); break;
    case T_TLIST:
        printf("list<");
        print_type(t->inner);
        printf(">");
        break;
    default:
        printf("?type?");
    }
}

static void
ind(int depth)
{
    for (int i = 0; i < depth; i++)
        printf("  ");
}

static void dump(struct node *n, int depth);

static void
dump_list(struct node *n, int depth)
{
    for (; n; n = n->next)
        dump(n, depth);
}

static void
dump(struct node *n, int depth)
{
    struct node *c;

    if (!n)
        return;

    ind(depth);
    switch (n->kind) {
    case N_PROGRAM:
        printf("PROGRAM\n");
        dump_list(n->a, depth + 1);
        break;

    case N_MODULE:
        printf("MODULE %s\n", n->name);
        break;

    case N_IMPORT:
        printf("IMPORT %s\n", n->name);
        break;

    case N_VERB:
    case N_FUNC:
        printf("%s%s %s",
               n->exported ? "EXPORT " : "",
               n->kind == N_FUNC ? "FUNC" : "VERB", n->name);
        if (n->type) {
            printf(" -> ");
            print_type(n->type);
        }
        printf("\n");
        dump_list(n->a, depth + 1);
        dump(n->b, depth + 1);
        break;

    case N_EXTERN_VERB:
    case N_EXTERN_FUNC:
        printf("EXTERN %s %s",
               n->kind == N_EXTERN_FUNC ? "FUNC" : "VERB", n->name);
        if (n->link_name)
            printf(" (link: %s)", n->link_name);
        if (n->type) {
            printf(" -> ");
            print_type(n->type);
        }
        printf("\n");
        dump_list(n->a, depth + 1);
        break;

    case N_CONST_DECL:
        printf("CONST %s: ", n->name);
        print_type(n->type);
        printf("\n");
        dump(n->a, depth + 1);
        break;

    case N_PARAM:
        printf("PARAM %s: ", n->name);
        print_type(n->type);
        printf("\n");
        break;

    case N_BLOCK:
        printf("BLOCK\n");
        dump_list(n->a, depth + 1);
        break;

    case N_VAR_DECL:
        printf("VAR %s: ", n->name);
        print_type(n->type);
        printf("\n");
        if (n->a)
            dump(n->a, depth + 1);
        break;

    case N_ASSIGN:
        printf("ASSIGN %s\n", tok_str(n->op));
        dump(n->a, depth + 1);
        dump(n->b, depth + 1);
        break;

    case N_IF:
        printf("IF\n");
        ind(depth + 1); printf("cond:\n");
        dump(n->a, depth + 2);
        ind(depth + 1); printf("then:\n");
        dump(n->b, depth + 2);
        if (n->c) {
            if (n->c->kind == N_IF) {
                ind(depth + 1); printf("elseif:\n");
                dump(n->c, depth + 2);
            } else {
                ind(depth + 1); printf("else:\n");
                dump(n->c, depth + 2);
            }
        }
        break;

    case N_FOR:
        printf("FOR %s", n->name);
        if (n->b)
            printf(" (range)\n");
        else
            printf(" (iter)\n");
        dump(n->a, depth + 1);
        if (n->b)
            dump(n->b, depth + 1);
        dump(n->c, depth + 1);
        break;

    case N_WHILE:
        printf("WHILE\n");
        dump(n->a, depth + 1);
        dump(n->b, depth + 1);
        break;

    case N_RETURN:
        printf("RETURN\n");
        if (n->a)
            dump(n->a, depth + 1);
        break;

    case N_RETURN_PUSH:
        printf("RETURN..\n");
        dump(n->a, depth + 1);
        break;

    case N_DEFER:
        printf("DEFER\n");
        dump(n->a, depth + 1);
        break;

    case N_PANIC_STMT:
        printf("PANIC\n");
        dump(n->a, depth + 1);
        break;

    case N_TRACE_STMT:
        printf("TRACE\n");
        dump(n->a, depth + 1);
        break;

    case N_TRACE_CMT:
        printf("/// %s\n", n->sval);
        break;

    case N_BREAK:
        printf("BREAK\n");
        break;

    case N_CONTINUE:
        printf("CONTINUE\n");
        break;

    case N_EXPR_STMT:
        printf("EXPR_STMT\n");
        dump(n->a, depth + 1);
        break;

    case N_BINOP:
        printf("BINOP %s\n", tok_str(n->op));
        dump(n->a, depth + 1);
        dump(n->b, depth + 1);
        break;

    case N_UNOP:
        printf("UNOP %s\n", tok_str(n->op));
        dump(n->a, depth + 1);
        break;

    case N_PROP:
        printf("PROP .%s\n", n->name);
        dump(n->a, depth + 1);
        break;

    case N_CPROP:
        printf("CPROP\n");
        dump(n->a, depth + 1);
        dump(n->b, depth + 1);
        break;

    case N_VCALL:
        printf("VCALL :%s\n", n->name);
        dump(n->a, depth + 1);
        for (c = n->b; c; c = c->next)
            dump(c, depth + 1);
        break;

    case N_INDEX:
        printf("INDEX\n");
        dump(n->a, depth + 1);
        dump(n->b, depth + 1);
        break;

    case N_SLICE:
        printf("SLICE\n");
        dump(n->a, depth + 1);
        dump(n->b, depth + 1);
        dump(n->c, depth + 1);
        break;

    case N_CALL:
        printf("CALL\n");
        dump(n->a, depth + 1);
        for (c = n->b; c; c = c->next)
            dump(c, depth + 1);
        break;

    case N_NAME:
        printf("NAME %s\n", n->name);
        break;

    case N_NUM:
        printf("NUM %ld\n", n->ival);
        break;

    case N_STR:
        printf("STR \"%s\"\n", n->sval);
        break;

    case N_OBJREF:
        printf("OBJREF $\"%s\"\n", n->sval);
        break;

    case N_ERRVAL:
        printf("ERRVAL $%s\n", n->sval);
        break;

    case N_BOOL:
        printf("BOOL %s\n", n->ival ? "true" : "false");
        break;

    case N_NIL:
        printf("NIL\n");
        break;

    case N_RECOVER:
        printf("RECOVER\n");
        break;

    case N_LISTLIT:
        printf("LIST\n");
        dump_list(n->a, depth + 1);
        break;

    default:
        printf("?node(%d)?\n", n->kind);
    }
}

/****************************************************************
 * Usage and main
 ****************************************************************/

static void
usage(const char *prog)
{
    fprintf(stderr, "usage: %s [options] <file.moo>\n", prog);
    fprintf(stderr, "  -o <file>   output assembly file\n");
    fprintf(stderr, "  -t          enable trace comments\n");
    fprintf(stderr, "  -V          show version\n");
    fprintf(stderr, "  --tokens    dump token stream and exit\n");
    fprintf(stderr, "  --ast       dump AST and exit\n");
    exit(1);
}

int
main(int argc, char **argv)
{
    const char *filename;
    const char *outfile;
    char *src;
    struct node *ast;
    int trace;
    int tokens_only;
    int ast_only;

    util_set_progname("mooc");

    filename = NULL;
    outfile = NULL;
    trace = 0;
    tokens_only = 0;
    ast_only = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc)
                usage(argv[0]);
            outfile = argv[i];
        } else if (strcmp(argv[i], "-V") == 0) {
            printf("skj-mooc %s\n", SKJ_VERSION);
            return 0;
        } else if (strcmp(argv[i], "-t") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "--tokens") == 0) {
            tokens_only = 1;
        } else if (strcmp(argv[i], "--ast") == 0) {
            ast_only = 1;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
        } else {
            if (filename)
                usage(argv[0]);
            filename = argv[i];
        }
    }
    if (!filename)
        usage(argv[0]);

    if (setjmp(util_die_env) != 0)
        return 1;
    util_die_active = 1;

    src = slurp(filename);
    util_cleanup_push(cleanup_free, src);

    {
        struct arena a;

        arena_init(&a);
        util_cleanup_push(cleanup_arena, &a);

        if (tokens_only) {
            dump_tokens(&a, src, filename, trace);
            util_cleanup_run();
            return 0;
        }

        lex_init(&a, src, filename, trace);
        ast = parse_program(&a);

        if (ast_only) {
            dump(ast, 0);
            util_cleanup_run();
            return 0;
        }

        typecheck_program(&a, ast);

        {
            struct ir_program *prog;
            struct ir_func *fn;
            FILE *out;

            prog = lower_program(&a, ast);

        for (fn = prog->funcs; fn; fn = fn->next)
            regalloc(fn);

        if (outfile) {
            out = fopen(outfile, "w");
            if (!out)
                die("cannot open '%s' for writing", outfile);
        } else {
            out = stdout;
        }

        target_emit(out, prog);

        if (outfile)
            fclose(out);
        }
    }

    util_cleanup_run();
    return 0;
}
