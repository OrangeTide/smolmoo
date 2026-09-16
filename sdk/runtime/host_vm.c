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
extern int sys_move(int obj, int dest);
extern int sys_next(int container, int after);
extern int sys_create(int parent);
extern int sys_recycle(int obj);
extern int sys_call(int target, const char *verb, const char *argstr);
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

/* Properties are stored as strings in the host. Infer the MooScript value
 * type from the text form: `#N` or `&N` is an object reference, a run of
 * digits (with an optional leading `-`) is an integer, anything else is a
 * string. `*out` receives the integer or object id for the numeric tags. */
static int
classify(const char *s, int len, int *out)
{
    int i;
    int v = 0;

    if (len > 0 && (s[0] == '#' || s[0] == '&')) {
        for (i = 1; i < len && s[i] >= '0' && s[i] <= '9'; i++)
            v = v * 10 + (s[i] - '0');
        *out = v;
        return MOO_T_OBJ;
    }
    i = 0;
    if (i < len && s[i] == '-')
        i++;
    if (i < len) {
        int all = 1;
        for (int j = i; j < len; j++) {
            if (s[j] < '0' || s[j] > '9') {
                all = 0;
                break;
            }
        }
        if (all) {
            for (; i < len; i++)
                v = v * 10 + (s[i] - '0');
            *out = (s[0] == '-') ? -v : v;
            return MOO_T_INT;
        }
    }
    return MOO_T_STR;
}

struct moo_prop *
__moo_prop_get(const char *obj, struct moo_str *prop)
{
    char name[64];
    char buf[256];
    struct moo_prop *p = __moo_arena_alloc(8);
    int id = (int)obj;
    int len;
    int tag;
    int out = 0;

    str_to_cstr(prop, name, 64);
    len = sys_getprop(id, name, buf, 256);
    if (len < 0) {
        /* missing property reads as nil (0) */
        p->tag = MOO_T_INT;
        p->val = 0;
        return p;
    }
    tag = classify(buf, len, &out);
    if (tag == MOO_T_STR) {
        char *s = __moo_arena_alloc(len + 1);
        for (int i = 0; i < len; i++)
            s[i] = buf[i];
        s[len] = 0;
        p->tag = MOO_T_STR;
        p->val = (int)make_str(s, len);
    } else {
        p->tag = tag;
        p->val = out;
    }
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
    /* sys_move sets the object-valued `location`; the string property setter
     * cannot, since it only writes plain string values. */
    sys_move((int)obj, (int)dest);
}

/* sys_create allocates a fresh persistent object under `parent`, owned by the
 * invoking player, and returns its id (negative on error -> nil handle).
 * sys_recycle destroys an object the caller owns (or any, for a wizard). */
const char *
__moo_obj_create(const char *parent)
{
    int id = sys_create((int)parent);

    return id < 0 ? 0 : (const char *)id;
}

void
__moo_obj_recycle(const char *obj)
{
    sys_recycle((int)obj);
}

const char *
__moo_obj_location(const char *obj)
{
    char buf[16];
    int id = (int)obj;
    int len = sys_getprop(id, "location", buf, 16);
    int loc = 0;
    int i = 0;

    if (len <= 0)
        return 0;
    if (buf[0] == '#' || buf[0] == '&')     /* skip the objref sigil */
        i = 1;
    for (; i < len && buf[i] >= '0' && buf[i] <= '9'; i++)
        loc = loc * 10 + (buf[i] - '0');
    return (const char *)loc;
}

struct moo_list *
__moo_obj_contents(const char *obj)
{
    int id = (int)obj;
    struct moo_list *l;
    int count = 0;
    int after = 0;
    int child;

    /* sys_next walks direct contents in ascending id order, returning 0 at the
     * end. Count first, then fill: the arena has no realloc. */
    while ((child = sys_next(id, after)) != 0) {
        count++;
        after = child;
    }
    l = __moo_arena_alloc(4 + count * 4);
    l->count = count;
    after = 0;
    for (int i = 0; i < count; i++) {
        child = sys_next(id, after);
        l->elem[i] = child;
        after = child;
    }
    return l;
}

/* Verb dispatch (`obj:verb(args)`) lands here. The MooScript backend passes
 * the call on the stack, so the variadic arguments follow `argc` in memory
 * (`&argc + 1` is the first). `tell` is a runtime built-in: it writes to fd 1,
 * the invoking player's stream, so `player:tell(...)` reaches that player. Any
 * other verb name is resolved and run on the target object through sys_call,
 * which binds `this` to the target and runs it fire-and-forget. The call's
 * first argument, if present, is passed as the verb's argument string, so it
 * must be a string; further or non-string arguments are not marshalled. */
void
__moo_verb_call(const char *obj, struct moo_str *verb, int argc)
{
    int *extra = &argc + 1;

    if (moo_str_eq_cstr(verb, "tell")) {
        struct moo_str *msg = argc >= 1 ? (struct moo_str *)extra[0] : 0;

        if (msg) {
            write(1, msg->data, msg->len);
            write(1, "\n", 1);
        }
        return;
    }
    {
        char vbuf[64];
        char abuf[256];

        str_to_cstr(verb, vbuf, sizeof(vbuf));
        abuf[0] = 0;
        if (argc >= 1) {
            struct moo_str *a0 = (struct moo_str *)extra[0];
            if (a0)
                str_to_cstr(a0, abuf, sizeof(abuf));
        }
        sys_call((int)obj, vbuf, abuf);
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

/* Interface checks that ask whether an object defines a verb need a verb
 * resolution primitive, which the ABI does not expose (verbs are matched by
 * the host's dispatcher, not queried from a running verb). Reports false
 * until such a syscall exists. */
int
__moo_obj_has_verb(const char *obj, struct moo_str *name)
{
    (void)obj;
    (void)name;
    return 0;
}
