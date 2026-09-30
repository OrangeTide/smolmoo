/* str.c : flat string operations for MooScript (m68k target) */

struct moo_str {
    int len;
    const char *data;
};

extern void *__moo_arena_alloc(int size);

struct moo_str *
__moo_str_concat(struct moo_str *a, struct moo_str *b)
{
    struct moo_str *r;
    char *buf;
    int len;

    if (!a) return b;
    if (!b) return a;
    len = a->len + b->len;
    buf = __moo_arena_alloc(len);
    for (int i = 0; i < a->len; i++)
        buf[i] = a->data[i];
    for (int i = 0; i < b->len; i++)
        buf[a->len + i] = b->data[i];
    r = __moo_arena_alloc(8);
    r->len = len;
    r->data = buf;
    return r;
}

int
__moo_str_eq(struct moo_str *a, struct moo_str *b)
{
    if (a == b)
        return 1;
    if (!a || !b)
        return 0;
    if (a->len != b->len)
        return 0;
    for (int i = 0; i < a->len; i++) {
        if (a->data[i] != b->data[i])
            return 0;
    }
    return 1;
}

int
__moo_str_len(struct moo_str *s)
{
    if (!s)
        return 0;
    return s->len;
}

int
__moo_toint(struct moo_str *s)
{
    int val = 0, neg = 0, i = 0;

    if (!s || s->len == 0)
        return 0;
    if (s->data[0] == '-') {
        neg = 1;
        i = 1;
    } else if (s->data[0] == '+') {
        i = 1;
    }
    for (; i < s->len; i++) {
        char c = s->data[i];
        if (c < '0' || c > '9')
            break;
        val = val * 10 + (c - '0');
    }
    return neg ? -val : val;
}

int
__moo_str_index(struct moo_str *haystack, struct moo_str *needle)
{
    if (!haystack || !needle)
        return 0;
    if (needle->len == 0)
        return 1;
    if (needle->len > haystack->len)
        return 0;
    for (int i = 0; i <= haystack->len - needle->len; i++) {
        int match = 1;
        for (int j = 0; j < needle->len; j++) {
            if (haystack->data[i + j] != needle->data[j]) {
                match = 0;
                break;
            }
        }
        if (match)
            return i + 1;
    }
    return 0;
}

struct moo_str *
__moo_str_substr(struct moo_str *s, int start, int end)
{
    struct moo_str *r;
    int rlen;

    if (!s)
        return 0;
    if (start < 1)
        start = 1;
    if (end > s->len)
        end = s->len;
    if (start > end)
        return 0;
    rlen = end - start + 1;
    r = __moo_arena_alloc(8);
    r->len = rlen;
    r->data = s->data + start - 1;
    return r;
}

struct moo_str *
__moo_str_strsub(struct moo_str *s, struct moo_str *old,
                 struct moo_str *new)
{
    int pos, rlen;
    char *buf;
    struct moo_str *r;

    if (!s)
        return 0;
    if (!old || old->len == 0)
        return s;
    pos = __moo_str_index(s, old);
    if (pos == 0)
        return s;
    pos--;
    rlen = s->len - old->len + (new ? new->len : 0);
    buf = __moo_arena_alloc(rlen);
    for (int i = 0; i < pos; i++)
        buf[i] = s->data[i];
    if (new) {
        for (int i = 0; i < new->len; i++)
            buf[pos + i] = new->data[i];
    }
    for (int i = pos + old->len; i < s->len; i++)
        buf[i - old->len + (new ? new->len : 0)] = s->data[i];
    r = __moo_arena_alloc(8);
    r->len = rlen;
    r->data = buf;
    return r;
}

struct moo_str *
__moo_tostr(int val)
{
    char *buf = __moo_arena_alloc(12);
    struct moo_str *r = __moo_arena_alloc(8);
    int neg = 0;
    int pos = 12;

    if (val < 0) {
        neg = 1;
        val = -val;
    }
    buf[--pos] = '\0';
    if (val == 0) {
        buf[--pos] = '0';
    } else {
        while (val > 0) {
            buf[--pos] = '0' + (val % 10);
            val /= 10;
        }
    }
    if (neg)
        buf[--pos] = '-';
    r->len = 11 - pos;
    r->data = buf + pos;
    return r;
}

struct moo_str *
__moo_tostr_err(int val)
{
    static const struct {
        int code;
        const char *name;
        int len;
    } errors[] = {
        { 0,  "E_NONE",    6 },
        { 1,  "E_TYPE",    6 },
        { 2,  "E_INVARG",  8 },
        { 3,  "E_RANGE",   7 },
        { 4,  "E_PERM",    6 },
        { 5,  "E_PROPNF",  8 },
        { 6,  "E_VERBNF",  8 },
        { 7,  "E_ARGS",    6 },
        { 8,  "E_RECMOVE", 9 },
        { 9,  "E_MAXREC",  8 },
        { 10, "E_INVOBJ",  8 },
    };

    for (int i = 0; i < (int)(sizeof(errors) / sizeof(errors[0])); i++) {
        if (errors[i].code == val) {
            struct moo_str *r = __moo_arena_alloc(8);
            r->len = errors[i].len;
            r->data = errors[i].name;
            return r;
        }
    }
    return __moo_tostr(val);
}

