/* toy_host.c : toy in-memory object database for MooScript testing.
 *
 * Provides a static set of objects with string properties, a working
 * tell verb (writes to stdout), contents(), valid(), and property
 * access.  All other host operations are stubs.
 *
 * Objects:
 *   rooms/lobby    name="The Lobby"    description="A grand entry hall."
 *   players/alice  name="Alice"        location=rooms/lobby
 *   items/sword    name="Rusty Sword"  description="A battered old blade."
 *   items/potion   name="Red Potion"   description="A bubbling red liquid."
 */

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
    MOO_T_INT  = 0,
    MOO_T_STR  = 1,
    MOO_T_OBJ  = 2,
    MOO_T_LIST = 3,
    MOO_T_ERR  = 4,
    MOO_T_BOOL  = 5,
    MOO_T_FLOAT = 6,
};

extern void *__moo_arena_alloc(int size);
extern int write(int fd, const char *buf, int n);

/****************************************************************
 * Helpers
 ****************************************************************/

static int
str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int
str_len(const char *s)
{
    int n = 0;

    while (s[n])
        n++;
    return n;
}

static int
moo_str_eq_cstr(struct moo_str *s, const char *cstr)
{
    int clen = str_len(cstr);

    if (s->len != clen)
        return 0;
    for (int i = 0; i < clen; i++) {
        if (s->data[i] != cstr[i])
            return 0;
    }
    return 1;
}

static struct moo_str *
make_str(const char *s, int len)
{
    struct moo_str *r = __moo_arena_alloc(8);

    r->len = len;
    r->data = s;
    return r;
}

/****************************************************************
 * Static object database
 ****************************************************************/

struct toy_prop {
    const char *name;
    const char *sval;
    int slen;
};

#define MAX_PROPS 4

struct toy_obj {
    const char *path;
    struct toy_prop props[MAX_PROPS];
    int nprop;
};

static struct toy_obj objects[] = {
    {
        "rooms/lobby",
        {
            { "name", "The Lobby", 9 },
            { "description", "A grand entry hall.", 19 },
        },
        2,
    },
    {
        "players/alice",
        {
            { "name", "Alice", 5 },
            { "location", "rooms/lobby", 11 },
        },
        2,
    },
    {
        "items/sword",
        {
            { "name", "Rusty Sword", 11 },
            { "description", "A battered old blade.", 21 },
            { "location", "rooms/lobby", 11 },
        },
        3,
    },
    {
        "items/potion",
        {
            { "name", "Red Potion", 10 },
            { "description", "A bubbling red liquid.", 22 },
            { "location", "rooms/lobby", 11 },
        },
        3,
    },
};

#define NOBJ ((int)(sizeof(objects) / sizeof(objects[0])))

static struct toy_obj *
find_obj(const char *path)
{
    for (int i = 0; i < NOBJ; i++) {
        if (str_eq(objects[i].path, path))
            return &objects[i];
    }
    return 0;
}

static const char *
find_prop(struct toy_obj *o, struct moo_str *name)
{
    for (int i = 0; i < o->nprop; i++) {
        if (moo_str_eq_cstr(name, o->props[i].name))
            return o->props[i].sval;
    }
    return 0;
}


/****************************************************************
 * Host glue functions
 ****************************************************************/

struct moo_prop *
__moo_prop_get(const char *obj, struct moo_str *prop)
{
    struct toy_obj *o = find_obj(obj);
    struct moo_prop *p = __moo_arena_alloc(8);

    if (!o) {
        p->tag = MOO_T_INT;
        p->val = 0;
        return p;
    }
    const char *val = find_prop(o, prop);
    if (!val) {
        p->tag = MOO_T_INT;
        p->val = 0;
        return p;
    }
    p->tag = MOO_T_STR;
    p->val = (int)make_str(val, str_len(val));
    return p;
}

void
__moo_prop_set(const char *obj, struct moo_str *prop, int val)
{
}

int
__moo_obj_valid(const char *obj)
{
    return obj != 0 && find_obj(obj) != 0;
}

void
__moo_obj_move(const char *obj, const char *dest)
{
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
    struct toy_obj *o = find_obj(obj);

    if (!o)
        return 0;
    for (int i = 0; i < o->nprop; i++) {
        if (str_eq(o->props[i].name, "location"))
            return o->props[i].sval;
    }
    return 0;
}

struct moo_list *
__moo_obj_contents(const char *obj)
{
    int found[16];
    int nfound = 0;

    for (int i = 0; i < NOBJ; i++) {
        for (int j = 0; j < objects[i].nprop; j++) {
            if (str_eq(objects[i].props[j].name, "location") &&
                str_eq(objects[i].props[j].sval, obj)) {
                if (nfound < 16)
                    found[nfound++] = (int)objects[i].path;
                break;
            }
        }
    }

    struct moo_list *l = __moo_arena_alloc(4 + nfound * 4);

    l->count = nfound;
    for (int i = 0; i < nfound; i++)
        l->elem[i] = found[i];
    return l;
}

void
__moo_verb_call(const char *obj, struct moo_str *verb, int argc, ...)
{
    if (moo_str_eq_cstr(verb, "tell") && argc >= 1) {
        __builtin_va_list ap;
        __builtin_va_start(ap, argc);
        struct moo_str *msg = __builtin_va_arg(ap, struct moo_str *);
        __builtin_va_end(ap);
        if (msg)
            write(1, msg->data, msg->len);
        write(1, "\n", 1);
    }
}

int
__moo_obj_has_prop(const char *obj, struct moo_str *name)
{
    struct toy_obj *o = find_obj(obj);

    if (!o)
        return 0;
    return find_prop(o, name) != 0;
}

int
__moo_obj_has_verb(const char *obj, struct moo_str *name)
{
    if (moo_str_eq_cstr(name, "tell"))
        return 1;
    return 0;
}
