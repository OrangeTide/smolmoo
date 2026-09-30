/* host_stub.c : stub host operations for standalone MooScript testing */

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

extern void *__moo_arena_alloc(int size);

struct moo_prop *
__moo_prop_get(const char *obj, struct moo_str *prop)
{
    struct moo_prop *p = __moo_arena_alloc(8);

    p->tag = 0;
    p->val = 0;
    return p;
}

void
__moo_prop_set(const char *obj, struct moo_str *prop, int val)
{
}

int
__moo_obj_valid(const char *obj)
{
    return obj != 0;
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
    return 0;
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
}

int
__moo_obj_has_prop(const char *obj, struct moo_str *name)
{
    return 0;
}

int
__moo_obj_has_verb(const char *obj, struct moo_str *name)
{
    return 0;
}
