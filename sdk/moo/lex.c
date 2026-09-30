/* lex.c - Hand-written lexer for MooScript.
 *
 * Single-buffer, one-token lookahead. The buffer is owned by the
 * caller of lex_init (main slurps the whole file into memory).
 *
 * Numbers: decimal or hex (0x...). Strings support \n \t \r \\ \"
 * and \xNN. Line comments (//), block comments (no nesting).
 * Trace comments /// emit T_TRACE_COMMENT when tracing
 * is enabled, otherwise discarded as regular comments.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "moo.h"

static struct arena *lex_arena;
static const char *src_buf;
static const char *src_file;
static const char *p;
static int line;
static int peeked;
static struct token peek_tok;
static int trace_on;

struct kw { const char *s; int k; };
static const struct kw keywords[] = {
    { "verb",     T_VERB },
    { "endverb",  T_ENDVERB },
    { "func",     T_FUNC },
    { "endfunc",  T_ENDFUNC },
    { "var",      T_VAR },
    { "const",    T_CONST },
    { "if",       T_IF },
    { "elseif",   T_ELSEIF },
    { "else",     T_ELSE },
    { "endif",    T_ENDIF },
    { "for",      T_FOR },
    { "in",       T_IN },
    { "while",    T_WHILE },
    { "endfor",   T_ENDFOR },
    { "endwhile", T_ENDWHILE },
    { "break",    T_BREAK },
    { "continue", T_CONTINUE },
    { "return",   T_RETURN },
    { "defer",         T_DEFER },
    { "enddefer",      T_ENDDEFER },
    { "interface",     T_INTERFACE },
    { "endinterface",  T_ENDINTERFACE },
    { "is",            T_IS },
    { "as",            T_AS },
    { "switch",        T_SWITCH },
    { "endswitch",     T_ENDSWITCH },
    { "case",          T_CASE },
    { "panic",         T_PANIC },
    { "recover",       T_RECOVER },
    { "trace",    T_TRACE },
    { "module",   T_MODULE },
    { "import",   T_IMPORT },
    { "export",   T_EXPORT },
    { "extern",   T_EXTERN },
    { "true",     T_TRUE },
    { "false",    T_FALSE },
    { "nil",      T_NIL },
    { "int",      T_TINT },
    { "str",      T_TSTR },
    { "obj",      T_TOBJ },
    { "bool",     T_TBOOL },
    { "err",      T_TERR },
    { "list",     T_TLIST },
    { "prop",     T_TPROP },
    { "float",    T_TFLOAT },
    { NULL, 0 }
};

void
lex_init(struct arena *a, const char *src, const char *filename, int trace)
{
    lex_arena = a;
    src_buf = src;
    src_file = filename ? filename : "<input>";
    p = src;
    line = 1;
    peeked = 0;
    trace_on = trace;
}

static void
skip_ws(void)
{
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r')
            p++;
        if (*p == '\n') {
            line++;
            p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '/') {
            if (p[2] == '/' && trace_on)
                break;
            while (*p && *p != '\n')
                p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n')
                    line++;
                p++;
            }
            if (*p)
                p += 2;
            continue;
        }
        break;
    }
}

static int
hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static int
read_escape(void)
{
    int c, h1, h2;

    c = *p++;
    switch (c) {
    case 'n':  return '\n';
    case 't':  return '\t';
    case 'r':  return '\r';
    case '\\': return '\\';
    case '"':  return '"';
    case 'x':
        h1 = hex_digit((unsigned char)*p);
        if (h1 < 0)
            die("%s:%d: bad \\x escape", src_file, line);
        p++;
        h2 = hex_digit((unsigned char)*p);
        if (h2 < 0)
            return h1;
        p++;
        return (h1 << 4) | h2;
    default:
        die("%s:%d: unknown escape \\%c", src_file, line, c);
    }
    return 0;
}

static struct token
make(int k)
{
    struct token t;
    t.kind = k;
    t.nval = 0;
    t.sval = NULL;
    t.slen = 0;
    t.line = line;
    return t;
}

static struct token
lex_string(int tok_kind)
{
    struct token t;
    char *buf;
    size_t cap, len;

    cap = 16;
    len = 0;
    buf = xmalloc(cap);
    while (*p && *p != '"') {
        int ch;
        if (*p == '\\') {
            p++;
            ch = read_escape();
        } else if (*p == '\n') {
            die("%s:%d: newline in string", src_file, line);
        } else {
            ch = (unsigned char)*p++;
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) die("out of memory");
        }
        buf[len++] = (char)ch;
    }
    if (*p != '"')
        die("%s:%d: unterminated string", src_file, line);
    p++;
    buf[len] = '\0';
    t = make(tok_kind);
    t.sval = arena_strndup(lex_arena, buf, len);
    t.slen = (int)len;
    free(buf);
    return t;
}

static struct token
lex_one(void)
{
    struct token t;
    const char *start;
    int c;
    long n;

    skip_ws();
    t = make(T_EOF);
    if (*p == '\0')
        return t;

    c = (unsigned char)*p;

    /* /// trace comments (only reached when trace_on) */
    if (c == '/' && p[1] == '/' && p[2] == '/') {
        p += 3;
        while (*p == ' ' || *p == '\t')
            p++;
        start = p;
        while (*p && *p != '\n')
            p++;
        t = make(T_TRACE_COMMENT);
        t.sval = arena_strndup(lex_arena,start, (size_t)(p - start));
        t.slen = (int)(p - start);
        return t;
    }

    /* identifiers and keywords */
    if (isalpha(c) || c == '_') {
        const struct kw *kw;
        size_t sz;
        start = p;
        while (isalnum((unsigned char)*p) || *p == '_')
            p++;
        sz = (size_t)(p - start);
        for (kw = keywords; kw->s; kw++) {
            if (strlen(kw->s) == sz && memcmp(kw->s, start, sz) == 0)
                return make(kw->k);
        }
        t = make(T_IDENT);
        t.sval = arena_strndup(lex_arena,start, sz);
        return t;
    }

    /* number literals (integer and float) */
    if (isdigit(c)) {
        const char *num_start = p;
        int is_float = 0;

        n = 0;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            int d;
            p += 2;
            if (hex_digit((unsigned char)*p) < 0)
                die("%s:%d: bad hex literal", src_file, line);
            while ((d = hex_digit((unsigned char)*p)) >= 0) {
                n = (n << 4) | d;
                p++;
            }
            t = make(T_NUMBER);
            t.nval = n;
            return t;
        }
        while (isdigit((unsigned char)*p))
            p++;
        if (*p == '.' && isdigit((unsigned char)p[1])) {
            is_float = 1;
            p++;
            while (isdigit((unsigned char)*p))
                p++;
        }
        if (*p == 'e' || *p == 'E') {
            is_float = 1;
            p++;
            if (*p == '+' || *p == '-')
                p++;
            if (!isdigit((unsigned char)*p))
                die("%s:%d: bad float exponent", src_file, line);
            while (isdigit((unsigned char)*p))
                p++;
        }
        if (is_float) {
            t = make(T_FLOAT_LIT);
            t.fval = strtod(num_start, NULL);
            return t;
        }
        n = 0;
        for (const char *q = num_start; q < p; q++)
            n = n * 10 + (*q - '0');
        t = make(T_NUMBER);
        t.nval = n;
        return t;
    }

    /* string literals */
    if (c == '"') {
        p++;
        return lex_string(T_STRING);
    }

    /* $ prefix: object literal or error code */
    if (c == '$') {
        p++;
        if (*p == '"') {
            p++;
            return lex_string(T_OBJLIT);
        }
        if (isalpha((unsigned char)*p) || *p == '_') {
            start = p;
            while (isalnum((unsigned char)*p) || *p == '_')
                p++;
            t = make(T_ERRCODE);
            t.sval = arena_strndup(lex_arena,start, (size_t)(p - start));
            return t;
        }
        die("%s:%d: unexpected character after '$'", src_file, line);
    }

    /* operators and punctuation */
    switch (c) {
    case '(': p++; return make(T_LPAREN);
    case ')': p++; return make(T_RPAREN);
    case '{': p++; return make(T_LBRACE);
    case '}': p++; return make(T_RBRACE);
    case '[': p++; return make(T_LBRACK);
    case ']': p++; return make(T_RBRACK);
    case ';': p++; return make(T_SEMI);
    case ',': p++; return make(T_COMMA);
    case ':': p++; return make(T_COLON);
    case '*': p++; return make(T_STAR);
    case '/': p++; return make(T_SLASH);
    case '%': p++; return make(T_PERCENT);
    case '.':
        p++;
        if (*p == '.') { p++; return make(T_DOTDOT); }
        return make(T_DOT);
    case '+':
        p++;
        if (*p == '=') { p++; return make(T_PLUSEQ); }
        return make(T_PLUS);
    case '-':
        p++;
        if (*p == '>') { p++; return make(T_ARROW); }
        if (*p == '=') { p++; return make(T_MINUSEQ); }
        return make(T_MINUS);
    case '=':
        p++;
        if (*p == '=') { p++; return make(T_EQ); }
        return make(T_ASSIGN);
    case '!':
        p++;
        if (*p == '=') { p++; return make(T_NE); }
        return make(T_BANG);
    case '<':
        p++;
        if (*p == '=') { p++; return make(T_LE); }
        return make(T_LT);
    case '>':
        p++;
        if (*p == '=') { p++; return make(T_GE); }
        return make(T_GT);
    case '&':
        p++;
        if (*p == '&') { p++; return make(T_ANDAND); }
        die("%s:%d: unexpected '&' (use '&&')", src_file, line);
        break;
    case '|':
        p++;
        if (*p == '|') { p++; return make(T_OROR); }
        die("%s:%d: unexpected '|' (use '||')", src_file, line);
        break;
    }

    die("%s:%d: unexpected character 0x%02x '%c'", src_file, line,
        c, isprint(c) ? c : '?');
    return t;
}

