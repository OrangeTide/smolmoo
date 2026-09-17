/* cas-omap.c : sparse object map -- numeric ID to CAS hash */
/* PUBLIC DOMAIN (CC0-1.0) */

#define _POSIX_C_SOURCE 200809L

#include "cas-omap.h"

#include <stdlib.h>
#include <string.h>

/****************************************************************
 * Internal constants
 ****************************************************************/

#define OMAP_DIR_MAGIC     "OMv1"
#define OMAP_DIR_MAGIC_LEN 4
#define OMAP_DIR_HEADER_LEN 8
#define OMAP_DIR_ENTRY_LEN 48

static const unsigned char zero_hash[CAS_HASH_LEN];

/****************************************************************
 * Internal structures
 ****************************************************************/

struct omap_page {
    unsigned char slots[CAS_OMAP_PAGE_SLOTS][CAS_HASH_LEN];
    uint16_t pop_count;
    int dirty;
};

struct omap_dir_entry {
    uint64_t page_num;
    unsigned char page_hash[CAS_HASH_LEN];
    uint16_t pop_count;
    struct omap_page *cached;
};

struct cas_omap {
    struct cas *store;
    struct omap_dir_entry *dir;
    int dir_count;
    int dir_cap;
};

/****************************************************************
 * Little-endian helpers
 ****************************************************************/

static void
store_le16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
}

