/* moo.h : shared declarations for the MooScript compiler */
#ifndef MOO_H
#define MOO_H

#include "ir.h"

/***** Lexer *****/

enum tok {
    T_EOF = 0,
    /***** keywords *****/
    T_VERB, T_VAR, T_CONST, T_ENDVERB,
    T_FUNC, T_ENDFUNC,
    T_IF, T_ELSEIF, T_ELSE, T_ENDIF,
    T_FOR, T_IN, T_WHILE, T_ENDFOR, T_ENDWHILE,
    T_BREAK, T_CONTINUE, T_RETURN,
    T_DEFER, T_PANIC, T_RECOVER, T_ENDDEFER,
    T_INTERFACE, T_ENDINTERFACE, T_IS, T_AS,
    T_SWITCH, T_ENDSWITCH, T_CASE,
    T_TRACE,
    T_MODULE, T_IMPORT, T_EXPORT, T_EXTERN,
    T_TRUE, T_FALSE, T_NIL,
    /* type keywords */
    T_TINT, T_TSTR, T_TOBJ, T_TBOOL, T_TERR, T_TLIST, T_TPROP, T_TIFACE,
    T_TFLOAT,
    /***** literals *****/
    T_IDENT, T_NUMBER, T_FLOAT_LIT, T_STRING, T_OBJLIT, T_ERRCODE,
    /***** operators *****/
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE,
    T_ANDAND, T_OROR, T_BANG,
    T_ASSIGN, T_PLUSEQ, T_MINUSEQ,
    T_DOTDOT, T_ARROW,
    /***** punctuation *****/
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE,
    T_LBRACK, T_RBRACK,
    T_DOT, T_COLON, T_SEMI, T_COMMA,
    /***** special *****/
    T_TRACE_COMMENT,
};

struct token {
    int kind;
    long nval;
    double fval;
    char *sval;
    int slen;
    int line;
};

void lex_init(struct arena *a, const char *src, const char *filename, int trace_on);
struct token lex_next(void);
struct token lex_peek(void);
const char *tok_str(int kind);

/***** AST *****/

enum node_kind {
    N_PROGRAM,
    /* top-level */
    N_VERB, N_FUNC, N_CONST_DECL, N_PARAM,
    N_EXTERN_VERB, N_EXTERN_FUNC,
    N_MODULE, N_IMPORT,
    /***** statements *****/
    N_BLOCK, N_VAR_DECL, N_ASSIGN,
    N_IF, N_FOR, N_WHILE,
    N_RETURN, N_RETURN_PUSH, N_DEFER, N_PANIC_STMT, N_TRACE_STMT, N_TRACE_CMT,
    N_BREAK, N_CONTINUE, N_EXPR_STMT,
    /***** expressions *****/
    N_BINOP, N_UNOP,
    N_PROP, N_CPROP, N_VCALL,
    N_INDEX, N_SLICE, N_CALL,
    N_NAME, N_NUM, N_FLOAT, N_STR, N_OBJREF, N_ERRVAL,
    N_BOOL, N_NIL, N_RECOVER, N_LISTLIT,
    /***** interfaces *****/
    N_INTERFACE, N_IFACE_PROP, N_IFACE_VERB,
    N_IS_EXPR, N_AS_EXPR,
    /***** switch *****/
    N_SWITCH, N_TYPESWITCH, N_CASE, N_CASE_VAL,
};

struct moo_type {
    int kind;
    struct moo_type *inner;
    char *name;
};

struct node {
    int kind;
    int op;
    int line;
    struct node *a, *b, *c;
    struct node *next;
    char *name;
    long ival;
    double fval;
    char *sval;
    int slen;
    struct moo_type *type;
    char *link_name;
    int exported;
};

struct node *parse_program(struct arena *a);

/***** Type checker *****/

void typecheck_program(struct arena *a, struct node *ast);

/***** Lowering *****/

struct ir_program *lower_program(struct arena *a, struct node *ast);

#endif /* MOO_H */