struct token
lex_next(void)
{
    if (peeked) {
        peeked = 0;
        return peek_tok;
    }
    return lex_one();
}

struct token
lex_peek(void)
{
    if (!peeked) {
        peek_tok = lex_one();
        peeked = 1;
    }
    return peek_tok;
}

const char *
tok_str(int kind)
{
    switch (kind) {
    case T_EOF:       return "EOF";
    case T_VERB:      return "verb";
    case T_VAR:       return "var";
    case T_CONST:     return "const";
    case T_ENDVERB:   return "endverb";
    case T_FUNC:      return "func";
    case T_ENDFUNC:   return "endfunc";
    case T_IF:        return "if";
    case T_ELSEIF:    return "elseif";
    case T_ELSE:      return "else";
    case T_ENDIF:     return "endif";
    case T_FOR:       return "for";
    case T_IN:        return "in";
    case T_WHILE:     return "while";
    case T_ENDFOR:    return "endfor";
    case T_ENDWHILE:  return "endwhile";
    case T_BREAK:     return "break";
    case T_CONTINUE:  return "continue";
    case T_RETURN:    return "return";
    case T_DEFER:     return "defer";
    case T_PANIC:     return "panic";
    case T_RECOVER:   return "recover";
    case T_ENDDEFER:      return "enddefer";
    case T_INTERFACE:     return "interface";
    case T_ENDINTERFACE:  return "endinterface";
    case T_IS:            return "is";
    case T_AS:            return "as";
    case T_SWITCH:        return "switch";
    case T_ENDSWITCH:     return "endswitch";
    case T_CASE:          return "case";
    case T_TRACE:         return "trace";
    case T_MODULE:        return "module";
    case T_IMPORT:        return "import";
    case T_EXPORT:        return "export";
    case T_EXTERN:        return "extern";
    case T_TRUE:      return "true";
    case T_FALSE:     return "false";
    case T_NIL:       return "nil";
    case T_TINT:      return "int";
    case T_TSTR:      return "str";
    case T_TOBJ:      return "obj";
    case T_TBOOL:     return "bool";
    case T_TERR:      return "err";
    case T_TLIST:     return "list";
    case T_TPROP:     return "prop";
    case T_TFLOAT:    return "float";
    case T_IDENT:     return "IDENT";
    case T_NUMBER:    return "NUMBER";
    case T_FLOAT_LIT: return "FLOAT";
    case T_STRING:    return "STRING";
    case T_OBJLIT:    return "OBJLIT";
    case T_ERRCODE:   return "ERRCODE";
    case T_PLUS:      return "+";
    case T_MINUS:     return "-";
    case T_STAR:      return "*";
    case T_SLASH:     return "/";
    case T_PERCENT:   return "%";
    case T_EQ:        return "==";
    case T_NE:        return "!=";
    case T_LT:        return "<";
    case T_LE:        return "<=";
    case T_GT:        return ">";
    case T_GE:        return ">=";
    case T_ANDAND:    return "&&";
    case T_OROR:      return "||";
    case T_BANG:      return "!";
    case T_ASSIGN:    return "=";
    case T_PLUSEQ:    return "+=";
    case T_MINUSEQ:   return "-=";
    case T_DOTDOT:    return "..";
    case T_ARROW:     return "->";
    case T_LPAREN:    return "(";
    case T_RPAREN:    return ")";
    case T_LBRACE:    return "{";
    case T_RBRACE:    return "}";
    case T_LBRACK:    return "[";
    case T_RBRACK:    return "]";
    case T_DOT:       return ".";
    case T_COLON:     return ":";
    case T_SEMI:      return ";";
    case T_COMMA:     return ",";
    case T_TRACE_COMMENT: return "///";
    default:          return "?";
    }
}