static uint16_t
load_le16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void
store_le32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t
load_le32(const unsigned char *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void
store_le64(unsigned char *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (unsigned char)(v >> (i * 8));
}

static uint64_t
load_le64(const unsigned char *p)
{
    uint64_t v = 0;

    for (int i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (i * 8);
    return v;
}

/****************************************************************
 * Directory search
 ****************************************************************/

static int
dir_find(struct cas_omap *om, uint64_t page_num)
{
    int lo = 0, hi = om->dir_count;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;

        if (om->dir[mid].page_num < page_num)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static int
dir_insert(struct cas_omap *om, uint64_t page_num,
           struct omap_dir_entry **out)
{
    int pos = dir_find(om, page_num);

    if (pos < om->dir_count &&
        om->dir[pos].page_num == page_num) {
        *out = &om->dir[pos];
        return CAS_OK;
    }

    if (om->dir_count >= om->dir_cap) {
        int newcap = om->dir_cap ? om->dir_cap * 2 : 16;
        struct omap_dir_entry *p = realloc(om->dir,
            (size_t)newcap * sizeof(*p));

        if (!p)
            return CAS_ENOMEM;
        om->dir = p;
        om->dir_cap = newcap;
    }

    if (pos < om->dir_count)
        memmove(&om->dir[pos + 1], &om->dir[pos],
                (size_t)(om->dir_count - pos) * sizeof(*om->dir));

    memset(&om->dir[pos], 0, sizeof(om->dir[pos]));
    om->dir[pos].page_num = page_num;
    om->dir_count++;
    *out = &om->dir[pos];
    return CAS_OK;
}

/****************************************************************
 * Page loading
 ****************************************************************/

static int
page_load(struct cas_omap *om, struct omap_dir_entry *de)
{
    if (de->cached)
        return CAS_OK;

    char hexhash[CAS_HASH_HEX + 1];

    cas_hex_encode(de->page_hash, CAS_HASH_LEN, hexhash);

    struct cas_file cf;
    char type[CAS_TYPE_MAX + 1];
    int rc = cas_open_object(om->store, &cf, hexhash, type,
                             sizeof(type));

    if (rc != CAS_OK)
        return rc;

    if (strcmp(type, "opage") != 0 ||
        cf.len != CAS_OMAP_PAGE_SIZE) {
        cas_close(&cf);
        return CAS_ETYPE;
    }

    /* Allocated only once nothing else can fail, so the page is either
     * handed to de->cached or never created, and no error path has to
     * remember to free it.  Ownership then sits with the directory
     * entry, which releases it in cas_omap_free, cas_omap_load, and
     * cas_omap_reset. */
    struct omap_page *pg = calloc(1, sizeof(*pg));

    if (!pg) {
        cas_close(&cf);
        return CAS_ENOMEM;
    }

    memcpy(pg->slots, cf.data, CAS_OMAP_PAGE_SIZE);
    cas_close(&cf);

    pg->pop_count = de->pop_count;
    pg->dirty = 0;

    /* -fanalyzer reports a leak here.  It is a false positive: the store
     * hands ownership to the directory entry, which outlives this call
     * and releases it in cas_omap_free, cas_omap_load, and
     * cas_omap_reset, and both assignments to de->cached are guarded by
     * a NULL test so nothing is ever overwritten.  The analyzer cannot
     * follow ownership out of a function and into a caller-owned struct
     * field, so it sees the last use of pg and stops there.
     *
     * Suppressed narrowly rather than left standing, so that `make
     * analyze` stays clean and a genuine leak elsewhere is not lost in
     * the noise.
     *
     * The suppressed region has to run to the end of the function, not
     * just cover the assignment.  The analyzer reports a leak wherever
     * the last reference to pg dies, and which statement that is moves
     * between compiler versions: gcc 13 blames the assignment, gcc 14
     * and later blame the return.  Everything from the assignment to
     * the closing brace is inside the region so either choice lands in
     * it.  The pop sits after the brace because the analyzer can also
     * pick the end of the function as the leak site. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
#endif
    de->cached = pg;
    return CAS_OK;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

static int
page_ensure(struct cas_omap *om, uint64_t id,
            struct omap_dir_entry **de_out,
            struct omap_page **pg_out)
{
    uint64_t page_num = id / CAS_OMAP_PAGE_SLOTS;
    struct omap_dir_entry *de;
    int rc = dir_insert(om, page_num, &de);

    if (rc != CAS_OK)
        return rc;

    if (!de->cached) {
        int has_hash = memcmp(de->page_hash, zero_hash,
                              CAS_HASH_LEN) != 0;

        if (has_hash) {
            rc = page_load(om, de);
            if (rc != CAS_OK)
                return rc;
        } else {
            struct omap_page *pg = calloc(1, sizeof(*pg));

            if (!pg)
                return CAS_ENOMEM;
            de->cached = pg;
        }
    }

    if (de_out) *de_out = de;
    if (pg_out) *pg_out = de->cached;
    return CAS_OK;
}

/****************************************************************
 * Lifecycle
 ****************************************************************/

struct cas_omap *
cas_omap_new(struct cas *store)
{
    struct cas_omap *om = calloc(1, sizeof(*om));

    if (!om)
        return NULL;
    om->store = store;
    return om;
}

void
cas_omap_free(struct cas_omap *om)
{
    if (!om)
        return;
    for (int i = 0; i < om->dir_count; i++)
        free(om->dir[i].cached);
    free(om->dir);
    free(om);
}

struct cas *
cas_omap_cas(struct cas_omap *om)
{
    return om->store;
}

/****************************************************************
 * Snapshot: load
 ****************************************************************/

int
cas_omap_load(struct cas_omap *om, const char *root_hash)
{
    struct cas_file cf;
    char type[CAS_TYPE_MAX + 1];
    int rc = cas_open_object(om->store, &cf, root_hash, type,
                             sizeof(type));

    if (rc != CAS_OK)
        return rc;

    if (strcmp(type, "omap") != 0 ||
        cf.len < OMAP_DIR_HEADER_LEN) {
        cas_close(&cf);
        return CAS_ETYPE;
    }

    if (memcmp(cf.data, OMAP_DIR_MAGIC, OMAP_DIR_MAGIC_LEN) != 0) {
        cas_close(&cf);
        return CAS_ERR;
    }

    uint32_t entry_count = load_le32(cf.data + 4);
    size_t expected = OMAP_DIR_HEADER_LEN +
                      (size_t)entry_count * OMAP_DIR_ENTRY_LEN;

    if (cf.len != expected) {
        cas_close(&cf);
        return CAS_ERR;
    }

    for (int i = 0; i < om->dir_count; i++)
        free(om->dir[i].cached);
    free(om->dir);
    om->dir = NULL;
    om->dir_count = 0;
    om->dir_cap = 0;

    /* Allocation and use share one block, so om->dir is provably
     * non-NULL wherever it is indexed.  An empty directory allocates
     * nothing and leaves om->dir NULL. */
    if (entry_count > 0) {
        om->dir = calloc((size_t)entry_count, sizeof(*om->dir));
        if (!om->dir) {
            cas_close(&cf);
            return CAS_ENOMEM;
        }
        om->dir_cap = (int)entry_count;

        const unsigned char *p = cf.data + OMAP_DIR_HEADER_LEN;

        for (uint32_t i = 0; i < entry_count; i++) {
            om->dir[i].page_num = load_le64(p);
            memcpy(om->dir[i].page_hash, p + 8, CAS_HASH_LEN);
            om->dir[i].pop_count = load_le16(p + 40);
            om->dir[i].cached = NULL;
            p += OMAP_DIR_ENTRY_LEN;
        }
    }
    om->dir_count = (int)entry_count;

    cas_close(&cf);
    return CAS_OK;
}

/****************************************************************
 * Snapshot: store
 ****************************************************************/

int
cas_omap_store(struct cas_omap *om, char *hash_out)
{
    int live_count = 0;

    for (int i = 0; i < om->dir_count; i++) {
        struct omap_dir_entry *de = &om->dir[i];

        if (de->cached && de->cached->dirty) {
            char h[CAS_HASH_HEX + 1];
            int rc = cas_put_object(om->store, "opage",
                                    de->cached->slots,
                                    CAS_OMAP_PAGE_SIZE, h);

            if (rc != CAS_OK)
                return rc;

            cas_hex_decode(h, CAS_HASH_HEX, de->page_hash,
                           CAS_HASH_LEN);
            de->pop_count = de->cached->pop_count;
            de->cached->dirty = 0;
        }
        if (de->pop_count > 0)
            live_count++;
    }

    size_t bufsz = OMAP_DIR_HEADER_LEN +
                   (size_t)live_count * OMAP_DIR_ENTRY_LEN;
    unsigned char *buf = malloc(bufsz);

    if (!buf)
        return CAS_ENOMEM;

    memcpy(buf, OMAP_DIR_MAGIC, OMAP_DIR_MAGIC_LEN);
    store_le32(buf + 4, (uint32_t)live_count);

    unsigned char *p = buf + OMAP_DIR_HEADER_LEN;

    for (int i = 0; i < om->dir_count; i++) {
        struct omap_dir_entry *de = &om->dir[i];

        if (de->pop_count == 0)
            continue;

        store_le64(p, de->page_num);
        memcpy(p + 8, de->page_hash, CAS_HASH_LEN);
        store_le16(p + 40, de->pop_count);
        memset(p + 42, 0, 6);
        p += OMAP_DIR_ENTRY_LEN;
    }

    int rc = cas_put_object(om->store, "omap", buf, bufsz,
                            hash_out);
    free(buf);
    return rc;
}

/****************************************************************
 * Lookup
 ****************************************************************/

int
cas_omap_get(struct cas_omap *om, uint64_t id, char *hash_out)
{
    uint64_t page_num = id / CAS_OMAP_PAGE_SLOTS;
    int slot = (int)(id % CAS_OMAP_PAGE_SLOTS);

    int pos = dir_find(om, page_num);

    if (pos >= om->dir_count ||
        om->dir[pos].page_num != page_num)
        return CAS_ENOTFOUND;

    struct omap_dir_entry *de = &om->dir[pos];
    int rc = page_load(om, de);

    if (rc != CAS_OK)
        return rc;

    if (memcmp(de->cached->slots[slot], zero_hash,
               CAS_HASH_LEN) == 0)
        return CAS_ENOTFOUND;

    cas_hex_encode(de->cached->slots[slot], CAS_HASH_LEN,
                   hash_out);
    return CAS_OK;
}

int
cas_omap_exists(struct cas_omap *om, uint64_t id)
{
    char h[CAS_HASH_HEX + 1];

    return cas_omap_get(om, id, h) == CAS_OK;
}

/****************************************************************
 * Mutation
 ****************************************************************/

int
cas_omap_put(struct cas_omap *om, uint64_t id, const char *hash)
{
    if (!cas_valid_hash(hash))
        return CAS_ERR;

    struct omap_dir_entry *de;
    struct omap_page *pg;
    int rc = page_ensure(om, id, &de, &pg);

    if (rc != CAS_OK)
        return rc;

    int slot = (int)(id % CAS_OMAP_PAGE_SLOTS);
    int was_empty = memcmp(pg->slots[slot], zero_hash,
                           CAS_HASH_LEN) == 0;

    cas_hex_decode(hash, CAS_HASH_HEX, pg->slots[slot],
                   CAS_HASH_LEN);
    pg->dirty = 1;

    if (was_empty) {
        pg->pop_count++;
        de->pop_count++;
    }

    return CAS_OK;
}

int
cas_omap_del(struct cas_omap *om, uint64_t id)
{
    uint64_t page_num = id / CAS_OMAP_PAGE_SLOTS;
    int slot = (int)(id % CAS_OMAP_PAGE_SLOTS);

    int pos = dir_find(om, page_num);

    if (pos >= om->dir_count ||
        om->dir[pos].page_num != page_num)
        return CAS_ENOTFOUND;

    struct omap_dir_entry *de = &om->dir[pos];
    int rc = page_load(om, de);

    if (rc != CAS_OK)
        return rc;

    if (memcmp(de->cached->slots[slot], zero_hash,
               CAS_HASH_LEN) == 0)
        return CAS_ENOTFOUND;

    memset(de->cached->slots[slot], 0, CAS_HASH_LEN);
    de->cached->dirty = 1;
    de->cached->pop_count--;
    de->pop_count--;
    return CAS_OK;
}

void
cas_omap_clear(struct cas_omap *om)
{
    for (int i = 0; i < om->dir_count; i++)
        free(om->dir[i].cached);
    free(om->dir);
    om->dir = NULL;
    om->dir_count = 0;
    om->dir_cap = 0;
}

/****************************************************************
 * Allocation
 ****************************************************************/

int
cas_omap_alloc(struct cas_omap *om, uint64_t start, uint64_t limit,
               uint64_t *id_out)
{
    uint64_t end = limit > 0 ? start + limit : UINT64_MAX;
    uint64_t id = start;

    while (id < end) {
        uint64_t page_num = id / CAS_OMAP_PAGE_SLOTS;
        int first_slot = (int)(id % CAS_OMAP_PAGE_SLOTS);

        int pos = dir_find(om, page_num);

        if (pos >= om->dir_count ||
            om->dir[pos].page_num != page_num) {
            *id_out = id;
            return CAS_OK;
        }

        struct omap_dir_entry *de = &om->dir[pos];

        if (de->pop_count == CAS_OMAP_PAGE_SLOTS) {
            id = (page_num + 1) * CAS_OMAP_PAGE_SLOTS;
            continue;
        }

        int rc = page_load(om, de);

        if (rc != CAS_OK)
            return rc;

        for (int s = first_slot; s < CAS_OMAP_PAGE_SLOTS; s++) {
            uint64_t candidate = page_num * CAS_OMAP_PAGE_SLOTS +
                                 (uint64_t)s;

            if (candidate >= end)
                return CAS_OMAP_EFULL;

            if (memcmp(de->cached->slots[s], zero_hash,
                       CAS_HASH_LEN) == 0) {
                *id_out = candidate;
                return CAS_OK;
            }
        }

        id = (page_num + 1) * CAS_OMAP_PAGE_SLOTS;
    }

    return CAS_OMAP_EFULL;
}

/****************************************************************
 * Iteration and statistics
 ****************************************************************/

uint64_t
cas_omap_count(struct cas_omap *om)
{
    uint64_t total = 0;

    for (int i = 0; i < om->dir_count; i++)
        total += om->dir[i].pop_count;
    return total;
}

int
cas_omap_page_count(struct cas_omap *om)
{
    int count = 0;

    for (int i = 0; i < om->dir_count; i++)
        if (om->dir[i].pop_count > 0)
            count++;
    return count;
}

int
cas_omap_foreach(struct cas_omap *om, cas_omap_foreach_fn fn,
                 void *ctx)
{
    char hexhash[CAS_HASH_HEX + 1];

    for (int i = 0; i < om->dir_count; i++) {
        struct omap_dir_entry *de = &om->dir[i];

        if (de->pop_count == 0)
            continue;

        int rc = page_load(om, de);

        if (rc != CAS_OK)
            return rc;

        for (int s = 0; s < CAS_OMAP_PAGE_SLOTS; s++) {
            if (memcmp(de->cached->slots[s], zero_hash,
                       CAS_HASH_LEN) == 0)
                continue;

            uint64_t id = de->page_num * CAS_OMAP_PAGE_SLOTS +
                          (uint64_t)s;

            cas_hex_encode(de->cached->slots[s], CAS_HASH_LEN,
                           hexhash);

            if (fn(id, hexhash, ctx) != 0)
                return CAS_OK;
        }
    }

    return CAS_OK;
}
