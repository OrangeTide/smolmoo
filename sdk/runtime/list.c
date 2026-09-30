/* list.c : runtime list operations for MooScript (m68k target) */

struct moo_list {
    int count;
    int elem[];
};

extern void *__moo_arena_alloc(int size);

int
__moo_list_index(struct moo_list *l, int idx)
{
    return l->elem[idx - 1];
}

int
__moo_list_len(struct moo_list *l)
{
    return l->count;
}

struct moo_list *
__moo_list_append(struct moo_list *l, int val)
{
    int nc = l->count + 1;
    struct moo_list *r = __moo_arena_alloc(4 + nc * 4);

    r->count = nc;
    for (int i = 0; i < l->count; i++)
        r->elem[i] = l->elem[i];
    r->elem[l->count] = val;
    return r;
}

struct moo_list *
__moo_list_delete(struct moo_list *l, int idx)
{
    int nc = l->count - 1;
    struct moo_list *r = __moo_arena_alloc(4 + nc * 4);
    int j = 0;

    r->count = nc;
    for (int i = 0; i < l->count; i++) {
        if (i != idx - 1)
            r->elem[j++] = l->elem[i];
    }
    return r;
}

struct moo_list *
__moo_list_set(struct moo_list *l, int idx, int val)
{
    struct moo_list *r = __moo_arena_alloc(4 + l->count * 4);

    r->count = l->count;
    for (int i = 0; i < l->count; i++)
        r->elem[i] = l->elem[i];
    r->elem[idx - 1] = val;
    return r;
}

struct moo_list *
__moo_list_slice(struct moo_list *l, int lo, int hi)
{
    int nc = hi - lo + 1;
    struct moo_list *r = __moo_arena_alloc(4 + nc * 4);

    r->count = nc;
    for (int i = 0; i < nc; i++)
        r->elem[i] = l->elem[lo - 1 + i];
    return r;
}

int
__moo_list_contains(struct moo_list *l, int val)
{
    for (int i = 0; i < l->count; i++) {
        if (l->elem[i] == val)
            return 1;
    }
    return 0;
}
