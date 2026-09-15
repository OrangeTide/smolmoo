/* host_vm.c : MooScript host bridge for smolmoo VM
 * Copyright (c) 2026 Jon Mayo
 * SPDX-License-Identifier: 0BSD OR CC0-1.0 */

struct moo_str {
    int len;
    const char *data;
};

struct moo_list {
    int count;
    int elem[];
};

struct moo_prop {
    int tag;
    int val;
};

enum {
    MOO_T_INT   = 0,
    MOO_T_STR   = 1,
    MOO_T_OBJ   = 2,
    MOO_T_LIST  = 3,
    MOO_T_ERR   = 4,
    MOO_T_BOOL  = 5,
    MOO_T_FLOAT = 6,
};

extern void *__moo_arena_alloc(int size);
extern int sys_getprop(int obj, const char *name, char *buf, int bufsz);
extern int sys_setprop(int obj, const char *name, const char *val);
extern int sys_objfind(const char *name);
extern int write(int fd, const void *buf, int len);

static int
str_to_cstr(struct moo_str *s, char *buf, int bufsz)
{
    int n = s->len;

    if (n >= bufsz)
        n = bufsz - 1;
    for (int i = 0; i < n; i++)
        buf[i] = s->data[i];
    buf[n] = 0;
    return n;
}

static int
moo_str_eq_cstr(struct moo_str *s, const char *c)
{
    int i;

    for (i = 0; i < s->len && c[i]; i++) {
        if (s->data[i] != c[i])
            return 0;
    }
    return i == s->len && c[i] == 0;
}

static struct moo_str *
make_str(const char *s, int len)
{
    struct moo_str *r = __moo_arena_alloc(8);

    r->len = len;
    r->data = s;
    return r;
}

static int
int_to_str(int val, char *buf, int bufsz)
{
    int neg = 0;
    int pos = bufsz;

    if (val < 0) {
        neg = 1;
        val = -val;
    }
    buf[--pos] = 0;
    if (val == 0) {
        buf[--pos] = '0';
    } else {
        while (val > 0 && pos > 0) {
            buf[--pos] = '0' + (val % 10);
            val /= 10;
        }
    }
    if (neg && pos > 0)
        buf[--pos] = '-';
    return pos;
}

struct moo_prop *
__moo_prop_get(const char *obj, struct moo_str *prop)
{
    char name[64];
    char buf[256];
    struct moo_prop *p = __moo_arena_alloc(8);
    int id = (int)obj;
    int len;

    str_to_cstr(prop, name, 64);
    len = sys_getprop(id, name, buf, 256);
    if (len < 0) {
        p->tag = MOO_T_INT;
        p->val = 0;
        return p;
    }
    char *s = __moo_arena_alloc(len + 1);
    for (int i = 0; i <= len; i++)
        s[i] = buf[i];
    p->tag = MOO_T_STR;
    p->val = (int)make_str(s, len);
    return p;
}

void
__moo_prop_set(const char *obj, struct moo_str *prop, int val)
{
    char name[64];
    char vbuf[256];
    int id = (int)obj;
    struct moo_str *sv = (struct moo_str *)val;

    str_to_cstr(prop, name, 64);
    str_to_cstr(sv, vbuf, 256);
    sys_setprop(id, name, vbuf);
}

int
__moo_obj_valid(const char *obj)
{
    return obj != 0;
}

void
__moo_obj_move(const char *obj, const char *dest)
{
    char buf[16];
    int id = (int)obj;
    int did = (int)dest;
    int pos = int_to_str(did, buf, 16);

    sys_setprop(id, "location", buf + pos);
}

const char *
__moo_obj_create(const char *parent)
{
    return 0;
}

void
__moo_obj_recycle(const char *obj)
{
}

const char *
__moo_obj_location(const char *obj)
{
    char buf[16];
    int id = (int)obj;
    int len = sys_getprop(id, "location", buf, 16);
    int loc = 0;

    if (len <= 0)
        return 0;
    for (int i = 0; i < len && buf[i] >= '0' && buf[i] <= '9'; i++)
        loc = loc * 10 + (buf[i] - '0');
    return (const char *)loc;
}

struct moo_list *
__moo_obj_contents(const char *obj)
{
    struct moo_list *l = __moo_arena_alloc(4);

    l->count = 0;
    return l;
}

void
__moo_verb_call(const char *obj, struct moo_str *verb, int argc)
{
    if (moo_str_eq_cstr(verb, "tell") && argc >= 1) {
        int *extra = &argc + 1;
        struct moo_str *msg = (struct moo_str *)extra[0];

        if (msg) {
            write(1, msg->data, msg->len);
            write(1, "\n", 1);
        }
    }
}

int
__moo_obj_has_prop(const char *obj, struct moo_str *name)
{
    char nbuf[64];
    char buf[4];
    int id = (int)obj;

    str_to_cstr(name, nbuf, 64);
    return sys_getprop(id, nbuf, buf, 4) >= 0;
}

int
__moo_obj_has_verb(const char *obj, struct moo_str *name)
{
    return 0;
}