struct moo_str *
__moo_tostr_float(double *dp)
{
    char *buf = __moo_arena_alloc(32);
    struct moo_str *r = __moo_arena_alloc(8);
    double v = *dp;
    int pos = 0;
    int i;

    if (v != v) {
        buf[0] = 'N'; buf[1] = 'a'; buf[2] = 'N';
        r->len = 3;
        r->data = buf;
        return r;
    }

    if (v < 0.0) {
        buf[pos++] = '-';
        v = -v;
    }

    if (v > 1.0e18 || (v != 0.0 && v < 1.0e-4)) {
        /* infinity or extreme values -- format as X.XXXXXXeYYY */
        int exp = 0;
        if (v == v + v && v != 0.0) {
            buf[pos++] = 'i'; buf[pos++] = 'n'; buf[pos++] = 'f';
            r->len = pos;
            r->data = buf;
            return r;
        }
        if (v >= 10.0) {
            while (v >= 10.0) { v /= 10.0; exp++; }
        } else if (v > 0.0) {
            while (v < 1.0) { v *= 10.0; exp--; }
        }
        int ipart = (int)v;
        buf[pos++] = '0' + ipart;
        buf[pos++] = '.';
        v = (v - ipart) * 1000000.0;
        int frac = (int)(v + 0.5);
        for (i = 5; i >= 0; i--) {
            buf[pos + i] = '0' + (frac % 10);
            frac /= 10;
        }
        pos += 6;
        while (pos > 2 && buf[pos - 1] == '0')
            pos--;
        if (buf[pos - 1] == '.')
            pos--;
        buf[pos++] = 'e';
        if (exp < 0) {
            buf[pos++] = '-';
            exp = -exp;
        }
        if (exp >= 100) {
            buf[pos++] = '0' + (exp / 100);
            exp %= 100;
        }
        if (exp >= 10) {
            buf[pos++] = '0' + (exp / 10);
            exp %= 10;
        }
        buf[pos++] = '0' + exp;
        r->len = pos;
        r->data = buf;
        return r;
    }

    /* normal range: print integer part + up to 6 decimals */
    long ipart = (long)v;
    double fpart = v - (double)ipart;

    if (ipart == 0) {
        buf[pos++] = '0';
    } else {
        char tmp[20];
        int ti = 0;
        long n = ipart;
        while (n > 0) {
            tmp[ti++] = '0' + (int)(n % 10);
            n /= 10;
        }
        for (i = ti - 1; i >= 0; i--)
            buf[pos++] = tmp[i];
    }

    buf[pos++] = '.';
    int frac = (int)(fpart * 1000000.0 + 0.5);
    for (i = 5; i >= 0; i--) {
        buf[pos + i] = '0' + (frac % 10);
        frac /= 10;
    }
    pos += 6;
    while (pos > 1 && buf[pos - 1] == '0')
        pos--;
    if (buf[pos - 1] == '.')
        pos++;

    r->len = pos;
    r->data = buf;
    return r;
}

void
__moo_tofloat(struct moo_str *s, double *out)
{
    double val = 0.0;
    double frac = 0.0;
    double div = 1.0;
    int neg = 0;
    int i = 0;

    *out = 0.0;
    if (!s || s->len == 0)
        return;

    while (i < s->len && (s->data[i] == ' ' || s->data[i] == '\t'))
        i++;

    if (i < s->len && s->data[i] == '-') {
        neg = 1;
        i++;
    } else if (i < s->len && s->data[i] == '+') {
        i++;
    }

    while (i < s->len && s->data[i] >= '0' && s->data[i] <= '9') {
        val = val * 10.0 + (s->data[i] - '0');
        i++;
    }

    if (i < s->len && s->data[i] == '.') {
        i++;
        while (i < s->len && s->data[i] >= '0' && s->data[i] <= '9') {
            frac = frac * 10.0 + (s->data[i] - '0');
            div *= 10.0;
            i++;
        }
        val += frac / div;
    }

    if (i < s->len && (s->data[i] == 'e' || s->data[i] == 'E')) {
        int eneg = 0;
        int exp = 0;
        i++;
        if (i < s->len && s->data[i] == '-') {
            eneg = 1;
            i++;
        } else if (i < s->len && s->data[i] == '+') {
            i++;
        }
        while (i < s->len && s->data[i] >= '0' && s->data[i] <= '9') {
            exp = exp * 10 + (s->data[i] - '0');
            i++;
        }
        double mul = 1.0;
        for (int j = 0; j < exp; j++)
            mul *= 10.0;
        if (eneg)
            val /= mul;
        else
            val *= mul;
    }

    *out = neg ? -val : val;
}
