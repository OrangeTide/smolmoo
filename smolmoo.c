/* smolmoo.c : a smol MOO server */
/* Copyright (c) 2026 Jon Mayo <jon@rm-f.net>
 * SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "rv32.h"
#include "monocypher.h"
#include "smolvfs/cas.h"
#include "smolvfs/cas-omap.h"
#include "smolvfs/cas-sign.h"

#define MAX_CONN 99
#define MAX_OBJ  1024
#define BUFSIZE  4096
#define ATOM_SIZE 4096
#define OK  (0)
#define ERR (-1)
#define FUEL_MAX    10
#define FUEL_REGEN  2
#define TICK_MS     5000
#define GRIT_TICK_MS 60000      /* slow downtime Grit trickle (Section 7) */
#define GRIT_REGEN   1
#define OBJ_NONE     (-1)
#define OBJ_EPH_BASE 1000000000
#define CHAR_PARENT     200
#define ACCT_PARENT     500
#define INVITE_PARENT   600
#define GROUP_PARENT    700
#define AREA_PARENT     900
#define RESET_PARENT    910
#define MAX_TASK     1024
#define TASK_QUANTUM 10000

enum { CONN_FREE, CONN_PENDING, CONN_SSE, CONN_STALLED, CONN_SYS };

/* The reserved system session (slot 0): a connection the task loop never reaps
   and the accept path never hands out, so autonomous agents (OLC-5) keep
   running with no player present. */
#define SYS_SID 0

/* Host-to-VM event kinds, matching mulibc.h. EV_USER is a player command (its
   verb/dobj/arg fields apply); the rest are engine signals with an int tag. */
enum { EV_USER, EV_TIMER, EV_ENTER };

struct conn {
    int fd;
    int state;
    char buf[BUFSIZE];
    int len;
    int obj;
    int fuel;
    int regen_timer;
    int grit;               /* live mirror of the creature's grit property */
    int grit_timer;
    time_t last_cmd;
};

static struct conn cc[MAX_CONN];
static volatile sig_atomic_t running = 1;
static struct cas *cas_store;
static struct cas_omap *obj_map;

static const char *depot_dir = "depot";
static const char *html_file = "index.html";
static char root_file[256] = "depot/root";
static char head_file[256] = "depot/head";
static char key_file[256] = "depot.key";

/* Signed world history (M29). The signing key is loaded at serve start;
 * head_addr/head_seq track the newest version record and are owned by the
 * save writer thread once serving begins. */
static unsigned char server_sk[CAS_SIGN_SECKEY_LEN];
static unsigned char server_pk[CAS_SIGN_PUBKEY_LEN];
static int signing_on = 0;
static char head_addr[CAS_HASH_HEX + 1] = "";
static uint64_t head_seq = 0;

static void
on_signal(int sig)
{
    (void)sig;
    running = 0;
}

/****************************************************************
 * Atoms
 ****************************************************************/

static char atoms[ATOM_SIZE];
static int atoms_used;

static const char *
make_atom(const char *s)
{
    char *p = atoms;

    while (p < atoms + atoms_used) {
        if (strcmp(p, s) == 0)
            return p;
        p += strlen(p) + 1;
    }
    int len = strlen(s) + 1;
    if (atoms_used + len > ATOM_SIZE)
        return NULL;
    memcpy(atoms + atoms_used, s, len);
    p = atoms + atoms_used;
    atoms_used += len;
    return p;
}

/****************************************************************
 * Values and properties
 ****************************************************************/

enum { VAL_NONE, VAL_STR, VAL_NUM, VAL_OBJ, VAL_LIST, VAL_ERR, };

enum {
    E_NONE,      /* no error */
    E_TYPE,      /* type mismatch */
    E_INVARG,    /* invalid argument */
    E_PROPNF,    /* property not found */
    E_VERBNF,    /* verb not found */
    E_PERM,      /* permission denied */
    E_RANGE,     /* index out of range */
    E_MAXREC,    /* max recursion depth */
    E_QUOTA,     /* resource limit exceeded */
};

struct value {
    int type;
    union {
        char *str;
        float num;
        int obj;
        int err;
        struct { struct value *items; int len; } list;
    };
};

enum {
    PERM_OR = 0x20, PERM_OW = 0x10,
    PERM_GR = 0x08, PERM_GW = 0x04,
    PERM_WR = 0x02, PERM_WW = 0x01,
};
#define PERM_DEFAULT (PERM_OR|PERM_OW|PERM_GR|PERM_WR)

struct prop {
    const char *name;
    struct value val;
    uint8_t flags;
};

static struct value
val_str(const char *s)
{
    return (struct value){ VAL_STR, { .str = strdup(s) } };
}

static struct value
val_obj(int id)
{
    return (struct value){ VAL_OBJ, { .obj = id } };
}

static void
val_free(struct value *v)
{
    if (v->type == VAL_STR)
        free(v->str);
    else if (v->type == VAL_LIST) {
        for (int i = 0; i < v->list.len; i++)
            val_free(&v->list.items[i]);
        free(v->list.items);
    } else if (v->type == VAL_ERR) {
        /* error values need no cleanup */
    }
    v->type = VAL_NONE;
}

/****************************************************************
 * Objects
 ****************************************************************/

struct obj {
    int id;
    int parent;
    int owner;
    int group;
    struct prop *props;
    int nprops;
    int task_id;
    unsigned dirty : 1;
    /* Cached rollup: the sum of one numeric field over this object's
     * containment subtree (see sys_rollup). One field cached at a time,
     * recomputed lazily when roll_stale is set. */
    unsigned roll_stale : 1;
    const char *roll_field;
    int roll_val;
};

static struct obj objs[MAX_OBJ];
static int eph_next;

static void
obj_init(void)
{
    for (int i = 0; i < MAX_OBJ; i++) {
        objs[i].id = OBJ_NONE;
        objs[i].owner = OBJ_NONE;
        objs[i].group = OBJ_NONE;
        objs[i].task_id = -1;
    }
}

static int
obj_is_ephemeral(int id)
{
    return id >= OBJ_EPH_BASE;
}

static struct obj *
obj_find(int id)
{
    for (int i = 0; i < MAX_OBJ; i++)
        if (objs[i].id == id)
            return &objs[i];
    return NULL;
}

static struct obj *
obj_create(int id, int parent)
{
    if (obj_is_ephemeral(parent))
        return NULL;
    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE) {
            objs[i].id = id;
            objs[i].parent = parent;
            objs[i].owner = OBJ_NONE;
            objs[i].group = OBJ_NONE;
            objs[i].task_id = -1;
            objs[i].props = NULL;
            objs[i].nprops = 0;
            objs[i].dirty = 1;
            return &objs[i];
        }
    }
    return NULL;
}

static int
obj_alloc_ephemeral(int parent)
{
    for (int i = 0; i < MAX_OBJ; i++) {
        int id = OBJ_EPH_BASE + ((eph_next + i) % MAX_OBJ);
        if (!obj_find(id)) {
            eph_next = (eph_next + i + 1) % MAX_OBJ;
            if (obj_create(id, parent))
                return id;
            return OBJ_NONE;
        }
    }
    return OBJ_NONE;
}

static int prop_objnum(struct obj *o, const char *name);
static void rollup_invalidate_chain(int start);

static void
obj_free(struct obj *o)
{
    /* Removing an object changes its container's subtree sums. */
    rollup_invalidate_chain(prop_objnum(o, make_atom("location")));
    for (int i = 0; i < o->nprops; i++)
        val_free(&o->props[i].val);
    free(o->props);
    o->props = NULL;
    o->nprops = 0;
    o->id = OBJ_NONE;
    o->roll_field = NULL;
    o->roll_stale = 0;
}

/** Format object ID into buffer.
 *
 * OBJ_NONE becomes "#-1", ephemeral objects become "&N",
 * persistent objects become "#N". Returns buf for chaining.
 */
static char *
obj_fmt(int id, char *buf, int bufsz)
{
    if (id == OBJ_NONE)
        snprintf(buf, bufsz, "#-1");
    else if (id >= OBJ_EPH_BASE)
        snprintf(buf, bufsz, "&%d", id - OBJ_EPH_BASE);
    else
        snprintf(buf, bufsz, "#%d", id);
    return buf;
}

static struct value *
prop_get(struct obj *o, const char *name)
{
    for (int depth = 0; o && depth < MAX_OBJ; depth++) {
        for (int i = 0; i < o->nprops; i++)
            if (o->props[i].name == name)
                return &o->props[i].val;
        o = (o->parent != OBJ_NONE) ? obj_find(o->parent) : NULL;
    }
    return NULL;
}

/* Invalidate cached rollups affected by a write to `o`. A move (writing
 * `location`) shifts a subtree between two containers, so both chains go stale;
 * any other field write can change a rolled-up total, so the containing chain
 * goes stale. Cheap: a short walk up the location chain marking bits. */
static void
rollup_note_write(struct obj *o, const char *name, struct value val)
{
    static const char *a_loc;

    if (!a_loc) a_loc = make_atom("location");
    if (name == a_loc) {
        rollup_invalidate_chain(prop_objnum(o, a_loc));   /* old container */
        if (val.type == VAL_OBJ) rollup_invalidate_chain(val.obj);
    } else {
        rollup_invalidate_chain(prop_objnum(o, a_loc));
    }
}

static int
prop_set(struct obj *o, const char *name, struct value val)
{
    rollup_note_write(o, name, val);
    for (int i = 0; i < o->nprops; i++) {
        if (o->props[i].name == name) {
            val_free(&o->props[i].val);
            o->props[i].val = val;
            o->dirty = 1;
            return OK;
        }
    }
    struct prop *p = realloc(o->props,
                 (o->nprops + 1) * sizeof(*p));
    if (!p)
        return ERR;
    o->props = p;
    o->props[o->nprops].name = name;
    o->props[o->nprops].val = val;
    o->props[o->nprops].flags = PERM_DEFAULT;
    o->nprops++;
    o->dirty = 1;
    return OK;
}

static const char *
prop_str(struct obj *o, const char *name)
{
    struct value *v = prop_get(o, name);

    return (v && v->type == VAL_STR) ? v->str : NULL;
}

static int
prop_objnum(struct obj *o, const char *name)
{
    struct value *v = prop_get(o, name);

    return (v && v->type == VAL_OBJ) ? v->obj : OBJ_NONE;
}

/* True if any live object still names `id` as its parent or location, so
   recycling `id` would orphan children (a prototype) or strand contents (a
   room or container). Callers refuse the recycle in that case. */
static int
obj_has_dependents(int id)
{
    const char *a_loc = make_atom("location");

    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE || objs[i].id == id)
            continue;
        if (objs[i].parent == id)
            return 1;
        if (prop_objnum(&objs[i], a_loc) == id)
            return 1;
    }
    return 0;
}

/* Read a string-valued integer property, or dflt when absent. */
static int
prop_int(struct obj *o, const char *name, int dflt)
{
    const char *s = prop_str(o, make_atom(name));

    return s ? atoi(s) : dflt;
}

/* --- Cached rollups over the containment tree (sys_rollup) ---
 *
 * A rollup is the sum of one numeric field over an object's containment
 * subtree (its descendants by `location`, the object's own field excluded).
 * An empty field name counts items instead, each descendant contributing 1
 * (the container item count that `cap` limits). Weight/load and item counts are
 * the first users; derived combat stats are the same shape. Each object caches
 * its own subtree sum for one field and marks it stale when a move or a field
 * write below it changes the total, so a read recomputes only the affected part
 * of the tree and reuses fresh child caches.
 */

/* Mark `start` and its location-ancestors stale: a change to something inside
 * `start` changes its subtree sum and every enclosing container's too. */
static void
rollup_invalidate_chain(int start)
{
    const char *a_loc = make_atom("location");

    for (int guard = 0; start > 0 && guard < MAX_OBJ; guard++) {
        struct obj *o = obj_find(start);

        if (!o) break;
        o->roll_stale = 1;
        start = prop_objnum(o, a_loc);
    }
}

static int rollup_of(int container, const char *field);

/* Sum `field` over the direct children of `container`, adding each child's own
 * cached subtree sum, so the result is the whole subtree total. */
static int
rollup_compute(int container, const char *field)
{
    const char *a_loc = make_atom("location");
    int sum = 0;

    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE) continue;
        if (prop_objnum(&objs[i], a_loc) != container) continue;
        sum += (*field ? prop_int(&objs[i], field, 0) : 1)
             + rollup_of(objs[i].id, field);
    }
    return sum;
}

/* The cached subtree sum of `field` for `container`, recomputed if stale or if
 * the cache currently holds a different field. `field` must be an interned
 * atom so the pointer compare is valid. */
static int
rollup_of(int container, const char *field)
{
    static int depth;
    struct obj *o = obj_find(container);

    if (!o) return 0;
    if (o->roll_stale || o->roll_field != field) {
        if (depth >= 64) return 0;   /* guard a pathological containment cycle */
        depth++;
        o->roll_val = rollup_compute(container, field);
        depth--;
        o->roll_field = field;
        o->roll_stale = 0;
    }
    return o->roll_val;
}

/* The creature sheet behind a player object: the `charid` sheet if set,
 * else the object itself (mirrors cs_sheet in the verb helpers). */
static struct obj *
player_sheet(int obj_id)
{
    struct obj *p = obj_find(obj_id);
    int c;

    if (!p) return NULL;
    c = prop_int(p, "charid", -1);
    return c >= 0 ? obj_find(c) : p;
}

/* Max Grit = 3 + Wit dice + Charm dice (Section 7). Cyberware Max-Grit
 * adjustments arrive with maneuvers and are not folded in yet. */
static int
grit_max(struct obj *o)
{
    int g;

    if (!o) return 1;
    g = 3 + prop_int(o, "wit", 0) / 3 + prop_int(o, "cha", 0) / 3;
    return g < 1 ? 1 : g;
}

/* The band word for a faction-standing step (rules Section 14), mirroring
 * cs_standing_word in the verb helpers. */
static const char *
standing_word(int step)
{
    if (step >= 3) return "Allied";
    if (step == 2) return "Friendly";
    if (step == 1) return "Known";
    if (step == 0) return "Neutral";
    if (step == -1) return "Watched";
    if (step == -2) return "Hostile";
    return "Hunted";
}

/* Max Body Points from a sheet: 12 + (Might dice x 3) + size x 6, mirroring
 * cs_recalc's maxbp. Used to restore BP on revival (Section 11). */
static int
bp_max(struct obj *o)
{
    int m;

    if (!o) return 1;
    m = 12 + (prop_int(o, "mig", 0) / 3) * 3 + prop_int(o, "size", 0) * 6;
    return m < 1 ? 1 : m;
}

/* The signed standing step for `faction` on a sheet, mirroring cs_standing in
 * the verb helpers: the `standing` prop is a comma list of id:step pairs, and
 * an absent faction reads 0 (Neutral). Prefix-safe on ids. */
static int
standing_step(struct obj *o, const char *faction)
{
    const char *s = o ? prop_str(o, make_atom("standing")) : NULL;
    int i = 0;

    if (!s) return 0;
    while (s[i]) {
        int j = 0, match = 1, sign = 1, v = 0;

        while (s[i] && s[i] != ':' && s[i] != ',') {
            if (faction[j] == '\0' || s[i] != faction[j]) match = 0;
            else j++;
            i++;
        }
        if (faction[j] != '\0') match = 0;
        if (s[i] == ':') {
            i++;
            if (s[i] == '-') { sign = -1; i++; }
            while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
        }
        while (s[i] == ',') i++;
        if (match) return sign * v;
    }
    return 0;
}

static int
prop_is_reserved(const char *name)
{
    return strcmp(name, "owner") == 0 || strcmp(name, "group") == 0;
}

static uint8_t
prop_parse_flags(const char *s)
{
    uint8_t f = 0;
    int shift = 4;

    for (; *s; s++) {
        if (*s == ',') {
            shift -= 2;
            if (shift < 0) break;
        } else if (*s == 'r')
            f |= (2 << shift);
        else if (*s == 'w')
            f |= (1 << shift);
    }
    return f;
}

static void
prop_fmt_flags(uint8_t flags, char *buf, int bufsz)
{
    int n = 0;

    if (flags == PERM_DEFAULT) {
        buf[0] = '\0';
        return;
    }
    if (n < bufsz - 1) buf[n++] = ':';
    if ((flags & PERM_OR) && n < bufsz - 1) buf[n++] = 'r';
    if ((flags & PERM_OW) && n < bufsz - 1) buf[n++] = 'w';
    if (n < bufsz - 1) buf[n++] = ',';
    if ((flags & PERM_GR) && n < bufsz - 1) buf[n++] = 'r';
    if ((flags & PERM_GW) && n < bufsz - 1) buf[n++] = 'w';
    if (n < bufsz - 1) buf[n++] = ',';
    if ((flags & PERM_WR) && n < bufsz - 1) buf[n++] = 'r';
    if ((flags & PERM_WW) && n < bufsz - 1) buf[n++] = 'w';
    buf[n] = '\0';
}

static int
is_wizard(int sid)
{
    struct obj *po = obj_find(cc[sid].obj);

    return po && prop_str(po, make_atom("admin")) != NULL;
}

static struct obj *
player_acct(int sid)
{
    struct obj *po = obj_find(cc[sid].obj);
    const char *name;

    if (!po)
        return NULL;
    name = prop_str(po, make_atom("name"));
    if (!name)
        return NULL;
    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE || objs[i].parent != ACCT_PARENT)
            continue;
        const char *an = prop_str(&objs[i], make_atom("name"));
        if (an && strcasecmp(an, name) == 0)
            return &objs[i];
    }
    return NULL;
}

/* Permission checks are keyed on an account object, so both a logged-in
 * player (via player_acct) and a running verb (via its owner account) can be
 * checked the same way. The int-sid wrappers below serve the direct-player
 * paths; the VM syscalls check the verb owner's account instead. */
static int
acct_is_wizard(struct obj *acct)
{
    return acct && prop_str(acct, make_atom("admin")) != NULL;
}

static int
acct_owner_match(struct obj *o, struct obj *acct)
{
    return o && acct && o->owner != OBJ_NONE && o->owner == acct->id;
}

static int
obj_owner_match(struct obj *o, int sid)
{
    return acct_owner_match(o, player_acct(sid));
}

static int
acct_group_match(struct obj *o, struct obj *acct)
{
    struct obj *grp;
    const char *members;
    char needle[16];
    int nlen;

    if (!o || o->group == OBJ_NONE || !acct)
        return 0;
    grp = obj_find(o->group);
    if (!grp || grp->parent != GROUP_PARENT)
        return 0;
    members = prop_str(grp, make_atom("members"));
    if (!members)
        return 0;
    snprintf(needle, sizeof(needle), "#%d", acct->id);
    nlen = strlen(needle);
    for (const char *p = members; *p; ) {
        while (*p == ',' || *p == ' ') p++;
        if (!*p) break;
        if (strncmp(p, needle, nlen) == 0 &&
            (p[nlen] == ',' || p[nlen] == '\0'))
            return 1;
        while (*p && *p != ',') p++;
    }
    return 0;
}

static int
obj_group_match(struct obj *o, int sid)
{
    return acct_group_match(o, player_acct(sid));
}

static int
acct_can_read(struct obj *acct, struct obj *o, struct prop *p)
{
    if (acct_is_wizard(acct))
        return 1;
    if (acct_owner_match(o, acct))
        return (p->flags & PERM_OR) != 0;
    if (acct_group_match(o, acct))
        return (p->flags & PERM_GR) != 0;
    return (p->flags & PERM_WR) != 0;
}

static int
perm_can_read(int sid, struct obj *o, struct prop *p)
{
    return acct_can_read(player_acct(sid), o, p);
}

static int
acct_can_write(struct obj *acct, struct obj *o, struct prop *p)
{
    if (acct_is_wizard(acct))
        return 1;
    if (acct_owner_match(o, acct))
        return (p->flags & PERM_OW) != 0;
    if (acct_group_match(o, acct))
        return (p->flags & PERM_GW) != 0;
    return (p->flags & PERM_WW) != 0;
}

static int
perm_can_write(int sid, struct obj *o, struct prop *p)
{
    return acct_can_write(player_acct(sid), o, p);
}

static int
elf_parse(const char *val, int *mode, char *hash, int hashsz)
{
    if (val[0] == '[') {
        const char *comma, *p, *end;
        int len;
        *mode = strtol(val + 1, NULL, 8);
        comma = strchr(val, ',');
        if (!comma) return ERR;
        p = comma + 1;
        while (*p == ' ') p++;
        if (strncmp(p, "b2:", 3) != 0) return ERR;
        p += 3;
        end = strchr(p, ']');
        len = end ? (int)(end - p) : (int)strlen(p);
        if (len >= hashsz) return ERR;
        memcpy(hash, p, len);
        hash[len] = '\0';
        return OK;
    }
    if (strncmp(val, "b2:", 3) == 0) {
        *mode = 0755;
        strncpy(hash, val + 3, hashsz - 1);
        hash[hashsz - 1] = '\0';
        return OK;
    }
    return ERR;
}

static int
perm_can_exec_elf(int sid, struct obj *verb_obj, int mode)
{
    if (is_wizard(sid))
        return 1;
    if (obj_owner_match(verb_obj, sid))
        return (mode & 0100) != 0;
    if (obj_group_match(verb_obj, sid))
        return (mode & 0010) != 0;
    return (mode & 0001) != 0;
}

/****************************************************************
 * Timers
 ****************************************************************/

struct timer_entry {
    int64_t deadline_ns;
    void (*cb)(void *);
    void *arg;
    int id;
};

#define PQ_NAME        timer_pq
#define PQ_ENTRY_TYPE  struct timer_entry
#define PQ_KEY(e)      ((e).deadline_ns)
#define PQ_STATIC
#define PQ_IMPLEMENTATION
#include "pq.h"

static struct timer_pq timers;
static int timer_next_id;

static int64_t
now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void
timer_init(void)
{
    timer_pq_init(&timers, 16);
}

static void
timer_shutdown(void)
{
    timer_pq_free(&timers);
}

static int
timer_add(int ms, void (*cb)(void *), void *arg)
{
    struct timer_entry e;

    e.deadline_ns = now_ns() + (int64_t)ms * 1000000LL;
    e.cb = cb;
    e.arg = arg;
    e.id = timer_next_id++;
    if (!timer_pq_enqueue(&timers, e)) {
        if (!timer_pq_resize(&timers, timers.max * 2))
            return -1;
        if (!timer_pq_enqueue(&timers, e))
            return -1;
    }
    return e.id;
}

static void
timer_remove(int id)
{
    for (unsigned i = 0; i < timer_pq_size(&timers); i++) {
        struct timer_entry *ep = timer_pq_peek(&timers, i);
        if (ep && ep->id == id) {
            timer_pq_remove(&timers, i, NULL);
            return;
        }
    }
}

static int
timer_next_ms(void)
{
    struct timer_entry *top = timer_pq_top(&timers);
    int64_t diff;

    if (!top)
        return -1;
    diff = (top->deadline_ns - now_ns() + 999999LL) / 1000000LL;
    if (diff < 0)
        diff = 0;
    if (diff > 0x7FFFFFFF)
        diff = 0x7FFFFFFF;
    return (int)diff;
}

static void
timer_dispatch(void)
{
    struct timer_entry e;
    int64_t t = now_ns();

    while (timer_pq_top(&timers) &&
           timer_pq_top(&timers)->deadline_ns <= t) {
        timer_pq_dequeue(&timers, &e);
        if (e.cb)
            e.cb(e.arg);
    }
}

/****************************************************************
 * Object serialization (text format, used as CAS blob body)
 ****************************************************************/

static void
unescape(char *s)
{
    char *r = s, *w = s;

    while (*r) {
        if (*r == '\\' && r[1]) {
            r++;
            switch (*r) {
            case 't': *w++ = '\t'; break;
            case 'n': *w++ = '\n'; break;
            case 'e': *w++ = '\033'; break;
            default:  *w++ = *r; break;
            }
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static int
escape_len(const char *s)
{
    int n = 0;

    while (*s) {
        switch (*s) {
        case '\t': case '\n': case '\033': case '\\': n += 2; break;
        default: n++; break;
        }
        s++;
    }
    return n;
}

static char *
escape_copy(char *dst, const char *s)
{
    while (*s) {
        switch (*s) {
        case '\t':  *dst++ = '\\'; *dst++ = 't'; break;
        case '\n':  *dst++ = '\\'; *dst++ = 'n'; break;
        case '\033': *dst++ = '\\'; *dst++ = 'e'; break;
        case '\\':  *dst++ = '\\'; *dst++ = '\\'; break;
        default:    *dst++ = *s; break;
        }
        s++;
    }
    return dst;
}

static int
obj_serialize(struct obj *o, char **out, size_t *outlen)
{
    size_t cap = 256;
    char *buf = malloc(cap);
    int pos = 0;

    if (!buf) return ERR;

#define ENSURE(n) do { \
    while (pos + (int)(n) >= (int)cap) { \
        cap *= 2; \
        char *nb = realloc(buf, cap); \
        if (!nb) { free(buf); return ERR; } \
        buf = nb; \
    } \
} while (0)

    if (o->parent == OBJ_NONE) {
        ENSURE(32);
        pos += snprintf(buf + pos, cap - pos, "#%d\n", o->id);
    } else {
        ENSURE(32);
        pos += snprintf(buf + pos, cap - pos,
                        "#%d #%d\n", o->id, o->parent);
    }
    if (o->owner != OBJ_NONE) {
        ENSURE(32);
        pos += snprintf(buf + pos, cap - pos,
                        "owner=#%d\n", o->owner);
    }
    if (o->group != OBJ_NONE) {
        ENSURE(32);
        pos += snprintf(buf + pos, cap - pos,
                        "group=#%d\n", o->group);
    }

    for (int j = 0; j < o->nprops; j++) {
        struct value *v = &o->props[j].val;
        char fb[8];
        prop_fmt_flags(o->props[j].flags, fb, sizeof(fb));
        int nlen = strlen(o->props[j].name);
        int flen = strlen(fb);

        if (v->type == VAL_STR) {
            int elen = escape_len(v->str);
            ENSURE(nlen + flen + 1 + elen + 2);
            memcpy(buf + pos, o->props[j].name, nlen);
            pos += nlen;
            memcpy(buf + pos, fb, flen);
            pos += flen;
            buf[pos++] = '=';
            pos = (int)(escape_copy(buf + pos, v->str) - buf);
            buf[pos++] = '\n';
        } else if (v->type == VAL_OBJ) {
            ENSURE(nlen + flen + 16);
            pos += snprintf(buf + pos, cap - pos, "%s%s=#%d\n",
                            o->props[j].name, fb, v->obj);
        }
    }
#undef ENSURE

    *out = buf;
    *outlen = pos;
    return OK;
}

/* Parser state for obj_load_buf. When map_n > 0, object ids and
 * references whose value appears in map_from[] are rewritten to the
 * parallel entry in map_to[] (used by merge to renumber a set of
 * objects into a fresh range). References outside the set are left
 * unchanged so they still point at existing world objects. When
 * clean is set, loaded objects are marked not-dirty (omap load path);
 * otherwise they stay dirty for a following world_save (text/merge). */
struct load_ctx {
    struct obj *cur;
    struct value *lastval;
    int count;
    int clean;
    const int *map_from;
    const int *map_to;
    int map_n;
};

static int
load_remap(struct load_ctx *lc, int id)
{
    for (int i = 0; i < lc->map_n; i++)
        if (lc->map_from[i] == id)
            return lc->map_to[i];
    return id;
}

static int
obj_load_buf(const char *data, size_t len, struct load_ctx *lc)
{
    char line[BUFSIZE];
    const char *p = data, *end = data + len;

    while (p < end) {
        const char *eol = memchr(p, '\n', end - p);
        if (!eol) eol = end;
        int llen = (int)(eol - p);
        if (llen >= (int)sizeof(line)) llen = sizeof(line) - 1;
        memcpy(line, p, llen);
        line[llen] = '\0';
        p = eol + 1;

        while (llen > 0 && (line[llen - 1] == '\r'))
            line[--llen] = '\0';

        if (llen == 0) {
            lc->cur = NULL;
            lc->lastval = NULL;
            continue;
        }

        if (isspace(line[0])) {
            if (lc->lastval && lc->lastval->type == VAL_STR) {
                char *lp = line;
                while (isspace(*lp)) lp++;
                int olen = strlen(lc->lastval->str);
                int alen = strlen(lp);
                char *ns = realloc(lc->lastval->str,
                           olen + 1 + alen + 1);
                if (!ns) continue;
                ns[olen] = '\n';
                memcpy(ns + olen + 1, lp, alen + 1);
                lc->lastval->str = ns;
            }
            continue;
        }

        if (line[0] == '#') {
            int id, parent = OBJ_NONE;
            char *lp = line + 1;
            id = strtol(lp, &lp, 10);
            while (isspace(*lp)) lp++;
            if (*lp == '#')
                parent = strtol(lp + 1, &lp, 10);
            id = load_remap(lc, id);
            if (parent != OBJ_NONE)
                parent = load_remap(lc, parent);
            lc->cur = obj_create(id, parent);
            if (lc->cur) {
                if (lc->clean) lc->cur->dirty = 0;
                lc->count++;
            }
            lc->lastval = NULL;
            continue;
        }

        char *eq = strchr(line, '=');
        if (eq && lc->cur) {
            struct obj *cur = lc->cur;
            *eq = '\0';
            char *colon = strchr(line, ':');
            uint8_t flags = PERM_DEFAULT;
            if (colon) {
                *colon = '\0';
                flags = prop_parse_flags(colon + 1);
            }
            if (prop_is_reserved(line)) {
                char *val = eq + 1;
                if (strcmp(line, "owner") == 0 && *val == '#')
                    cur->owner = load_remap(lc,
                                     strtol(val + 1, NULL, 10));
                else if (strcmp(line, "group") == 0 &&
                         *val == '#')
                    cur->group = load_remap(lc,
                                     strtol(val + 1, NULL, 10));
                lc->lastval = NULL;
            } else {
                const char *name = make_atom(line);
                char *val = eq + 1;
                unescape(val);
                if (*val == '#') {
                    int ref = load_remap(lc,
                                  strtol(val + 1, NULL, 10));
                    prop_set(cur, name, val_obj(ref));
                    lc->lastval = NULL;
                } else {
                    prop_set(cur, name, val_str(val));
                    lc->lastval = &cur->props[cur->nprops - 1].val;
                }
                if (flags != PERM_DEFAULT)
                    cur->props[cur->nprops - 1].flags = flags;
            }
            if (lc->clean) cur->dirty = 0;
        }
    }
    return lc->count ? OK : ERR;
}

static int
obj_deserialize(const char *data, size_t len)
{
    struct load_ctx lc = { 0 };

    lc.clean = 1;
    return obj_load_buf(data, len, &lc);
}

/****************************************************************
 * Object-map persistence
 ****************************************************************/

static int
root_read(char *hash_out)
{
    FILE *f = fopen(root_file, "r");

    if (!f) return ERR;
    if (!fgets(hash_out, CAS_HASH_HEX + 1, f)) {
        fclose(f);
        return ERR;
    }
    fclose(f);
    return OK;
}

/* Atomically and durably write a one-line pointer file (root or history
   head). The value is flushed and fsynced before the rename, and the
   containing directory is fsynced after, so the pointer never references
   content that has not itself reached disk. */
static int
ptr_write(const char *file, const char *val)
{
    char tmp[260];
    FILE *f;

    snprintf(tmp, sizeof(tmp), "%s.tmp", file);
    f = fopen(tmp, "w");
    if (!f) return ERR;
    fprintf(f, "%s\n", val);
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
        fclose(f); unlink(tmp); return ERR;
    }
    if (fclose(f) != 0) { unlink(tmp); return ERR; }
    if (rename(tmp, file) != 0) { unlink(tmp); return ERR; }
    {
        char dir[260];
        snprintf(dir, sizeof(dir), "%s", file);
        char *slash = strrchr(dir, '/');
        int dfd = open(slash ? (*slash = '\0', dir) : ".", O_RDONLY);
        if (dfd >= 0) { fsync(dfd); close(dfd); }
    }
    return OK;
}

static int
root_write(const char *hash)
{
    return ptr_write(root_file, hash);
}

/* ---- world save: snapshot (main thread) + flush (writer thread) -----
 *
 * A save has two halves. save_snapshot() runs on the main thread: it
 * serializes each dirty object into a self-contained byte buffer and clears
 * its dirty flag, capturing a consistent point-in-time image without holding
 * any object pointers. save_flush() consumes that snapshot: it writes each
 * buffer into the CAS, updates the object map, and rewrites the durable root.
 *
 * During the serve loop save_flush() runs only on a single background writer
 * thread, so the CAS root pointer and obj_map have exactly one accessor and
 * the fsync latency stays off the main loop. CLI subcommands (migrate, merge,
 * install) run save_flush() synchronously since no writer thread exists. */

struct save_ent {
    uint64_t id;
    char *buf;
    size_t len;
};

struct save_job {
    struct save_ent *ents;
    int n;
};

/* Serialize every dirty persistent object into a job and clear its dirty
   flag. Returns NULL on allocation failure (dirty flags left untouched). */
static struct save_job *
save_snapshot(void)
{
    int dirty = 0;

    for (int i = 0; i < MAX_OBJ; i++) {
        struct obj *o = &objs[i];
        if (o->id == OBJ_NONE || obj_is_ephemeral(o->id))
            continue;
        if (o->dirty)
            dirty++;
    }

    struct save_job *job = malloc(sizeof(*job));
    if (!job)
        return NULL;
    job->n = 0;
    job->ents = dirty ? calloc((size_t)dirty, sizeof(*job->ents)) : NULL;
    if (dirty && !job->ents) {
        free(job);
        return NULL;
    }

    for (int i = 0; i < MAX_OBJ && job->n < dirty; i++) {
        struct obj *o = &objs[i];
        if (o->id == OBJ_NONE || obj_is_ephemeral(o->id) || !o->dirty)
            continue;
        char *buf;
        size_t len;
        if (obj_serialize(o, &buf, &len) != OK)
            continue;               /* leave dirty; retry on next save */
        job->ents[job->n].id = (uint64_t)o->id;
        job->ents[job->n].buf = buf;
        job->ents[job->n].len = len;
        job->n++;
        o->dirty = 0;
    }
    return job;
}

/* Append a signed version record naming `root` to the world history chain
   and advance the head pointer. Called from save_flush after the root is
   durable, so a crash before the record is written loses only a history
   entry, never world content. Head state is owned by the writer thread
   during serve. */
static void
history_record(const char *root)
{
    struct cas_vrec v;
    unsigned char rec[CAS_VREC_LEN];
    char vaddr[CAS_HASH_HEX + 1];

    memcpy(v.pubkey, server_pk, CAS_SIGN_PUBKEY_LEN);
    v.seq = head_seq + 1;
    v.timestamp = (int64_t)time(NULL);
    memcpy(v.root, root, CAS_HASH_HEX + 1);
    memcpy(v.prev, head_addr, CAS_HASH_HEX + 1);

    if (cas_vrec_encode(&v, server_sk, rec) != CAS_OK ||
        cas_put_object(cas_store, CAS_VREC_TYPE, rec, CAS_VREC_LEN,
                       vaddr) != CAS_OK ||
        ptr_write(head_file, vaddr) != OK) {
        fprintf(stderr, "[history] failed to record version %llu\n",
                (unsigned long long)v.seq);
        return;
    }
    memcpy(head_addr, vaddr, CAS_HASH_HEX + 1);
    head_seq = v.seq;
}

/* Write a snapshot into the CAS, update the object map, rewrite the root,
   and free the job. Touches obj_map and the root file; during serve this
   runs only on the writer thread. */
static int
save_flush(struct save_job *job)
{
    int rc = OK;

    for (int i = 0; i < job->n; i++) {
        char hash[CAS_HASH_HEX + 1];
        if (cas_put_object(cas_store, "obj", job->ents[i].buf,
                           job->ents[i].len, hash) != CAS_OK ||
            cas_omap_put(obj_map, job->ents[i].id, hash) != CAS_OK)
            rc = ERR;
        free(job->ents[i].buf);
    }
    if (rc == OK && job->n > 0) {
        char root[CAS_HASH_HEX + 1];
        if (cas_omap_store(obj_map, root) != CAS_OK ||
            root_write(root) != OK)
            rc = ERR;
        else {
            fprintf(stderr, "[save] %d dirty objects stored\n", job->n);
            if (signing_on)
                history_record(root);
        }
    }
    free(job->ents);
    free(job);
    return rc;
}

/* Synchronous save: snapshot then flush on the calling thread. Used by CLI
   subcommands and as the fallback when the writer thread is not running. */
static int
world_save(const char *path)
{
    (void)path;
    struct save_job *job = save_snapshot();
    if (!job)
        return ERR;
    return save_flush(job);
}

/* ---- background writer thread --------------------------------------- */

#define SAVE_QUEUE_MAX 2   /* outstanding saves before the main loop blocks */

static struct {
    pthread_t thread;
    pthread_mutex_t mtx;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    pthread_cond_t idle;
    struct save_job *q[SAVE_QUEUE_MAX];
    int head;
    int count;
    int busy;
    int stop;
    int started;
} saver;

static void *
saver_main(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&saver.mtx);
    for (;;) {
        while (saver.count == 0 && !saver.stop)
            pthread_cond_wait(&saver.not_empty, &saver.mtx);
        if (saver.count == 0 && saver.stop)
            break;
        struct save_job *job = saver.q[saver.head];
        saver.head = (saver.head + 1) % SAVE_QUEUE_MAX;
        saver.count--;
        saver.busy = 1;
        pthread_cond_signal(&saver.not_full);
        pthread_mutex_unlock(&saver.mtx);

        if (save_flush(job) != OK)
            fprintf(stderr, "[save] async flush failed\n");

        pthread_mutex_lock(&saver.mtx);
        saver.busy = 0;
        pthread_cond_signal(&saver.idle);
    }
    pthread_mutex_unlock(&saver.mtx);
    return NULL;
}

static int
saver_start(void)
{
    memset(&saver, 0, sizeof(saver));
    pthread_mutex_init(&saver.mtx, NULL);
    pthread_cond_init(&saver.not_full, NULL);
    pthread_cond_init(&saver.not_empty, NULL);
    pthread_cond_init(&saver.idle, NULL);
    if (pthread_create(&saver.thread, NULL, saver_main, NULL) != 0)
        return ERR;
    saver.started = 1;
    return OK;
}

/* Signal the writer to drain its queue and exit, then join it. */
static void
saver_stop(void)
{
    if (!saver.started)
        return;
    pthread_mutex_lock(&saver.mtx);
    saver.stop = 1;
    pthread_cond_signal(&saver.not_empty);
    pthread_mutex_unlock(&saver.mtx);
    pthread_join(saver.thread, NULL);
    pthread_mutex_destroy(&saver.mtx);
    pthread_cond_destroy(&saver.not_full);
    pthread_cond_destroy(&saver.not_empty);
    pthread_cond_destroy(&saver.idle);
    saver.started = 0;
}

/* Block until the queue is empty and no flush is in flight. */
static void
saver_wait_idle(void)
{
    if (!saver.started)
        return;
    pthread_mutex_lock(&saver.mtx);
    while (saver.count > 0 || saver.busy)
        pthread_cond_wait(&saver.idle, &saver.mtx);
    pthread_mutex_unlock(&saver.mtx);
}

/* Snapshot the world on the main thread and hand it to the writer. Blocks
   only if SAVE_QUEUE_MAX saves are already outstanding. Falls back to a
   synchronous save when the writer thread is not running. */
static void
world_save_async(void)
{
    if (!saver.started) {
        world_save(NULL);
        return;
    }
    struct save_job *job = save_snapshot();
    if (!job) {
        fprintf(stderr, "[save] snapshot alloc failed\n");
        return;
    }
    if (job->n == 0) {      /* nothing dirty: root already current */
        free(job->ents);
        free(job);
        return;
    }
    pthread_mutex_lock(&saver.mtx);
    while (saver.count == SAVE_QUEUE_MAX)
        pthread_cond_wait(&saver.not_full, &saver.mtx);
    saver.q[(saver.head + saver.count) % SAVE_QUEUE_MAX] = job;
    saver.count++;
    pthread_cond_signal(&saver.not_empty);
    pthread_mutex_unlock(&saver.mtx);
}

/* Load the current history head from head_file, verifying the record it
   names, and set head_addr/head_seq. A missing or unreadable head is a
   fresh chain (seq 0), not an error. */
static void
history_load_head(void)
{
    FILE *f;
    char line[CAS_HASH_HEX + 2];

    head_addr[0] = '\0';
    head_seq = 0;

    f = fopen(head_file, "r");
    if (!f)
        return;
    if (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        struct cas_file cf;
        char type[CAS_TYPE_MAX];
        if (cas_open_object(cas_store, &cf, line, type,
                            sizeof(type)) == CAS_OK) {
            struct cas_vrec v;
            if (cas_vrec_decode((const unsigned char *)cf.data, cf.len,
                                &v) == CAS_OK) {
                memcpy(head_addr, line, CAS_HASH_HEX + 1);
                head_seq = v.seq;
            }
            cas_close(&cf);
        }
    }
    fclose(f);
}

/* Prepare signed world history for the serve loop: load the server's
   signing key (generating one on first run), then read the chain head.
   On any key problem, signing is left off and saves proceed unsigned. */
static void
history_init(void)
{
    int rc;

    signing_on = 0;
    if (!cas_sign_available()) {
        fprintf(stderr, "[history] no signing backend; history disabled\n");
        return;
    }

    rc = cas_sign_key_load(key_file, server_sk, server_pk);
    if (rc == CAS_ENOTFOUND) {
        rc = cas_sign_key_generate(key_file, server_pk);
        if (rc == CAS_OK)
            rc = cas_sign_key_load(key_file, server_sk, server_pk);
        if (rc == CAS_OK)
            fprintf(stderr, "[history] generated signing key %s\n",
                    key_file);
    }
    if (rc != CAS_OK) {
        fprintf(stderr, "[history] signing disabled: %s\n",
                cas_sign_strerror(rc));
        return;
    }

    history_load_head();
    signing_on = 1;
    fprintf(stderr, "[history] world signing on, chain at seq %llu\n",
            (unsigned long long)head_seq);
}

/* Read an entire file into a malloc'd, NUL-terminated buffer.
 * Returns the buffer (caller frees) and stores the length in *lenp,
 * or NULL on error. */
static char *
file_slurp(const char *path, long *lenp)
{
    FILE *f = fopen(path, "rb");
    long sz;
    char *data;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    data = malloc(sz + 1);
    if (!data) { fclose(f); return NULL; }
    sz = (long)fread(data, 1, sz, f);
    fclose(f);
    data[sz] = '\0';
    *lenp = sz;
    return data;
}

static int
world_load_text(const char *path)
{
    struct load_ctx lc = { 0 };
    long len;
    char *data = file_slurp(path, &len);

    if (!data) return ERR;
    obj_load_buf(data, (size_t)len, &lc);
    free(data);
    fprintf(stderr, "loaded %d objects from %s\n", lc.count, path);
    return OK;
}

static int
world_migrate(const char *text_path)
{
    fprintf(stderr, "[migrate] converting %s to omap...\n",
            text_path);
    if (world_load_text(text_path) != OK)
        return ERR;

    for (int i = 0; i < MAX_OBJ; i++) {
        struct obj *o = &objs[i];
        if (o->id == OBJ_NONE || obj_is_ephemeral(o->id))
            continue;
        o->dirty = 1;
    }

    if (world_save(NULL) != OK)
        return ERR;

    fprintf(stderr, "[migrate] done\n");
    return OK;
}

static int
load_one_cb(uint64_t id, const char *hash, void *ctx)
{
    struct cas_file cf;
    char type[CAS_TYPE_MAX];
    int *count = ctx;

    (void)id;
    if (cas_open_object(cas_store, &cf, hash, type,
                        sizeof(type)) != CAS_OK)
        return 0;
    obj_deserialize((const char *)cf.data, cf.len);
    cas_close(&cf);
    (*count)++;
    return 0;
}

static int
world_load(const char *path)
{
    char root[CAS_HASH_HEX + 1];

    if (root_read(root) == OK) {
        if (cas_omap_load(obj_map, root) != CAS_OK) {
            fprintf(stderr, "failed to load omap root\n");
            return ERR;
        }
        int count = 0;
        cas_omap_foreach(obj_map, load_one_cb, &count);
        fprintf(stderr, "loaded %d objects from omap\n", count);
        return OK;
    }

    if (access(path, F_OK) == 0)
        return world_migrate(path);

    fprintf(stderr, "no world data found, starting fresh\n");
    return OK;
}

static void
world_free(void)
{
    for (int i = 0; i < MAX_OBJ; i++)
        if (objs[i].id != OBJ_NONE)
            obj_free(&objs[i]);
}

/****************************************************************
 * Accounts and invite codes
 ****************************************************************/

#define USER_CODE_COUNT 3
#define ADMIN_CODE_USES 10
#define INVITE_REFRESH_MS (24 * 3600 * 1000)

static const char invite_chars[] = "ABCEGHJKLMNPRSTVWXYZ0123456789";

static int
rand_bytes(unsigned char *buf, int n)
{
    int fd = open("/dev/urandom", O_RDONLY);
    int r;

    if (fd < 0) return ERR;
    r = read(fd, buf, n);
    close(fd);
    return (r == n) ? OK : ERR;
}

#define PW_SALT_LEN  16
#define PW_HASH_LEN  32
#define PW_BLOCKS    256
#define PW_PASSES    3
#define PW_STR_LEN   (PW_SALT_LEN * 2 + 1 + PW_HASH_LEN * 2)

static void
hex_encode(const uint8_t *src, int n, char *dst)
{
    for (int i = 0; i < n; i++)
        sprintf(dst + i * 2, "%02x", src[i]);
}

static int
hex_decode(const char *src, uint8_t *dst, int n)
{
    for (int i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(src + i * 2, "%2x", &v) != 1)
            return ERR;
        dst[i] = v;
    }
    return OK;
}

static int
pw_hash(const char *pw, char *out)
{
    uint8_t salt[PW_SALT_LEN], hash[PW_HASH_LEN];
    uint8_t work[PW_BLOCKS * 1024];
    crypto_argon2_config cfg = {
        CRYPTO_ARGON2_ID, PW_BLOCKS, PW_PASSES, 1
    };
    crypto_argon2_inputs inp = {
        (const uint8_t *)pw, salt,
        (uint32_t)strlen(pw), PW_SALT_LEN
    };

    if (rand_bytes(salt, PW_SALT_LEN) != OK)
        return ERR;
    crypto_argon2(hash, PW_HASH_LEN, work, cfg, inp,
                  crypto_argon2_no_extras);
    hex_encode(salt, PW_SALT_LEN, out);
    out[PW_SALT_LEN * 2] = '$';
    hex_encode(hash, PW_HASH_LEN, out + PW_SALT_LEN * 2 + 1);
    out[PW_STR_LEN] = '\0';
    return OK;
}

static int
pw_verify(const char *pw, const char *stored)
{
    uint8_t salt[PW_SALT_LEN], hash[PW_HASH_LEN], expected[PW_HASH_LEN];
    uint8_t work[PW_BLOCKS * 1024];
    crypto_argon2_config cfg = {
        CRYPTO_ARGON2_ID, PW_BLOCKS, PW_PASSES, 1
    };
    crypto_argon2_inputs inp = {
        (const uint8_t *)pw, salt,
        (uint32_t)strlen(pw), PW_SALT_LEN
    };

    if ((int)strlen(stored) != PW_STR_LEN || stored[PW_SALT_LEN * 2] != '$')
        return ERR;
    if (hex_decode(stored, salt, PW_SALT_LEN) != OK)
        return ERR;
    if (hex_decode(stored + PW_SALT_LEN * 2 + 1, expected, PW_HASH_LEN) != OK)
        return ERR;
    crypto_argon2(hash, PW_HASH_LEN, work, cfg, inp,
                  crypto_argon2_no_extras);
    if (crypto_verify32(hash, expected) != 0)
        return ERR;
    return OK;
}

static int
invite_gen_code(char *out, int outsz)
{
    unsigned char rnd[12];

    if (outsz < 15 || rand_bytes(rnd, sizeof(rnd)) != OK)
        return ERR;
    for (int i = 0, j = 0; i < 12; i++) {
        if (i > 0 && i % 4 == 0)
            out[j++] = '-';
        out[j++] = invite_chars[rnd[i] % 30];
    }
    out[14] = '\0';
    return OK;
}

static int
obj_next_id(void)
{
    uint64_t id;
    static const char placeholder[CAS_HASH_HEX + 1] =
        "0000000000000000000000000000000000000000000000000000000000000001";

    if (obj_map) {
        uint64_t start = 0;
        while (cas_omap_alloc(obj_map, start, OBJ_EPH_BASE - start,
                              &id) == CAS_OK) {
            if (!obj_find((int)id)) {
                cas_omap_put(obj_map, id, placeholder);
                return (int)id;
            }
            start = id + 1;
            if (start >= (uint64_t)OBJ_EPH_BASE)
                break;
        }
    }
    for (int i = 0; i < MAX_OBJ; i++)
        if (!obj_find(i))
            return i;
    return OBJ_NONE;
}

static struct obj *
acct_find(const char *name)
{
    const char *a = make_atom("name");

    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE || objs[i].parent != ACCT_PARENT)
            continue;
        const char *n = prop_str(&objs[i], a);
        if (n && strcasecmp(n, name) == 0)
            return &objs[i];
    }
    return NULL;
}

/* Ensure the account has a persistent ChromeSix character sheet, a
 * child of the Character Prototype (#200) owned by the account. Base
 * stats are inherited from #200 until customized. Returns the character
 * object id, or OBJ_NONE on failure. */
static int
char_ensure(struct obj *acct)
{
    const char *a_char = make_atom("character");
    int cid = prop_objnum(acct, a_char);
    struct obj *ch;
    const char *nm;

    if (cid != OBJ_NONE && obj_find(cid))
        return cid;
    cid = obj_next_id();
    if (cid == OBJ_NONE)
        return OBJ_NONE;
    ch = obj_create(cid, CHAR_PARENT);
    if (!ch)
        return OBJ_NONE;
    ch->owner = acct->id;
    nm = prop_str(acct, make_atom("name"));
    if (nm)
        prop_set(ch, make_atom("name"), val_str(nm));
    prop_set(acct, a_char, val_obj(cid));
    return cid;
}

static struct obj *
invite_find(const char *code)
{
    const char *a = make_atom("code");

    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE || objs[i].parent != INVITE_PARENT)
            continue;
        const char *c = prop_str(&objs[i], a);
        if (c && strcasecmp(c, code) == 0)
            return &objs[i];
    }
    return NULL;
}

static struct obj *
invite_create(int owner_id, int max_uses, int is_admin)
{
    int id = obj_next_id();
    struct obj *inv;
    char code[16], tmp[16];

    if (id == OBJ_NONE) return NULL;
    inv = obj_create(id, INVITE_PARENT);
    if (!inv) return NULL;
    if (invite_gen_code(code, sizeof(code)) != OK) {
        obj_free(inv);
        return NULL;
    }
    prop_set(inv, make_atom("code"), val_str(code));
    snprintf(tmp, sizeof(tmp), "%d", max_uses);
    prop_set(inv, make_atom("max_uses"), val_str(tmp));
    prop_set(inv, make_atom("used"), val_str("0"));
    if (is_admin)
        prop_set(inv, make_atom("admin"), val_str("1"));
    inv->owner = owner_id;
    return inv;
}

static int
invite_count(int owner_id)
{
    int count = 0;

    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE || objs[i].parent != INVITE_PARENT)
            continue;
        if (objs[i].owner == owner_id)
            count++;
    }
    return count;
}

static void
world_bootstrap(void)
{
    struct obj *sys = obj_find(0);
    if (sys)
        sys->parent = OBJ_NONE;
}

static void
invite_refresh_cb(void *arg)
{
    const char *a_max = make_atom("max_uses");
    const char *a_used = make_atom("used");

    (void)arg;
    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE || objs[i].parent != INVITE_PARENT)
            continue;
        const char *ms = prop_str(&objs[i], a_max);
        const char *us = prop_str(&objs[i], a_used);
        if (ms && us && atoi(us) >= atoi(ms))
            obj_free(&objs[i]);
    }
    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE || objs[i].parent != ACCT_PARENT)
            continue;
        int is_admin = prop_str(&objs[i], make_atom("admin")) != NULL;
        int target = is_admin ? 1 : USER_CODE_COUNT;
        int uses = is_admin ? ADMIN_CODE_USES : 1;
        int n = invite_count(objs[i].id);
        while (n < target) {
            if (!invite_create(objs[i].id, uses, 0))
                break;
            n++;
        }
    }
    timer_add(INVITE_REFRESH_MS, invite_refresh_cb, NULL);
}

/****************************************************************
 * HTTP helpers
 ****************************************************************/

static int
http_respond(int fd, const char *status, const char *ctype,
         const char *body, int bodylen)
{
    char hdr[512];
    int n;

    n = snprintf(hdr, sizeof(hdr),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n", status, ctype, bodylen);
    if (write(fd, hdr, n) < 0)
        return ERR;
    if (bodylen > 0 && write(fd, body, bodylen) < 0)
        return ERR;
    return OK;
}

static int
serve_file(int fd, const char *path, const char *ctype)
{
    char buf[BUFSIZE * 4];
    int f, n;

    f = open(path, O_RDONLY);
    if (f < 0)
        return http_respond(fd, "404 Not Found",
                    "text/plain", "Not Found", 9);
    n = read(f, buf, sizeof(buf));
    close(f);
    if (n < 0)
        return ERR;
    return http_respond(fd, "200 OK", ctype, buf, n);
}

/****************************************************************
 * Output buffering
 ****************************************************************/

static int
out_flush(int i)
{
    int n;

    if (cc[i].len <= 0)
        return OK;
    n = write(cc[i].fd, cc[i].buf, cc[i].len);
    if (n <= 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? OK : ERR;
    if (n < cc[i].len)
        memmove(cc[i].buf, cc[i].buf + n, cc[i].len - n);
    cc[i].len -= n;
    return OK;
}

static int
out_puts(int i, int cmd, const char *msg)
{
    const char *p, *nl;
    int room, n;

    p = msg;
    do {
        nl = strchr(p, '\n');
        room = BUFSIZE - cc[i].len;
        if (nl) {
            n = snprintf(cc[i].buf + cc[i].len, room,
                     "data: %c%.*s\n", cmd, (int)(nl - p), p);
            p = nl + 1;
        } else {
            n = snprintf(cc[i].buf + cc[i].len, room,
                     "data: %c%s\n\n", cmd, p);
        }
        if (n >= room)
            return ERR;
        cc[i].len += n;
    } while (nl);
    return OK;
}

/****************************************************************
 * Connection management
 ****************************************************************/

static void
conn_close(int i)
{
    if (i < 0 || i >= MAX_CONN || cc[i].fd == 0)
        return;
    if (cc[i].len > 0)
        out_flush(i);
    if (cc[i].regen_timer >= 0)
        timer_remove(cc[i].regen_timer);
    if (cc[i].obj != OBJ_NONE) {
        struct obj *o = obj_find(cc[i].obj);
        if (o)
            obj_free(o);
    }
    close(cc[i].fd);
    memset(&cc[i], 0, sizeof(cc[i]));
    cc[i].obj = OBJ_NONE;
    cc[i].regen_timer = -1;
    cc[i].grit_timer = -1;
}

static int
session_write(int i, const char *msg)
{
    if (i < 0 || i >= MAX_CONN || cc[i].state != CONN_SSE)
        return ERR;
    if (cc[i].len > 0 && out_flush(i) != OK) {
        conn_close(i);
        return ERR;
    }
    if (out_puts(i, 'm', msg) != OK) {
        cc[i].state = CONN_STALLED;
        return ERR;
    }
    return OK;
}

static int
room_broadcast(int from, int room, const char *msg)
{
    int count = 0;

    for (int i = 0; i < MAX_CONN; i++) {
        if (i != from && cc[i].state == CONN_SSE &&
            cc[i].obj != OBJ_NONE) {
            struct obj *o = obj_find(cc[i].obj);
            if (o && prop_objnum(o, make_atom("location")) == room)
                if (session_write(i, msg) == OK)
                    count++;
        }
    }
    return count;
}

/****************************************************************
 * RISC-V RV32 VM
 ****************************************************************/

#define VM_MEMSZ       0x20000
#define VM_ARGS_ADDR   0x380
#define VM_STR_ADDR    0x3B0
#define VM_STR_MAX     208
#define VM_MAX_INSN    100000

struct vm_event {
    int type;
    int tag;
    int player;
    int room;
    int this_obj;
    int dobj;
    int iobj;
    uint32_t argstr;
    uint32_t dobjstr;
    uint32_t iobjstr;
    uint32_t prepstr;
    uint32_t verb;
};

/* A queued event, held host-side (strings included) until the handler reads it,
   so a second event cannot clobber the first's data in VM memory. The mailbox
   is a small per-task ring; overflow drops the oldest and counts it. */
#define MBOX_MAX 4
struct host_event {
    int type, tag;
    int player, room, this_obj, dobj, iobj;
    char argstr[160], verb[32];
};

struct vm {
    rv_cpu cpu;
    uint8_t mem[VM_MEMSZ];
    int sid;
    /* Privilege bracketing (see OLC.md). A verb runs with the caller's
       authority; grant_accept elevates to the verb owner, but only if the verb
       carries the setuid capability. */
    int caller_acct;   /* invoking player's account id (the real authority) */
    int verb_owner;    /* account (or #0) the verb elevates to on grant */
    int can_elevate;   /* verb's elf carries the setuid bit (04000) */
    int elevated;      /* currently running as verb_owner */
    char out[BUFSIZE];
    int outlen;
};

enum { TASK_FREE, TASK_READY, TASK_SLEEPING, TASK_DEAD };

struct vm_task {
    int state;
    int id;
    int timer_id;
    int suspended;
    int handler_obj;               /* object whose events route here, or 0 */
    struct host_event mbox[MBOX_MAX];
    int mbox_head, mbox_count, mbox_drops;
    struct vm vm;
};

static struct vm_task *tasks;
static int task_next_id;
static int task_current = -1;

struct verb_match {
    char hash[65];
    const char *verb;
    int this_obj;
    int verb_obj;
    int dobj;
    int iobj;
    char dobjstr[64];
    char iobjstr[64];
    char prepstr[32];
};
static int  task_alloc(int sid);
static void task_free(int ti);
static void task_wake(void *arg);
static int  task_find_by_id(int id);
static uint32_t vm_put_str(struct vm *vm, uint32_t *off, const char *s);
static int  task_setup(int ti, const char *hash, int player, int room_id,
                       const char *argstr, const struct verb_match *m);
static int  verb_resolve_on(int target, const char *verb, int sid,
                            struct verb_match *m);

static uint32_t
vm_read8(void *ctx, uint32_t addr)
{
    struct vm *vm = ctx;

    return (addr < VM_MEMSZ) ? vm->mem[addr] : 0;
}

static uint32_t
vm_read16(void *ctx, uint32_t addr)
{
    struct vm *vm = ctx;

    if (addr + 1 < VM_MEMSZ)
        return (uint32_t)vm->mem[addr] | ((uint32_t)vm->mem[addr + 1] << 8);
    return 0;
}

static uint32_t
vm_read32(void *ctx, uint32_t addr)
{
    struct vm *vm = ctx;

    if (addr + 3 < VM_MEMSZ)
        return  (uint32_t)vm->mem[addr] |
               ((uint32_t)vm->mem[addr + 1] << 8) |
               ((uint32_t)vm->mem[addr + 2] << 16) |
               ((uint32_t)vm->mem[addr + 3] << 24);
    return 0;
}

static void
vm_write8(void *ctx, uint32_t addr, uint32_t val)
{
    struct vm *vm = ctx;

    if (addr < VM_MEMSZ)
        vm->mem[addr] = val & 0xFF;
}

static void
vm_write16(void *ctx, uint32_t addr, uint32_t val)
{
    struct vm *vm = ctx;

    if (addr + 1 < VM_MEMSZ) {
        vm->mem[addr]     = val & 0xFF;
        vm->mem[addr + 1] = (val >> 8) & 0xFF;
    }
}

static void
vm_write32(void *ctx, uint32_t addr, uint32_t val)
{
    struct vm *vm = ctx;

    if (addr + 3 < VM_MEMSZ) {
        vm->mem[addr]     = val & 0xFF;
        vm->mem[addr + 1] = (val >> 8) & 0xFF;
        vm->mem[addr + 2] = (val >> 16) & 0xFF;
        vm->mem[addr + 3] = (val >> 24) & 0xFF;
    }
}

static uint16_t
rd16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t
rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** Load ELF from memory into VM address space. Returns entry point. */
static uint32_t
elf_load_mem(const uint8_t *data, size_t len, struct vm *vm)
{
    uint32_t entry, phoff;
    uint16_t phnum, phentsz;

    if (len < 52) return 0;
    if (data[0] != 0x7f || data[1] != 'E' ||
        data[2] != 'L' || data[3] != 'F') return 0;
    if (data[4] != 1 || data[5] != 1) return 0;     /* ELFCLASS32, ELFDATA2LSB */
    if (rd16le(data + 16) != 2) return 0;            /* ET_EXEC */
    if (rd16le(data + 18) != 243) return 0;          /* EM_RISCV */

    entry   = rd32le(data + 24);
    phoff   = rd32le(data + 28);
    phentsz = rd16le(data + 42);
    phnum   = rd16le(data + 44);

    for (int i = 0; i < phnum; i++) {
        const uint8_t *ph = data + phoff + (uint32_t)i * phentsz;
        uint32_t p_type, p_offset, p_vaddr, p_filesz, p_memsz;

        if (ph + phentsz > data + len) break;
        p_type   = rd32le(ph);
        p_offset = rd32le(ph + 4);
        p_vaddr  = rd32le(ph + 8);
        p_filesz = rd32le(ph + 16);
        p_memsz  = rd32le(ph + 20);

        if (p_type != 1) continue;
        if (p_offset + p_filesz > (uint32_t)len) return 0;

        for (uint32_t j = 0; j < p_filesz; j++)
            if (p_vaddr + j < VM_MEMSZ)
                vm->mem[p_vaddr + j] = data[p_offset + j];
        for (uint32_t j = p_filesz; j < p_memsz; j++)
            if (p_vaddr + j < VM_MEMSZ)
                vm->mem[p_vaddr + j] = 0;
    }

    return entry;
}

static int obj_match_name(const char *name, int sid);

static int
vm_read_str(struct vm *vm, uint32_t addr, char *buf, int bufsz)
{
    int i = 0;

    while (i < bufsz - 1 && addr + (uint32_t)i < VM_MEMSZ) {
        char c = vm->mem[addr + i];
        if (!c) break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

/* ECALL handler. The syscall number is in a7 and the arguments follow the
 * standard RISC-V ILP32 psABI: integer/pointer args in a0-a5, the return
 * value in a0. ARG(n) reads the nth argument register (a0 = x10), RET() sets
 * the return register. A 64-bit argument occupies an aligned register pair. */
/* The account a running verb currently acts as: the caller by default, or the
 * verb owner while elevated (grant_accept). Authority #0 is the System Object,
 * which carries wizard power; *sys is set for it. A non-#0 authority resolves
 * to its account object (NULL if none, in which case only world bits apply). */
static struct obj *
vm_authority(struct vm *vm, int *sys)
{
    int who = vm->elevated ? vm->verb_owner : vm->caller_acct;

    *sys = (who == 0);
    return (who > 0) ? obj_find(who) : NULL;
}

/* The connected player whose object is `objid`, or -1. Used to spot a player
   walking into a room, which wakes the room's reactive NPCs (OLC-4). */
static int
sid_of_player_obj(int objid)
{
    if (objid == OBJ_NONE)
        return -1;
    for (int i = 0; i < MAX_CONN; i++)
        if (cc[i].state == CONN_SSE && cc[i].obj == objid)
            return i;
    return -1;
}

/* Enqueue an event on task ti's mailbox (OLC-5) and wake it if it is waiting.
   A full mailbox drops the oldest and counts it, so a slow agent degrades
   rather than blocking a producer. */
static void
mbox_push(int ti, const struct host_event *e)
{
    struct vm_task *t = &tasks[ti];
    int slot;

    if (t->mbox_count >= MBOX_MAX) {
        t->mbox_head = (t->mbox_head + 1) % MBOX_MAX;
        t->mbox_count--;
        t->mbox_drops++;
    }
    slot = (t->mbox_head + t->mbox_count) % MBOX_MAX;
    t->mbox[slot] = *e;
    t->mbox_count++;
    if (t->state == TASK_SLEEPING) {
        if (t->timer_id >= 0) { timer_remove(t->timer_id); t->timer_id = -1; }
        t->state = TASK_READY;
    }
}

/* The live agent task listening on object `obj` (via sys_listen), or -1. */
static int
handler_task_of(int obj)
{
    struct obj *o = obj_find(obj);
    int ti;

    if (!o || o->task_id < 0)
        return -1;
    ti = task_find_by_id(o->task_id);
    if (ti < 0) {
        o->task_id = -1;
        return -1;
    }
    return ti;
}

/* OLC-4 reactive mob behavior: when a player enters `room`, run each resident
   NPC's `on_enter` verb as its own fire-and-forget task, bound with this = the
   NPC and player/dobj = the entering player. Only objects that carry a
   `behavior` property (their own or inherited from a prototype) are woken, so
   ordinary items and players in the room are skipped. The behavior verb runs at
   the entering player's authority and elevates through its own setuid bit; a
   task spawned from it (an aggro NPC starting a fight) inherits this room, so
   the fight opens in the right place. */
static void
mob_enter(int intruder, int room, int sid)
{
    const char *a_beh = make_atom("behavior");

    for (int i = 0; i < MAX_OBJ; i++) {
        struct verb_match m;
        int mob = objs[i].id, ti;

        if (mob == OBJ_NONE || mob == intruder)
            continue;
        if (prop_objnum(&objs[i], make_atom("location")) != room)
            continue;
        if (!prop_str(&objs[i], a_beh))
            continue;
        if (sid_of_player_obj(mob) >= 0)   /* a player is not a mob */
            continue;
        /* a live agent listening on this mob gets an EV_ENTER event (OLC-5);
           otherwise fall back to spawning its on_enter verb (OLC-4). */
        ti = handler_task_of(mob);
        if (ti >= 0) {
            struct host_event e;

            memset(&e, 0, sizeof(e));
            e.type = EV_ENTER;
            e.player = intruder;
            e.room = room;
            e.this_obj = mob;
            e.dobj = intruder;
            mbox_push(ti, &e);
            continue;
        }
        if (verb_resolve_on(mob, "on_enter", sid, &m) != OK)
            continue;
        m.dobj = intruder;
        m.iobj = OBJ_NONE;
        ti = task_alloc(sid);
        if (ti < 0)
            break;
        if (task_setup(ti, m.hash, intruder, room, "", &m) != OK)
            task_free(ti);
    }
}

static int
vm_ecall(struct rv_cpu *cpu, void *ctx)
{
    struct vm *vm = ctx;
    int nr = (int)rv_get_x(cpu, 17);            /* a7 */

#define ARG(n) rv_get_x(cpu, 10 + (n))          /* a0..a5 */
#define RET(v) rv_set_x(cpu, 10, (uint32_t)(v)) /* a0 */

    switch (nr) {
    case 0: /* sys_exit */
        cpu->halted = 1;
        return 0;
    case 4: { /* sys_write(fd, buf, len) */
        uint32_t addr = ARG(1);
        int len = (int)ARG(2);
        int avail = BUFSIZE - vm->outlen;

        if (len > avail) len = avail;
        for (int i = 0; i < len; i++)
            if (addr + (uint32_t)i < VM_MEMSZ)
                vm->out[vm->outlen++] = vm->mem[addr + i];
        RET(len);
        return 0;
    }
    case 6: { /* sys_broadcast(room, msg) → count */
        int room = (int)ARG(0);
        char msg[256];

        vm_read_str(vm, ARG(1), msg, sizeof(msg));
        RET(room_broadcast(-1, room, msg));
        return 0;
    }
    case 7: { /* sys_getprop(obj, name, buf, bufsz) */
        int objid = (int)ARG(0);
        uint32_t buf_addr = ARG(2);
        int bufsz = (int)ARG(3);
        char name[64];
        struct obj *o;
        const char *val;
        int vlen;

        vm_read_str(vm, ARG(1), name, sizeof(name));
        o = obj_find(objid);
        if (!o) {
            RET(-E_INVARG);
            return 0;
        }
        val = prop_str(o, make_atom(name));
        if (!val) {
            RET(-E_PROPNF);
            return 0;
        }
        vlen = strlen(val);
        if (vlen > bufsz) vlen = bufsz;
        for (int i = 0; i < vlen; i++)
            if (buf_addr + (uint32_t)i < VM_MEMSZ)
                vm->mem[buf_addr + i] = val[i];
        RET(vlen);
        return 0;
    }
    case 8: { /* sys_setprop(obj, name, val) */
        int objid = (int)ARG(0);
        char name[64], val[256];
        struct obj *o;

        vm_read_str(vm, ARG(1), name, sizeof(name));
        vm_read_str(vm, ARG(2), val, sizeof(val));
        o = obj_find(objid);
        if (!o) {
            RET(-E_INVARG);
            return 0;
        }
        /* owner and group are struct fields, set via @chown/@chgrp, not
           writable as ordinary properties (matches @set). */
        if (prop_is_reserved(name)) {
            RET(-E_PERM);
            return 0;
        }
        int sysverb;
        struct obj *va = vm_authority(vm, &sysverb);
        const char *atom = make_atom(name);
        struct prop *p = NULL;
        for (int i = 0; i < o->nprops; i++)
            if (o->props[i].name == atom) { p = &o->props[i]; break; }
        /* A verb writes with its owner's authority. Existing properties honor
           the per-property write bits; creating a new property requires owning
           the object. System verbs (owned by #0) carry wizard authority. */
        if (p) {
            if (!sysverb && !acct_can_write(va, o, p)) {
                RET(-E_PERM);
                return 0;
            }
        } else if (!sysverb && !acct_is_wizard(va) &&
                   !acct_owner_match(o, va)) {
            RET(-E_PERM);
            return 0;
        }
        prop_set(o, atom, val_str(val));
        RET(0);
        return 0;
    }
    case 9: { /* sys_objfind(name) → objid */
        char name[64];

        vm_read_str(vm, ARG(0), name, sizeof(name));
        RET(obj_match_name(name, vm->sid));
        return 0;
    }
    case 10: { /* sys_spawn(hash, delay_ms, argstr) → task_id */
        int delay = (int)ARG(1);
        char hash[65], spawn_args[256];
        int ti;

        vm_read_str(vm, ARG(0), hash, sizeof(hash));
        vm_read_str(vm, ARG(2), spawn_args, sizeof(spawn_args));

        ti = task_alloc(vm->sid);
        if (ti < 0) {
            RET(-1);
            return 0;
        }

        if (task_setup(ti, hash,
                       vm_read32(vm, VM_ARGS_ADDR),
                       vm_read32(vm, VM_ARGS_ADDR + 4),
                       spawn_args, NULL) != OK) {
            task_free(ti);
            RET(-1);
            return 0;
        }
        /* A spawned task inherits the spawner's authority and privilege state,
           so a verb's background work keeps the same rights it had at spawn and
           a player's verb cannot escalate by spawning. */
        tasks[ti].vm.caller_acct = vm->caller_acct;
        tasks[ti].vm.verb_owner = vm->verb_owner;
        tasks[ti].vm.can_elevate = vm->can_elevate;
        tasks[ti].vm.elevated = vm->elevated;

        if (delay > 0) {
            tasks[ti].state = TASK_SLEEPING;
            tasks[ti].timer_id = timer_add(delay, task_wake,
                                           (void *)(intptr_t)ti);
        }

        RET(tasks[ti].id);
        return 0;
    }
    case 11: { /* sys_suspend(delay_ms) */
        int delay = (int)ARG(0);
        int ti = task_current;

        if (ti < 0) {
            RET(-1);
            return 0;
        }

        tasks[ti].suspended = 1;
        if (delay > 0) {
            tasks[ti].state = TASK_SLEEPING;
            tasks[ti].timer_id = timer_add(delay, task_wake,
                                           (void *)(intptr_t)ti);
        } else if (delay < 0) {
            tasks[ti].state = TASK_SLEEPING;
        }
        /* delay == 0: yield, stays TASK_READY */
        cpu->halted = 1;
        RET(0);
        return 0;
    }
    case 12: { /* sys_random(max) → uniform int in [0,max) */
        int max = (int)ARG(0);
        unsigned char rb[4];
        uint32_t r;

        if (max <= 0 || rand_bytes(rb, 4) != OK) {
            RET(-E_INVARG);
            return 0;
        }
        r = ((uint32_t)rb[0] << 24) | ((uint32_t)rb[1] << 16) |
            ((uint32_t)rb[2] << 8) | (uint32_t)rb[3];
        RET(r % (uint32_t)max);
        return 0;
    }
    case 13: { /* sys_move(obj, dest) → set obj.location = dest (obj ref) */
        int objid = (int)ARG(0);
        int dest = (int)ARG(1);
        struct obj *o = obj_find(objid);
        int sysverb;
        struct obj *va;

        if (!o || !obj_find(dest)) {
            RET(-E_INVARG);
            return 0;
        }
        /* Moving an object is a write to its location, gated like creating a
           property on it: the effective account must own it (or be a wizard, or
           a #0-owned system verb). A verb that moves objects it does not own
           (buy, get/put, combat) is setuid and elevates first. */
        va = vm_authority(vm, &sysverb);
        if (!sysverb && !acct_is_wizard(va) && !acct_owner_match(o, va)) {
            RET(-E_PERM);
            return 0;
        }
        prop_set(o, make_atom("location"), val_obj(dest));
        /* a player walking into a room wakes its reactive NPCs (OLC-4) */
        {
            int esid = sid_of_player_obj(objid);

            if (esid >= 0)
                mob_enter(objid, dest, esid);
        }
        RET(0);
        return 0;
    }
    case 14: { /* sys_next(container, after) → next child obj id > after, or 0 */
        int container = (int)ARG(0);
        int after = (int)ARG(1);
        const char *a_loc = make_atom("location");
        int best = 0;

        for (int i = 0; i < MAX_OBJ; i++) {
            int id = objs[i].id;

            if (id == OBJ_NONE || id <= after) continue;
            if (prop_objnum(&objs[i], a_loc) != container) continue;
            if (best == 0 || id < best) best = id;
        }
        RET(best);
        return 0;
    }
    case 15: { /* sys_rollup(obj, field) → sum of field over the subtree */
        int objid = (int)ARG(0);
        char field[64];

        vm_read_str(vm, ARG(1), field, sizeof(field));
        if (!obj_find(objid)) {
            RET(-E_INVARG);
            return 0;
        }
        RET(rollup_of(objid, make_atom(field)));
        return 0;
    }
    case 16: { /* sys_create(parent) → new persistent obj id */
        int parent = (int)ARG(0);
        struct obj *pobj = obj_find(parent);
        struct obj *o, *acct;
        int newid;

        if (!pobj || obj_is_ephemeral(parent)) {
            RET(-E_INVARG);
            return 0;
        }
        newid = 0;
        while (newid < OBJ_EPH_BASE && obj_find(newid))
            newid++;
        if (newid >= OBJ_EPH_BASE) {
            RET(-E_INVARG);
            return 0;
        }
        o = obj_create(newid, parent);
        if (!o) {
            RET(-E_INVARG);
            return 0;
        }
        acct = player_acct(vm->sid);
        if (acct)
            o->owner = acct->id;
        RET(newid);
        return 0;
    }
    case 17: { /* sys_recycle(obj) → 0, or negative on error/denied */
        int objid = (int)ARG(0);
        struct obj *o = obj_find(objid);

        if (!o || objid == 0 || obj_is_ephemeral(objid)) {
            RET(-E_INVARG);
            return 0;
        }
        /* Recycle with the verb owner's authority: wizard (system verb or an
           admin-owned verb) or the owner of the object being recycled. */
        int sysverb;
        struct obj *va = vm_authority(vm, &sysverb);
        if (!sysverb && !acct_is_wizard(va) && !acct_owner_match(o, va)) {
            RET(-E_PERM);
            return 0;
        }
        if (obj_has_dependents(objid)) {   /* would orphan children/contents */
            RET(-E_INVARG);
            return 0;
        }
        obj_free(o);
        RET(0);
        return 0;
    }
    case 18: { /* sys_call(target, verb, argstr, dobj, iobj) → run on target */
        int target = (int)ARG(0);
        char verb[64], argstr[256];
        struct verb_match m;
        int player = (int)vm_read32(vm, VM_ARGS_ADDR);
        int room = (int)vm_read32(vm, VM_ARGS_ADDR + 4);
        int dobj = (int)ARG(3);
        int iobj = (int)ARG(4);
        int ti;

        vm_read_str(vm, ARG(1), verb, sizeof(verb));
        vm_read_str(vm, ARG(2), argstr, sizeof(argstr));
        if (!obj_find(target)) {
            RET(-E_INVARG);
            return 0;
        }
        if (verb_resolve_on(target, verb, vm->sid, &m) != OK) {
            RET(-E_VERBNF);
            return 0;
        }
        m.dobj = dobj;
        m.iobj = iobj;
        ti = task_alloc(vm->sid);
        if (ti < 0) {
            RET(-1);
            return 0;
        }
        /* fire-and-forget: the verb runs as its own task with this=target,
         * and the caller's object arguments delivered as dobj/iobj */
        if (task_setup(ti, m.hash, player, room, argstr, &m) != OK) {
            task_free(ti);
            RET(-1);
            return 0;
        }
        RET(0);
        return 0;
    }
    case 19: { /* sys_hasverb(target, verb) → 1 if callable, else 0 */
        int target = (int)ARG(0);
        char verb[64];
        struct verb_match m;

        vm_read_str(vm, ARG(1), verb, sizeof(verb));
        if (!obj_find(target)) {
            RET(0);
            return 0;
        }
        RET(verb_resolve_on(target, verb, vm->sid, &m) == OK ? 1 : 0);
        return 0;
    }
    case 20: { /* sys_setpriv(on): raise to / drop from the verb owner's
                * authority. Raising needs the setuid capability. */
        if (ARG(0)) {
            if (!vm->can_elevate) {
                RET(-E_PERM);
                return 0;
            }
            vm->elevated = 1;
        } else {
            vm->elevated = 0;
        }
        RET(0);
        return 0;
    }
    case 21: { /* sys_getmsg(buf, len) → event type, or WAIT_TIMEOUT if the
                * mailbox is empty. Non-blocking: an agent blocks with
                * sys_suspend and pops here on wake (OLC-5). Event strings are
                * marshalled into the VM string area (VM_STR_ADDR), the same
                * region as the boot args, so an agent must not rely on
                * vm_args string pointers after its first getmsg. */
        uint32_t buf = ARG(0);
        int len = (int)ARG(1);
        int ti = task_current;
        struct vm_task *t;
        struct host_event *he;
        struct vm_event ev;
        uint32_t str_off = 0;

        if (ti < 0) { RET(-1); return 0; }
        t = &tasks[ti];
        if (t->mbox_count <= 0) { RET(-2); return 0; }   /* WAIT_TIMEOUT */
        he = &t->mbox[t->mbox_head];
        t->mbox_head = (t->mbox_head + 1) % MBOX_MAX;
        t->mbox_count--;

        memset(&ev, 0, sizeof(ev));
        ev.type = he->type;
        ev.tag = he->tag;
        ev.player = he->player;
        ev.room = he->room;
        ev.this_obj = he->this_obj;
        ev.dobj = he->dobj;
        ev.iobj = he->iobj;
        ev.argstr = vm_put_str(vm, &str_off, he->argstr);
        ev.verb = vm_put_str(vm, &str_off, he->verb);
        {
            int sz = (int)sizeof(ev);
            const uint8_t *src = (const uint8_t *)&ev;

            if (len < sz) sz = len;
            for (int i = 0; i < sz; i++)
                if (buf + (uint32_t)i < VM_MEMSZ)
                    vm->mem[buf + i] = src[i];
        }
        RET(he->type);
        return 0;
    }
    case 22: { /* sys_listen(objid): route objid's events to this task (OLC-5) */
        int objid = (int)ARG(0);
        struct obj *o = obj_find(objid);
        int ti = task_current;

        if (!o || ti < 0) { RET(-1); return 0; }
        o->task_id = tasks[ti].id;
        tasks[ti].handler_obj = objid;
        RET(0);
        return 0;
    }
    }
    return -1;

#undef ARG
#undef RET
}

static void
vm_flush_output(struct vm *vm)
{
    char *p = vm->out, *end = vm->out + vm->outlen, *nl;

    while (p < end) {
        nl = memchr(p, '\n', end - p);
        if (nl) {
            *nl = '\0';
            if (nl > p)
                session_write(vm->sid, p);
            p = nl + 1;
        } else {
            char tmp[BUFSIZE];
            int len = end - p;

            if (len > (int)sizeof(tmp) - 1)
                len = sizeof(tmp) - 1;
            memcpy(tmp, p, len);
            tmp[len] = '\0';
            if (len > 0)
                session_write(vm->sid, tmp);
            break;
        }
    }
}

/****************************************************************
 * Task Scheduling
 ****************************************************************/

static void
task_init(void)
{
    tasks = calloc(MAX_TASK, sizeof(*tasks));
    if (!tasks) { perror("calloc tasks"); exit(1); }
    for (int i = 0; i < MAX_TASK; i++)
        tasks[i].timer_id = -1;
}

static void
task_shutdown(void)
{
    free(tasks);
    tasks = NULL;
}

static int
task_alloc(int sid)
{
    for (int i = 0; i < MAX_TASK; i++) {
        if (tasks[i].state == TASK_FREE) {
            memset(&tasks[i], 0, sizeof(tasks[i]));
            tasks[i].state = TASK_DEAD;
            tasks[i].id = task_next_id++;
            tasks[i].timer_id = -1;
            tasks[i].vm.sid = sid;
            return i;
        }
    }
    return -1;
}

static void
task_free(int ti)
{
    int id = tasks[ti].id;

    /* clear an agent's sys_listen registration (OLC-5) */
    if (tasks[ti].handler_obj) {
        struct obj *o = obj_find(tasks[ti].handler_obj);
        if (o && o->task_id == id)
            o->task_id = -1;
    }
    if (tasks[ti].timer_id >= 0)
        timer_remove(tasks[ti].timer_id);
    tasks[ti].state = TASK_FREE;
    tasks[ti].timer_id = -1;
}

static void
task_wake(void *arg)
{
    int ti = (int)(intptr_t)arg;

    if (ti >= 0 && ti < MAX_TASK && tasks[ti].state == TASK_SLEEPING) {
        tasks[ti].state = TASK_READY;
        tasks[ti].timer_id = -1;
    }
}

static void
task_step(int ti, int quantum)
{
    struct vm_task *t = &tasks[ti];

    t->suspended = 0;
    t->vm.cpu.halted = 0;
    t->vm.outlen = 0;
    task_current = ti;
    rv_run(&t->vm.cpu, quantum);
    vm_flush_output(&t->vm);
    task_current = -1;

    if (t->suspended) {
        /* sys_suspend set the state already */
    } else if (t->vm.cpu.halted) {
        task_free(ti);
    }
    /* else: preempted, stays TASK_READY */
}

static void
task_run_all(void)
{
    for (int i = 0; i < MAX_TASK; i++) {
        if (tasks[i].state == TASK_FREE)
            continue;
        int sid = tasks[i].vm.sid;
        if (sid < 0 || sid >= MAX_CONN || cc[sid].state == CONN_FREE) {
            task_free(i);
            continue;
        }
        if (tasks[i].state == TASK_READY)
            task_step(i, TASK_QUANTUM);
    }
}


/** Write string into VM string area; returns VM address or 0. */
static uint32_t
vm_put_str(struct vm *vm, uint32_t *off, const char *s)
{
    int len;
    uint32_t addr;

    if (!s || !*s) return 0;
    len = strlen(s);
    if (*off + (uint32_t)len + 1 > VM_STR_MAX) return 0;
    addr = VM_STR_ADDR + *off;
    memcpy(vm->mem + addr, s, len + 1);
    *off += len + 1;
    return addr;
}

static int
task_setup(int ti, const char *hash, int player, int room_id,
           const char *argstr, const struct verb_match *m)
{
    struct vm *vm = &tasks[ti].vm;
    struct cas_file cf;
    uint32_t entry, str_off = 0;

    /* Set up privilege bracketing. The verb starts at the caller's authority;
       grant_accept can raise it to the verb owner if the verb's elf carries the
       setuid bit. A task spawned from a bare hash (sys_spawn, m == NULL)
       inherits the spawner's state, which sys_spawn copies after this returns. */
    {
        struct obj *ca = player_acct(vm->sid);
        struct obj *vobj = m ? obj_find(m->verb_obj) : NULL;

        vm->caller_acct = ca ? ca->id : OBJ_NONE;
        vm->verb_owner = vobj ? vobj->owner : OBJ_NONE;
        vm->can_elevate = 0;
        vm->elevated = 0;
        if (vobj) {
            const char *ev = prop_str(vobj, make_atom("elf"));
            int mode; char h[65];
            if (ev && elf_parse(ev, &mode, h, sizeof(h)) == OK)
                vm->can_elevate = (mode & 04000) != 0;
        }
    }

    if (cas_open(cas_store, &cf, hash) != CAS_OK) return ERR;

    entry = elf_load_mem(cf.data, cf.len, vm);
    cas_close(&cf);
    if (entry == 0) return ERR;

    vm_write32(vm, VM_ARGS_ADDR, player);
    vm_write32(vm, VM_ARGS_ADDR + 4, room_id);
    vm_write32(vm, VM_ARGS_ADDR + 8,
               vm_put_str(vm, &str_off, argstr));
    vm_write32(vm, VM_ARGS_ADDR + 12,
               argstr ? (uint32_t)strlen(argstr) : 0);

    if (m) {
        vm_write32(vm, VM_ARGS_ADDR + 16, m->this_obj);
        vm_write32(vm, VM_ARGS_ADDR + 20, m->dobj);
        vm_write32(vm, VM_ARGS_ADDR + 24, m->iobj);
        vm_write32(vm, VM_ARGS_ADDR + 28,
                   vm_put_str(vm, &str_off, m->dobjstr));
        vm_write32(vm, VM_ARGS_ADDR + 32,
                   vm_put_str(vm, &str_off, m->iobjstr));
        vm_write32(vm, VM_ARGS_ADDR + 36,
                   vm_put_str(vm, &str_off, m->prepstr));
        vm_write32(vm, VM_ARGS_ADDR + 40,
                   vm_put_str(vm, &str_off, m->verb));
    }

    rv_init(&vm->cpu, vm_read8, vm_read16, vm_read32,
            vm_write8, vm_write16, vm_write32, vm);
    rv_set_ecall(&vm->cpu, vm_ecall, vm);
    rv_reset(&vm->cpu, entry);
    /* RV has no memory reset vector for the stack; the verb's _start runs C
     * immediately, so seed sp (x2) at the top of guest RAM. */
    rv_set_x(&vm->cpu, 2, VM_MEMSZ);

    tasks[ti].state = TASK_READY;
    return OK;
}

static int
vm_exec(int sid, const char *hash, int player, int room_id,
        const char *argstr, const struct verb_match *m)
{
    int ti = task_alloc(sid);

    if (ti < 0) return ERR;
    if (task_setup(ti, hash, player, room_id, argstr, m) != OK) {
        task_free(ti);
        return ERR;
    }
    task_step(ti, VM_MAX_INSN);
    return OK;
}

/****************************************************************
 * Verb argument matching — typed slot system
 ****************************************************************/

static int player_room(int sid);

enum { SLOT_OBJ, SLOT_TEXT, SLOT_PLAYER, SLOT_EXIT, SLOT_ANY, SLOT_ATOM };

struct arg_slot {
    int type;
    char atom[32];
};

#define MAX_SLOTS 8

static int
slot_parse_one(const char *tok, struct arg_slot *s)
{
    if (tok[0] != '<') {
        s->type = SLOT_ATOM;
        strncpy(s->atom, tok, sizeof(s->atom) - 1);
        s->atom[sizeof(s->atom) - 1] = '\0';
        return OK;
    }
    const char *p = tok + 1, *eq = strchr(p, '=');
    const char *gt = strchr(p, '>');
    if (!gt) return ERR;

    int tlen = (eq && eq < gt) ? (int)(eq - p) : (int)(gt - p);
    char tname[16];
    if (tlen >= (int)sizeof(tname)) return ERR;
    memcpy(tname, p, tlen);
    tname[tlen] = '\0';

    if (strcmp(tname, "obj") == 0) s->type = SLOT_OBJ;
    else if (strcmp(tname, "text") == 0) s->type = SLOT_TEXT;
    else if (strcmp(tname, "player") == 0) s->type = SLOT_PLAYER;
    else if (strcmp(tname, "exit") == 0) s->type = SLOT_EXIT;
    else if (strcmp(tname, "any") == 0) s->type = SLOT_ANY;
    else if (strcmp(tname, "atom") == 0) s->type = SLOT_ATOM;
    else return ERR;

    s->atom[0] = '\0';
    if (eq && eq < gt) {
        int alen = (int)(gt - eq - 1);
        if (alen >= (int)sizeof(s->atom)) alen = sizeof(s->atom) - 1;
        memcpy(s->atom, eq + 1, alen);
        s->atom[alen] = '\0';
    }
    return OK;
}

static int
argspec_parse(const char *spec, struct arg_slot *slots, int *nslots)
{
    char buf[128], *p;
    *nslots = 0;

    if (!spec || !*spec) return OK;
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    for (p = buf; *p && *nslots < MAX_SLOTS; ) {
        while (isspace(*p)) p++;
        if (!*p) break;
        char *start = p;
        if (*p == '<') {
            p = strchr(p, '>');
            if (!p) return ERR;
            p++;
        } else {
            while (*p && !isspace(*p)) p++;
        }
        char save = *p;
        if (*p) *p++ = '\0';
        if (slot_parse_one(start, &slots[*nslots]) != OK)
            return ERR;
        (*nslots)++;
        if (!save) break;
    }
    return OK;
}

static int
atom_match(const struct arg_slot *s, const char *word)
{
    const char *p = s->atom, *sep;
    int wlen = strlen(word);

    while (*p) {
        sep = strchr(p, '|');
        int len = sep ? (int)(sep - p) : (int)strlen(p);
        if (len == wlen && strncasecmp(p, word, len) == 0)
            return 1;
        p = sep ? sep + 1 : p + len;
    }
    return 0;
}

static int
obj_match_player(const char *name, int sid)
{
    const char *a_name = make_atom("name");
    (void)sid;

    for (int i = 0; i < MAX_CONN; i++) {
        if (cc[i].fd == 0 || cc[i].obj == OBJ_NONE) continue;
        struct obj *o = obj_find(cc[i].obj);
        if (!o) continue;
        const char *n = prop_str(o, a_name);
        if (n && strcasecmp(n, name) == 0)
            return cc[i].obj;
    }
    return OBJ_NONE;
}

/** Resolve an object name in the player's scope. */
static int
obj_match_name(const char *name, int sid)
{
    const char *a_name = make_atom("name");
    const char *a_loc = make_atom("location");
    int player = cc[sid].obj;
    int room = player_room(sid);
    int found_room = OBJ_NONE;

    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE) continue;
        const char *n = prop_str(&objs[i], a_name);
        if (!n || strcasecmp(n, name) != 0) continue;
        int loc = prop_objnum(&objs[i], a_loc);
        if (loc == player) return objs[i].id;
        if (loc == room && found_room == OBJ_NONE)
            found_room = objs[i].id;
    }
    if (found_room != OBJ_NONE) return found_room;

    struct obj *r = obj_find(room);
    if (r) {
        const char *n = prop_str(r, a_name);
        if (n && strcasecmp(n, name) == 0) return room;
    }
    struct obj *sys = obj_find(0);
    if (sys) {
        const char *n = prop_str(sys, a_name);
        if (n && strcasecmp(n, name) == 0) return 0;
    }

    /* Lowest priority: reach one level into an open container (cap > 0) or a
     * dead body resting in the room or the player's own hands. This lets you
     * name an item inside a satchel or strip gear off a fallen body without
     * opening it first (get/drop/put slice, chromesix-smolmoo.md Section 9). */
    const char *a_cap = make_atom("cap");
    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE) continue;
        int loc = prop_objnum(&objs[i], a_loc);
        if (loc != player && loc != room) continue;
        if (prop_int(&objs[i], a_cap, 0) <= 0) {
            struct obj *sh = player_sheet(objs[i].id);
            if (!sh || !prop_int(sh, "dead", 0)) continue;
        }
        for (int j = 0; j < MAX_OBJ; j++) {
            if (objs[j].id == OBJ_NONE) continue;
            if (prop_objnum(&objs[j], a_loc) != objs[i].id) continue;
            const char *n = prop_str(&objs[j], a_name);
            if (n && strcasecmp(n, name) == 0) return objs[j].id;
        }
    }
    return OBJ_NONE;
}

static int
obj_match_exit(const char *name, int sid)
{
    const char *a_name = make_atom("name");
    const char *a_loc = make_atom("location");
    int room = player_room(sid);

    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE) continue;
        if (prop_objnum(&objs[i], a_loc) != room) continue;
        const char *n = prop_str(&objs[i], a_name);
        if (n && strcasecmp(n, name) == 0)
            return objs[i].id;
    }
    return OBJ_NONE;
}

static int
slot_resolve(const struct arg_slot *s, const char *text, int sid)
{
    switch (s->type) {
    case SLOT_OBJ:    return obj_match_name(text, sid);
    case SLOT_PLAYER: return obj_match_player(text, sid);
    case SLOT_EXIT:   return obj_match_exit(text, sid);
    default:          return OBJ_NONE;
    }
}

/** Try matching input against a slot sequence. Returns OK if matched. */
static int
slots_match(const struct arg_slot *slots, int nslots,
            const char *rest, int sid, struct verb_match *m)
{
    const char *p = rest;
    int typed = 0;

    for (int si = 0; si < nslots; si++) {
        while (*p && isspace(*p)) p++;

        if (slots[si].type == SLOT_ATOM) {
            const char *wend = p;
            while (*wend && !isspace(*wend)) wend++;
            int wlen = wend - p;
            char word[32];
            if (wlen <= 0 || wlen >= (int)sizeof(word)) return ERR;
            memcpy(word, p, wlen);
            word[wlen] = '\0';
            if (!atom_match(&slots[si], word)) return ERR;
            if (typed == 1) {
                int off = strlen(m->prepstr);
                if (off && off < (int)sizeof(m->prepstr) - 1)
                    m->prepstr[off++] = ' ';
                if (off + wlen < (int)sizeof(m->prepstr)) {
                    memcpy(m->prepstr + off, word, wlen);
                    m->prepstr[off + wlen] = '\0';
                }
            }
            p = wend;
            continue;
        }

        if (slots[si].type == SLOT_TEXT || slots[si].type == SLOT_ANY) {
            while (*p && isspace(*p)) p++;
            char *dst = (typed == 0) ? m->dobjstr : m->iobjstr;
            int sz = (typed == 0) ?
                (int)sizeof(m->dobjstr) : (int)sizeof(m->iobjstr);
            strncpy(dst, p, sz - 1);
            dst[sz - 1] = '\0';
            typed++;
            p += strlen(p);
            continue;
        }

        const char *boundary = p + strlen(p);
        for (int ni = si + 1; ni < nslots; ni++) {
            if (slots[ni].type != SLOT_ATOM) continue;
            const char *scan = p;
            while (*scan) {
                while (*scan && isspace(*scan)) scan++;
                const char *we = scan;
                while (*we && !isspace(*we)) we++;
                int wl = we - scan;
                char w[32];
                if (wl > 0 && wl < (int)sizeof(w)) {
                    memcpy(w, scan, wl);
                    w[wl] = '\0';
                    if (atom_match(&slots[ni], w)) {
                        boundary = scan;
                        goto found_boundary;
                    }
                }
                scan = we;
            }
            break;
        }
found_boundary:;

        int tlen = boundary - p;
        while (tlen > 0 && isspace(p[tlen - 1])) tlen--;
        if (tlen <= 0) return ERR;

        char *dst = (typed == 0) ? m->dobjstr : m->iobjstr;
        int sz = (typed == 0) ?
            (int)sizeof(m->dobjstr) : (int)sizeof(m->iobjstr);
        if (tlen >= sz) tlen = sz - 1;
        memcpy(dst, p, tlen);
        dst[tlen] = '\0';

        int *id = (typed == 0) ? &m->dobj : &m->iobj;
        *id = slot_resolve(&slots[si], dst, sid);
        if (*id == OBJ_NONE) return ERR;

        typed++;
        p = boundary;
    }

    while (*p && isspace(*p)) p++;
    if (*p && !nslots) return ERR;
    return OK;
}

/** Match verb + args against verb definitions in scope. */
static int
verb_resolve(int sid, const char *verb, const char *rest,
             struct verb_match *m)
{
    const char *a_verb = make_atom("verb");
    const char *a_elf = make_atom("elf");
    const char *a_args = make_atom("args");
    const char *a_loc = make_atom("location");
    struct obj *pobj, *sys;

    int chain[] = { cc[sid].obj, OBJ_NONE, OBJ_NONE, 0 };
    pobj = obj_find(cc[sid].obj);
    if (pobj)
        chain[1] = prop_objnum(pobj, a_loc);
    sys = obj_find(0);
    if (sys)
        chain[2] = prop_objnum(sys, a_verb);

    for (int ci = 0; ci < 4; ci++) {
        int link = chain[ci];
        if (link == OBJ_NONE) continue;

        for (int ancestor = link, depth = 0;
             ancestor != OBJ_NONE && depth < MAX_OBJ; depth++) {
            for (int i = 0; i < MAX_OBJ; i++) {
                if (objs[i].id == OBJ_NONE) continue;
                if (objs[i].parent != ancestor) continue;

                const char *n = prop_str(&objs[i], a_verb);
                if (!n || strcmp(n, verb) != 0) continue;

                const char *elf = prop_str(&objs[i], a_elf);
                int elf_mode;
                char elf_hash[65];
                if (!elf || elf_parse(elf, &elf_mode, elf_hash,
                    sizeof(elf_hash)) != OK)
                    continue;

                const char *spec = prop_str(&objs[i], a_args);
                struct arg_slot slots[MAX_SLOTS];
                int nslots = 0;
                if (spec && *spec) {
                    if (argspec_parse(spec, slots, &nslots) != OK)
                        continue;
                }

                struct verb_match tm;
                memset(&tm, 0, sizeof(tm));
                tm.verb = verb;
                tm.dobj = OBJ_NONE;
                tm.iobj = OBJ_NONE;
                tm.this_obj = ancestor;

                if (nslots == 0 && *rest)
                    continue;
                if (nslots > 0 &&
                    slots_match(slots, nslots, rest, sid, &tm) != OK)
                    continue;

                if (!perm_can_exec_elf(sid, &objs[i], elf_mode))
                    continue;
                *m = tm;
                memcpy(m->hash, elf_hash, 65);
                m->this_obj = ancestor;
                m->verb_obj = objs[i].id;
                return OK;
            }
            struct obj *ao = obj_find(ancestor);
            ancestor = ao ? ao->parent : OBJ_NONE;
        }
    }

    return ERR;
}

/* Resolve a verb by name for a `target:verb()` call. Verbs are objects named
 * by their `verb` property; a game's verbs live under the `#0.verb` prototype,
 * and object-specific verbs under the target's own parent chain. Both are
 * searched, child-first. On a match the invoking player must hold execute
 * permission. `this` is bound to the target, not the ancestor that defines the
 * verb, so the called verb sees the object it was called on. */
static int
verb_resolve_on(int target, const char *verb, int sid, struct verb_match *m)
{
    const char *a_verb = make_atom("verb");
    const char *a_elf = make_atom("elf");
    struct obj *sys = obj_find(0);
    int chain[] = { target, OBJ_NONE };

    if (sys)
        chain[1] = prop_objnum(sys, a_verb);

    for (int ci = 0; ci < 2; ci++) {
        int link = chain[ci];
        if (link == OBJ_NONE) continue;

        for (int ancestor = link, depth = 0;
             ancestor != OBJ_NONE && depth < MAX_OBJ; depth++) {
            for (int i = 0; i < MAX_OBJ; i++) {
                if (objs[i].id == OBJ_NONE) continue;
                if (objs[i].parent != ancestor) continue;

                const char *n = prop_str(&objs[i], a_verb);
                if (!n || strcmp(n, verb) != 0) continue;

                const char *elf = prop_str(&objs[i], a_elf);
                int elf_mode;
                char elf_hash[65];
                if (!elf || elf_parse(elf, &elf_mode, elf_hash,
                    sizeof(elf_hash)) != OK)
                    continue;
                if (!perm_can_exec_elf(sid, &objs[i], elf_mode))
                    continue;

                memset(m, 0, sizeof(*m));
                m->verb = verb;
                m->this_obj = target;
                m->dobj = OBJ_NONE;
                m->iobj = OBJ_NONE;
                m->verb_obj = objs[i].id;
                memcpy(m->hash, elf_hash, 65);
                return OK;
            }
            struct obj *ao = obj_find(ancestor);
            ancestor = ao ? ao->parent : OBJ_NONE;
        }
    }

    return ERR;
}

/****************************************************************
 * Commands
 ****************************************************************/

static const char *
player_name(int sid)
{
    struct obj *o = obj_find(cc[sid].obj);

    return o ? prop_str(o, make_atom("name")) : NULL;
}

static int
player_room(int sid)
{
    struct obj *o = obj_find(cc[sid].obj);

    return o ? prop_objnum(o, make_atom("location")) : OBJ_NONE;
}

static void grit_schedule_regen(int sid);

static void
status_update(int sid)
{
    char status[256];
    int room_id, gmax;
    struct obj *room_obj, *po;
    const char *room_name;

    if (cc[sid].obj == OBJ_NONE)
        return;

    room_id = player_room(sid);
    room_obj = obj_find(room_id);
    room_name = room_obj ? prop_str(room_obj, make_atom("name")) : "Unknown";

    /* Grit's store of record is the creature's `grit` property; refresh the
     * live mirror from it so a verb that spent Grit shows immediately. */
    po = player_sheet(cc[sid].obj);
    gmax = grit_max(po);
    cc[sid].grit = prop_int(po, "grit", gmax);
    if (cc[sid].grit > gmax) cc[sid].grit = gmax;

    snprintf(status, sizeof(status), "%s | Fuel: %d/%d | Grit: %d/%d",
             room_name, cc[sid].fuel, FUEL_MAX, cc[sid].grit, gmax);
    out_puts(sid, '=', status);

    if (cc[sid].grit < gmax)
        grit_schedule_regen(sid);
}

static void
player_act(int sid, int kind, const char *args1, const char *args2)
{
    const char *name = player_name(sid) ?: "Someone";
    char buf[BUFSIZE];
    int room = player_room(sid);

    switch (kind) {
    case 0: /* say */
        snprintf(buf, sizeof(buf), "You say \"%s\"", args1);
        session_write(sid, buf);
        snprintf(buf, sizeof(buf), "%s says \"%s\"", name, args1);
        break;
    case 1: /* emote */
    case 2: /* emote nospace */
        snprintf(buf, sizeof(buf), kind == 1 ? "%s %s" : "%s%s",
                 name, args1);
        session_write(sid, buf);
        break;
    case 3: /* quit */
        session_write(sid, args1);
        snprintf(buf, sizeof(buf), "%s %s", name, args2);
        break;
    }
    room_broadcast(sid, room, buf);
}

static void
cmd_say(int sid, const char *args)
{
    if (!*args) {
        session_write(sid, "Say what?");
        return;
    }
    player_act(sid, 0, args, args);
}

static int
task_find_by_id(int id)
{
    for (int i = 0; i < MAX_TASK; i++)
        if (tasks[i].state != TASK_FREE && tasks[i].id == id)
            return i;
    return -1;
}

static int
verb_dispatch(int sid, const char *verb, const char *args)
{
    struct verb_match m;

    if (verb_resolve(sid, verb, args, &m) != OK)
        return ERR;

    return vm_exec(sid, m.hash, cc[sid].obj, player_room(sid), args, &m);
}

/* General command help lives on the #0.help object, one topic per property, so
 * it is editable in-game and viewable through the wiki/editor (see help.md).
 * `help` lists the topics; `help <topic>` prints one. A static summary covers
 * the case where the help object is missing (an old or hand-built depot). */
static void
cmd_help(int sid, const char *args)
{
    struct obj *sys = obj_find(0);
    int hid = sys ? prop_objnum(sys, make_atom("help")) : OBJ_NONE;
    struct obj *h = (hid != OBJ_NONE) ? obj_find(hid) : NULL;
    char topic[32];
    int n = 0;

    while (*args == ' ') args++;
    while (args[n] && !isspace((unsigned char)args[n]) &&
           n < (int)sizeof(topic) - 1) {
        topic[n] = args[n];
        n++;
    }
    topic[n] = '\0';

    if (h && topic[0]) {
        const char *v = prop_str(h, make_atom(topic));
        if (v) session_write(sid, v);
        else session_write(sid,
            "No help on that topic. Type 'help' for the list.");
        return;
    }
    if (h) {
        char line[512];
        int len = 0;

        session_write(sid, "Help topics (type 'help <topic>'):");
        for (int i = 0; i < h->nprops; i++) {
            const char *nm = h->props[i].name;
            int nl;

            if (strcmp(nm, "name") == 0 || strcmp(nm, "description") == 0)
                continue;
            nl = strlen(nm);
            if (len && len + 2 + nl >= (int)sizeof(line)) {
                session_write(sid, line);
                len = 0;
            }
            if (len) { line[len++] = ' '; line[len++] = ' '; }
            memcpy(line + len, nm, nl);
            len += nl;
            line[len] = '\0';
        }
        if (len) session_write(sid, line);
        return;
    }

    session_write(sid, "Commands: say \"  emote :  whisper  page");
    session_write(sid, "          look  help  quit");
    session_write(sid, "Building: @create @set @examine @edit @view");
    session_write(sid, "          @chmod @chown @chgrp");
    session_write(sid, "Groups:   @group create|add|remove|list");
    session_write(sid, "Feedback: @gripe @typo @bug @idea @suggest @comment");
}

static void
cmd_quit(int sid, const char *args)
{
    (void)args;
    player_act(sid, 3, "Goodbye!", "has disconnected.");
}

static void
cmd_emote(int sid, const char *args)
{
    if (!*args) {
        session_write(sid, "Emote what?");
        return;
    }

    player_act(sid, 1, args, args);
}

static void
cmd_emote2(int sid, const char *args)
{
    if (!*args) {
        session_write(sid, "Emote what?");
        return;
    }

    player_act(sid, 2, args, args);
}

static int
find_player_by_name(const char *name, int room)
{
    for (int i = 0; i < MAX_CONN; i++) {
        if (cc[i].state != CONN_SSE || cc[i].obj == OBJ_NONE)
            continue;
        const char *pn = player_name(i);
        if (!pn || strcasecmp(pn, name) != 0)
            continue;
        if (room == OBJ_NONE || player_room(i) == room)
            return i;
    }
    return -1;
}

static void
cmd_tell(int sid, const char *args, const char *verb,
         int room, const char *nofind)
{
    char buf[BUFSIZE], who[64];
    const char *p = args;
    int n = 0, target;

    while (*p && !isspace(*p) && n < (int)sizeof(who) - 1)
        who[n++] = *p++;
    who[n] = '\0';
    while (isspace(*p))
        p++;
    if (!*who || !*p) {
        snprintf(buf, sizeof(buf), "Usage: %s <player> <message>",
                 verb);
        session_write(sid, buf);
        return;
    }
    target = find_player_by_name(who, room);
    if (target < 0) {
        session_write(sid, nofind);
        return;
    }
    snprintf(buf, sizeof(buf), "%s %ss, \"%s\"",
             player_name(sid), verb, p);
    session_write(target, buf);
    snprintf(buf, sizeof(buf), "You %s, \"%s\" to %s.", verb, p,
             player_name(target));
    session_write(sid, buf);
}

static void
cmd_whisper(int sid, const char *args)
{
    cmd_tell(sid, args, "whisper", player_room(sid),
             "You don't see that person here.");
}

static void
cmd_page(int sid, const char *args)
{
    cmd_tell(sid, args, "page", OBJ_NONE,
             "That person is not connected.");
}

/* @program support: compile MooScript source into a verb ELF and store it in
 * the CAS. The SDK toolchain (next to the server binary) and vm_rv.ld (in the
 * working directory) must be present at runtime. On success returns OK with
 * the b2 hash in hash_out (at least 65 bytes); on a compiler error, sends the
 * diagnostics to the session and returns ERR. The compile runs synchronously,
 * briefly blocking the single-threaded server. */
static int
program_compile(int sid, const char *src, char *hash_out, int is_c)
{
    char sdk[512], path[640], cmd[8192];
    ssize_t n;
    FILE *f;
    char *sl;
    long len;
    unsigned char *data;

    n = readlink("/proc/self/exe", sdk, sizeof(sdk) - 5);
    if (n <= 0) {
        session_write(sid, "@program: cannot locate the SDK.");
        return ERR;
    }
    sdk[n] = '\0';
    sl = strrchr(sdk, '/');
    if (!sl) {
        session_write(sid, "@program: SDK path error.");
        return ERR;
    }
    strcpy(sl + 1, "sdk");

    snprintf(path, sizeof(path), "%s/_prog.%s", depot_dir,
             is_c ? "c" : "moo");
    f = fopen(path, "w");
    if (!f) {
        session_write(sid, "@program: cannot write temp source.");
        return ERR;
    }
    fwrite(src, 1, strlen(src), f);
    fputc('\n', f);
    fclose(f);

    if (is_c)
        /* C verbs: skj-cc-rv-psabi (register psABI) + verb_rt_rv stubs. The
         * header comes from the SDK dir (bundled mulibc.h) or the repo (-I.).*/
        snprintf(cmd, sizeof(cmd),
            "%s/skj-cc-rv-psabi -I%s -I. -o %s/_prog.s %s/_prog.c "
            "2>%s/_prog.err && "
            "%s/skj-as-rv -o %s/_prog.o %s/_prog.s 2>>%s/_prog.err && "
            "%s/skj-ld-rv -T vm_rv.ld -o %s/_prog.elf %s/_prog.o "
            "%s/verb_rt_rv.o 2>>%s/_prog.err",
            sdk, sdk, depot_dir, depot_dir, depot_dir,
            sdk, depot_dir, depot_dir, depot_dir,
            sdk, depot_dir, depot_dir, sdk, depot_dir);
    else
        snprintf(cmd, sizeof(cmd),
            "%s/skj-mooc-rv -o %s/_prog.s %s/_prog.moo 2>%s/_prog.err && "
            "%s/skj-as-rv -o %s/_prog.o %s/_prog.s 2>>%s/_prog.err && "
            "%s/skj-ld-rv -T vm_rv.ld -o %s/_prog.elf %s/_prog.o "
            "%s/moo_rt.o %s/host_vm.o %s/str.o %s/list.o %s/moo_syscall_rv.o "
            "2>>%s/_prog.err",
            sdk, depot_dir, depot_dir, depot_dir,
            sdk, depot_dir, depot_dir, depot_dir,
            sdk, depot_dir, depot_dir,
            sdk, sdk, sdk, sdk, sdk, depot_dir);

    if (system(cmd) != 0) {
        char err[1024];
        size_t rd = 0;

        snprintf(path, sizeof(path), "%s/_prog.err", depot_dir);
        f = fopen(path, "r");
        if (f) {
            rd = fread(err, 1, sizeof(err) - 1, f);
            fclose(f);
        }
        err[rd] = '\0';
        session_write(sid, "@program: compile failed:");
        if (rd)
            session_write(sid, err);
        return ERR;
    }

    snprintf(path, sizeof(path), "%s/_prog.elf", depot_dir);
    f = fopen(path, "rb");
    if (!f) {
        session_write(sid, "@program: missing compiled output.");
        return ERR;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    rewind(f);
    data = malloc(len > 0 ? (size_t)len : 1);
    if (!data || len <= 0 || fread(data, 1, len, f) != (size_t)len) {
        free(data);
        fclose(f);
        session_write(sid, "@program: read error.");
        return ERR;
    }
    fclose(f);
    if (cas_put(cas_store, data, len, hash_out) != CAS_OK) {
        free(data);
        session_write(sid, "@program: CAS store failed.");
        return ERR;
    }
    free(data);
    return OK;
}

/* Read the current history head address from head_file, race-free (the
   writer thread updates it atomically). Returns 1 with addr filled, or 0
   if there is no head yet. */
static int
history_head_addr(char *addr)
{
    FILE *f = fopen(head_file, "r");

    addr[0] = '\0';
    if (!f)
        return 0;
    if (fgets(addr, CAS_HASH_HEX + 2, f))
        addr[strcspn(addr, "\r\n")] = '\0';
    fclose(f);
    return addr[0] ? 1 : 0;
}

/* ---- depot garbage collection (M30) --------------------------------
 *
 * The signed history keeps every save's root, and every changed object or
 * directory page leaves its old CAS version behind, so the depot grows
 * without bound. history_gc marks every object reachable from the retained
 * version records (the newest `keep`) plus the live root, then removes any
 * unmarked object of a type whose reachability it fully enumerates: world
 * objects, object-map pages and roots, and version records. Verb ELFs
 * (type "blob") and anything else are never touched, since their
 * reachability is not enumerated here. Pruning old version records falls
 * out of the same sweep: an unretained record is simply left unmarked. */

#define GC_KEEP_DEFAULT 128     /* version records retained by default */

struct markset {
    char (*h)[CAS_HASH_HEX + 1];
    int n, cap, failed;
};

static void
mark_add(struct markset *m, const char *hash)
{
    if (m->n == m->cap) {
        int cap = m->cap ? m->cap * 2 : 512;
        void *p = realloc(m->h, (size_t)cap * sizeof(*m->h));
        if (!p) { m->failed = 1; return; }
        m->h = p;
        m->cap = cap;
    }
    memcpy(m->h[m->n++], hash, CAS_HASH_HEX + 1);
}

static int
mark_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static int
mark_has(const struct markset *m, const char *hash)
{
    return bsearch(hash, m->h, (size_t)m->n, sizeof(*m->h),
                   mark_cmp) != NULL;
}

static int
gc_mark_obj(uint64_t id, const char *hash, void *ctx)
{
    (void)id;
    mark_add(ctx, hash);
    return 0;
}

static int
gc_mark_page(const char *hash, void *ctx)
{
    mark_add(ctx, hash);
    return 0;
}

struct gc_walk {
    struct markset *mark;
    char (*roots)[CAS_HASH_HEX + 1];
    int nroots, caproots, keep, seen, failed;
};

static void
gc_add_root(struct gc_walk *g, const char *root)
{
    for (int i = 0; i < g->nroots; i++)
        if (strcmp(g->roots[i], root) == 0)
            return;
    if (g->nroots == g->caproots) {
        int cap = g->caproots ? g->caproots * 2 : 64;
        void *p = realloc(g->roots, (size_t)cap * sizeof(*g->roots));
        if (!p) { g->failed = 1; return; }
        g->roots = p;
        g->caproots = cap;
    }
    memcpy(g->roots[g->nroots++], root, CAS_HASH_HEX + 1);
}

static int
gc_walk_cb(const struct cas_vrec *v, const char *addr, void *ctx)
{
    struct gc_walk *g = ctx;
    mark_add(g->mark, addr);        /* keep this version record */
    gc_add_root(g, v->root);
    return ++g->seen >= g->keep;    /* stop after `keep` records */
}

struct gc_sweep {
    const struct markset *mark;
    struct markset *dead;
    time_t now;
    int grace;
    int kept;
};

static int
gc_sweep_cb(const char *hash, void *ctx)
{
    struct gc_sweep *s = ctx;
    struct cas_file cf;
    char type[CAS_TYPE_MAX];
    int sweepable;
    time_t mt;

    if (mark_has(s->mark, hash)) { s->kept++; return 0; }

    if (cas_open_object(cas_store, &cf, hash, type,
                        sizeof(type)) != CAS_OK) {
        s->kept++;
        return 0;
    }
    sweepable = strcmp(type, "obj") == 0 || strcmp(type, "opage") == 0 ||
                strcmp(type, "omap") == 0 || strcmp(type, "vrec") == 0;
    cas_close(&cf);
    if (!sweepable) { s->kept++; return 0; }

    if (s->grace > 0 && cas_object_mtime(cas_store, hash, &mt) == CAS_OK &&
        s->now - mt < s->grace) { s->kept++; return 0; }

    mark_add(s->dead, hash);        /* defer removal past the iteration */
    return 0;
}

/* Collect depot garbage. Reports to `sid` if >= 0, else only to stderr. */
static void
history_gc(int sid, int keep)
{
    char head[CAS_HASH_HEX + 2];
    char live[CAS_HASH_HEX + 1];
    struct markset mark = {0};
    struct markset dead = {0};
    struct gc_walk g = {0};
    struct gc_sweep sw;
    struct cas_omap *scratch;
    char msg[128];
    int removed = 0;

    if (!signing_on) {
        if (sid >= 0)
            session_write(sid, "World history is not enabled.");
        return;
    }
    if (keep < 1)
        keep = 1;

    saver_wait_idle();      /* no save in flight while we mark and sweep */

    g.mark = &mark;
    g.keep = keep;
    if (history_head_addr(head))
        cas_vchain_walk(cas_store, head, 0, gc_walk_cb, &g);
    if (root_read(live) == OK)
        gc_add_root(&g, live);

    scratch = cas_omap_new(cas_store);
    for (int i = 0; i < g.nroots; i++) {
        mark_add(&mark, g.roots[i]);    /* the object-map root object */
        if (scratch && cas_omap_load(scratch, g.roots[i]) == CAS_OK) {
            cas_omap_foreach(scratch, gc_mark_obj, &mark);
            cas_omap_foreach_page(scratch, gc_mark_page, &mark);
        }
    }
    if (scratch)
        cas_omap_free(scratch);

    /* An incomplete mark set could delete a live object, so bail rather
       than sweep if any mark allocation failed. */
    if (mark.failed || g.failed) {
        if (sid >= 0)
            session_write(sid, "@gc aborted: out of memory.");
        fprintf(stderr, "[gc] aborted: out of memory\n");
        free(mark.h);
        free(g.roots);
        return;
    }

    qsort(mark.h, (size_t)mark.n, sizeof(*mark.h), mark_cmp);

    sw.mark = &mark;
    sw.dead = &dead;
    sw.now = time(NULL);
    sw.grace = 0;           /* saves are quiesced, so age is not needed */
    sw.kept = 0;
    cas_foreach(cas_store, gc_sweep_cb, &sw);

    for (int i = 0; i < dead.n; i++)
        if (cas_remove(cas_store, dead.h[i]) == CAS_OK)
            removed++;

    snprintf(msg, sizeof(msg),
             "[gc] kept %d, removed %d objects (%d versions retained)",
             sw.kept, removed, g.seen);
    if (sid >= 0)
        session_write(sid, msg);
    fprintf(stderr, "%s\n", msg);

    free(mark.h);
    free(dead.h);
    free(g.roots);
}

/* ---- depot integrity check (@fsck) ---------------------------------
 *
 * Read-only verification. The content pass recomputes every stored object's
 * address from its bytes and flags any that no longer hash to their name
 * (corruption or bit rot). The reachability pass confirms the live world's
 * objects, object-map pages, and the verb ELFs its objects reference are all
 * present (no dangling references). The chain pass walks the signed version
 * history, which verifies each record's signature and linkage as it goes. */

struct fsck_ctx {
    int checked;        /* objects examined in the content pass */
    int corrupt;        /* content does not hash to its address */
    int unreadable;     /* object could not be opened at all */
    int missing;        /* a referenced object is absent */
};

static int
fsck_content_cb(const char *hash, void *ctx)
{
    struct fsck_ctx *f = ctx;
    struct cas_file cf;
    char type[CAS_TYPE_MAX];
    char computed[CAS_HASH_HEX + 1];

    f->checked++;
    if (cas_open_object(cas_store, &cf, hash, type,
                        sizeof(type)) != CAS_OK) {
        f->unreadable++;
        return 0;
    }
    if (cas_hash_object(type, cf.data, cf.len, computed) != CAS_OK ||
        strcmp(computed, hash) != 0)
        f->corrupt++;
    cas_close(&cf);
    return 0;
}

/* Confirm each verb ELF referenced by a serialized object (spelled b2:<hex>)
   is present in the store. */
static void
fsck_scan_refs(const char *data, size_t len, struct fsck_ctx *f)
{
    size_t i;

    if (len < (size_t)(3 + CAS_HASH_HEX))
        return;
    for (i = 0; i + 3 + CAS_HASH_HEX <= len; i++) {
        if (data[i] != 'b' || data[i + 1] != '2' || data[i + 2] != ':')
            continue;
        char ref[CAS_HASH_HEX + 1];
        int ok = 1, k;
        for (k = 0; k < CAS_HASH_HEX; k++) {
            char c = data[i + 3 + k];
            if (!isxdigit((unsigned char)c)) { ok = 0; break; }
            ref[k] = c;
        }
        if (!ok)
            continue;
        ref[CAS_HASH_HEX] = '\0';
        if (!cas_exists(cas_store, ref))
            f->missing++;
        i += 3 + CAS_HASH_HEX - 1;
    }
}

static int
fsck_reach_cb(uint64_t id, const char *hash, void *ctx)
{
    struct fsck_ctx *f = ctx;
    struct cas_file cf;
    char type[CAS_TYPE_MAX];

    (void)id;
    if (!cas_exists(cas_store, hash)) {
        f->missing++;
        return 0;
    }
    if (cas_open_object(cas_store, &cf, hash, type,
                        sizeof(type)) == CAS_OK) {
        fsck_scan_refs((const char *)cf.data, cf.len, f);
        cas_close(&cf);
    }
    return 0;
}

static int
fsck_page_cb(const char *hash, void *ctx)
{
    struct fsck_ctx *f = ctx;

    if (!cas_exists(cas_store, hash))
        f->missing++;
    return 0;
}

static int
fsck_chain_cb(const struct cas_vrec *v, const char *addr, void *ctx)
{
    (void)v;
    (void)addr;
    (void)ctx;
    return 0;
}

static void
history_fsck(int sid)
{
    struct fsck_ctx f = {0, 0, 0, 0};
    char live[CAS_HASH_HEX + 1];
    char head[CAS_HASH_HEX + 2];
    char msg[192];
    const char *chain;
    const char *root_state = "ok";
    int rc;

    saver_wait_idle();          /* verify a stable snapshot */

    /* content pass: every stored object hashes to its own address */
    cas_foreach(cas_store, fsck_content_cb, &f);

    /* reachability pass: the live world resolves with no dangling refs */
    if (root_read(live) == OK) {
        struct cas_omap *scratch = cas_omap_new(cas_store);

        if (scratch && cas_omap_load(scratch, live) == CAS_OK) {
            cas_omap_foreach(scratch, fsck_reach_cb, &f);
            cas_omap_foreach_page(scratch, fsck_page_cb, &f);
        } else {
            root_state = "UNREADABLE";
        }
        if (scratch)
            cas_omap_free(scratch);
    }

    /* chain pass: the signed history verifies end to end */
    if (!history_head_addr(head)) {
        chain = "none";
    } else {
        rc = cas_vchain_walk(cas_store, head, 0, fsck_chain_cb, NULL);
        chain = rc == CAS_OK ? "verified" :
                rc == CAS_SIGN_EINCOMPLETE ? "verified (older records pruned)" :
                "BROKEN";
    }

    snprintf(msg, sizeof(msg),
             "[fsck] %d objects: %d corrupt, %d unreadable; "
             "live root %s, %d missing refs; history %s",
             f.checked, f.corrupt, f.unreadable, root_state, f.missing,
             chain);
    if (sid >= 0)
        session_write(sid, msg);
    fprintf(stderr, "%s\n", msg);
}

struct hist_ctx {
    int sid;
    char live[CAS_HASH_HEX + 1];
    int shown;
    int limit;
};

static int
hist_cb(const struct cas_vrec *v, const char *addr, void *ctx)
{
    struct hist_ctx *h = ctx;
    char line[160], when[32];
    time_t t = (time_t)v->timestamp;
    struct tm tmv;

    (void)addr;
    localtime_r(&t, &tmv);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &tmv);
    snprintf(line, sizeof(line), "  %-4llu %s  %.12s%s",
             (unsigned long long)v->seq, when, v->root,
             strcmp(v->root, h->live) == 0 ? "  (live)" : "");
    session_write(h->sid, line);
    return ++h->shown >= h->limit;
}

/* @history: list the signed version chain, newest first, marking which
   record names the world's current live root. */
static void
cmd_history(int sid)
{
    char head[CAS_HASH_HEX + 2];
    struct hist_ctx h;
    int rc;

    if (!history_head_addr(head)) {
        session_write(sid, "No world history yet.");
        return;
    }
    h.sid = sid;
    h.shown = 0;
    h.limit = 50;
    if (root_read(h.live) != OK)
        h.live[0] = '\0';

    session_write(sid, "World history (newest first):");
    session_write(sid, "  seq  when              root");
    rc = cas_vchain_walk(cas_store, head, 0, hist_cb, &h);
    if (rc == CAS_SIGN_EINCOMPLETE)
        session_write(sid, "  ... (older records not in this depot)");
}

struct rewind_ctx {
    uint64_t want;
    int found;
    char root[CAS_HASH_HEX + 1];
};

static int
rewind_cb(const struct cas_vrec *v, const char *addr, void *ctx)
{
    struct rewind_ctx *r = ctx;

    (void)addr;
    if (v->seq == r->want) {
        memcpy(r->root, v->root, sizeof(r->root));
        r->found = 1;
        return 1;
    }
    return v->seq < r->want;   /* walked past it: stop */
}

/* @rewind <seq>: wizard-only. Restore the world to the root named by an
   earlier version record. The restore is recorded as a new version so the
   chain stays forward-only, the live persistent world is reloaded in place,
   and every session is disconnected since its ephemeral avatar belongs to
   the pre-rewind world. */
static void
cmd_rewind(int sid, const char *arg)
{
    char head[CAS_HASH_HEX + 2];
    struct rewind_ctx r;
    char *end;
    long seq;
    int count = 0;

    if (!is_wizard(sid)) {
        session_write(sid, "Only a wizard may @rewind.");
        return;
    }
    if (!signing_on) {
        session_write(sid, "World history is not enabled on this server.");
        return;
    }
    seq = strtol(arg, &end, 10);
    if (end == arg || seq < 1) {
        session_write(sid, "Usage: @rewind <seq>  (see @history)");
        return;
    }
    if (!history_head_addr(head)) {
        session_write(sid, "No world history yet.");
        return;
    }

    r.want = (uint64_t)seq;
    r.found = 0;
    if (cas_vchain_walk(cas_store, head, 0, rewind_cb, &r) < 0 &&
        !r.found) {
        session_write(sid, "Could not read the history chain.");
        return;
    }
    if (!r.found) {
        session_write(sid, "No such version. Try @history.");
        return;
    }

    /* Quiesce the writer so nothing is mid-save, then record the rewind as
       the new head before touching the live world. */
    saver_wait_idle();
    history_record(r.root);
    if (root_write(r.root) != OK) {
        session_write(sid, "@rewind failed: could not write root.");
        return;
    }

    fprintf(stderr, "[history] rewinding world to seq %ld (root %.12s)\n",
            seq, r.root);

    /* Disconnect everyone: each session's in-world avatar is ephemeral and
       belongs to the state being replaced. conn_close flushes the notice. */
    for (int i = 0; i < MAX_CONN; i++) {
        if (cc[i].fd) {
            session_write(i, "The world was rolled back by an "
                             "administrator. Please reconnect.");
            conn_close(i);
        }
    }

    /* Replace the live persistent world with the restored root. */
    world_free();
    if (cas_omap_load(obj_map, r.root) != CAS_OK) {
        fprintf(stderr, "[history] FATAL: could not load rewound root\n");
        running = 0;
        return;
    }
    cas_omap_foreach(obj_map, load_one_cb, &count);
    world_bootstrap();
    fprintf(stderr, "[history] rewound to seq %ld: %d objects loaded\n",
            seq, count);
}

/* Resolve an OLC object argument: #N, &N, or a name matched near the caller. */
static int
olc_ref(int sid, const char *s)
{
    while (*s == ' ') s++;
    if (*s == '#') return (int)strtol(s + 1, NULL, 10);
    if (*s == '&') return OBJ_EPH_BASE + (int)strtol(s + 1, NULL, 10);
    if (!*s) return OBJ_NONE;
    return obj_match_name(s, sid);
}

/* Deep-copy a property value for @clone, so the clone owns its own strings.
   List values are not used on world objects and are left for the caller to
   skip. */
static struct value
olc_val_copy(struct value v)
{
    if (v.type == VAL_STR)
        return val_str(v.str ? v.str : "");
    return v;
}

/* Case-insensitive substring test for @find. */
static int
olc_ci_contains(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);

    if (!nl) return 1;
    for (; *hay; hay++)
        if (strncasecmp(hay, needle, nl) == 0) return 1;
    return 0;
}

static void
cmd_feedback(int sid, const char *args)
{
    char tag[16];
    const char *name = player_name(sid);
    int room = player_room(sid);
    struct obj *robj = obj_find(room);
    int owner = robj ? robj->owner : OBJ_NONE;
    const char *p = args;
    int n = 0;

    while (*p && !isspace(*p) && n < (int)sizeof(tag) - 1)
        tag[n++] = *p++;
    tag[n] = '\0';
    while (isspace(*p))
        p++;

    /* Handle @save command. Queue the save on the writer thread, then wait
       for it to drain so the confirmation is truthful. */
    if (strcmp(tag, "save") == 0) {
        world_save_async();
        saver_wait_idle();
        session_write(sid, "World saved.");
        return;
    }

    /* Signed world history (M29): list the chain, or rewind to a version. */
    if (strcmp(tag, "history") == 0) {
        cmd_history(sid);
        return;
    }
    if (strcmp(tag, "rewind") == 0) {
        cmd_rewind(sid, p);
        return;
    }
    if (strcmp(tag, "gc") == 0) {
        int keep = GC_KEEP_DEFAULT;
        if (!is_wizard(sid)) {
            session_write(sid, "Only a wizard may @gc.");
            return;
        }
        if (*p) {
            int k = atoi(p);
            if (k > 0)
                keep = k;
        }
        session_write(sid, "Collecting depot garbage...");
        history_gc(sid, keep);
        return;
    }
    if (strcmp(tag, "fsck") == 0) {
        if (!is_wizard(sid)) {
            session_write(sid, "Only a wizard may @fsck.");
            return;
        }
        session_write(sid, "Checking depot integrity...");
        history_fsck(sid);
        return;
    }

    /* Handle @edit / @view commands */
    if (strcmp(tag, "edit") == 0 || strcmp(tag, "view") == 0) {
        int objid = OBJ_NONE, is_edit = (tag[0] == 'e');
        char objref[32], propname[64], url[256];
        const char *dot;

        dot = strchr(p, '.');
        if (!dot || !dot[1]) {
            session_write(sid,
                "Usage: @edit #N.prop or @view #N.prop");
            return;
        }
        int objlen = dot - p;
        if (objlen >= (int)sizeof(objref)) {
            session_write(sid, "Object reference too long.");
            return;
        }
        memcpy(objref, p, objlen);
        objref[objlen] = '\0';
        if (*objref == '#')
            objid = strtol(objref + 1, NULL, 10);
        else if (*objref == '&')
            objid = OBJ_EPH_BASE + strtol(objref + 1, NULL, 10);
        int proplen = strlen(dot + 1);
        if (proplen >= (int)sizeof(propname)) {
            session_write(sid, "Property name too long.");
            return;
        }
        strcpy(propname, dot + 1);
        if (!obj_find(objid)) {
            session_write(sid, "Object not found.");
            return;
        }
        snprintf(url, sizeof(url), "/%s?obj=%d&prop=%s&sid=%d",
            is_edit ? "edit" : "view", objid, propname, sid);
        out_puts(sid, is_edit ? 'E' : 'V', url);
        out_flush(sid);
        return;
    }

    /* Handle @examine command */
    if (strcmp(tag, "examine") == 0) {
        int objid = OBJ_NONE;
        struct obj *o;
        char buf[256], id1[16], id2[16];

        if (*p == '#')
            objid = strtol(p + 1, NULL, 10);
        else if (*p == '&')
            objid = OBJ_EPH_BASE + strtol(p + 1, NULL, 10);
        else if (!*p)
            objid = room;

        o = obj_find(objid);
        if (!o) {
            session_write(sid, "Object not found.");
            return;
        }

        {
            char id3[16], id4[16];
            if (o->group != OBJ_NONE)
                snprintf(buf, sizeof(buf),
                    "%s (parent: %s, owner: %s, group: %s)",
                    obj_fmt(o->id, id1, sizeof(id1)),
                    obj_fmt(o->parent, id2, sizeof(id2)),
                    obj_fmt(o->owner, id3, sizeof(id3)),
                    obj_fmt(o->group, id4, sizeof(id4)));
            else
                snprintf(buf, sizeof(buf),
                    "%s (parent: %s, owner: %s)",
                    obj_fmt(o->id, id1, sizeof(id1)),
                    obj_fmt(o->parent, id2, sizeof(id2)),
                    obj_fmt(o->owner, id3, sizeof(id3)));
        }
        session_write(sid, buf);

        for (int i = 0; i < o->nprops; i++) {
            struct prop *prop = &o->props[i];
            struct value *val = &prop->val;
            char fb[8];

            if (!perm_can_read(sid, o, prop))
                continue;
            prop_fmt_flags(prop->flags, fb, sizeof(fb));

            switch (val->type) {
            case VAL_OBJ:
                snprintf(buf, sizeof(buf), "  %s%s = %s",
                    prop->name, fb,
                    obj_fmt(val->obj, id1, sizeof(id1)));
                break;
            case VAL_NUM:
                snprintf(buf, sizeof(buf), "  %s%s = %g",
                    prop->name, fb, val->num);
                break;
            case VAL_STR: {
                const char *str = val->str;
                int len = strlen(str);
                if (len > 60) {
                    snprintf(buf, sizeof(buf),
                        "  %s%s = \"%.60s...\"",
                        prop->name, fb, str);
                } else {
                    snprintf(buf, sizeof(buf),
                        "  %s%s = \"%s\"",
                        prop->name, fb, str);
                }
                break;
            }
            case VAL_LIST:
                snprintf(buf, sizeof(buf),
                    "  %s%s = {list, %d items}",
                    prop->name, fb, val->list.len);
                break;
            default:
                continue;
            }
            session_write(sid, buf);
        }
        return;
    }

    /* Handle @set command */
    if (strcmp(tag, "set") == 0) {
        char objref[32], propname[64], propval[256];
        const char *eq, *dot;
        int objid = OBJ_NONE;
        struct obj *o;

        eq = strchr(p, '=');
        if (!eq) {
            session_write(sid,
                "Usage: @set #N.prop=value or &N.prop=value");
            return;
        }

        dot = strchr(p, '.');
        if (!dot || dot > eq) {
            session_write(sid,
                "Usage: @set #N.prop=value or &N.prop=value");
            return;
        }

        int objlen = dot - p;
        if (objlen >= (int)sizeof(objref)) {
            session_write(sid, "Object reference too long.");
            return;
        }
        memcpy(objref, p, objlen);
        objref[objlen] = '\0';

        if (*objref == '#')
            objid = strtol(objref + 1, NULL, 10);
        else if (*objref == '&')
            objid = OBJ_EPH_BASE + strtol(objref + 1, NULL, 10);

        int proplen = eq - (dot + 1);
        if (proplen >= (int)sizeof(propname)) {
            session_write(sid, "Property name too long.");
            return;
        }
        memcpy(propname, dot + 1, proplen);
        propname[proplen] = '\0';

        int vallen = strlen(eq + 1);
        if (vallen >= (int)sizeof(propval)) {
            session_write(sid, "Value too long.");
            return;
        }
        strcpy(propval, eq + 1);

        o = obj_find(objid);
        if (!o) {
            session_write(sid, "Object not found.");
            return;
        }

        if (prop_is_reserved(propname)) {
            session_write(sid, "Cannot set reserved property.");
            return;
        }

        const char *atom_name = make_atom(propname);
        if (!atom_name) {
            session_write(sid, "Property name atom allocation failed.");
            return;
        }

        {
            int found = 0;
            for (int i = 0; i < o->nprops; i++) {
                if (o->props[i].name == atom_name) {
                    found = 1;
                    if (!perm_can_write(sid, o, &o->props[i])) {
                        session_write(sid,
                            "Permission denied.");
                        return;
                    }
                    break;
                }
            }
            if (!found && !is_wizard(sid) &&
                !obj_owner_match(o, sid)) {
                session_write(sid, "Permission denied.");
                return;
            }
        }

        /* Parse value: #N for objects, otherwise string */
        struct value v;
        if (*propval == '#') {
            int ref = strtol(propval + 1, NULL, 10);
            v = val_obj(ref);
        } else {
            v = val_str(propval);
        }

        if (prop_set(o, atom_name, v) != OK) {
            session_write(sid, "Failed to set property.");
            return;
        }

        {
            char idbuf[16];
            snprintf(propval, sizeof(propval),
                "Property %s set on %s.",
                propname, obj_fmt(objid, idbuf, sizeof(idbuf)));
        }
        session_write(sid, propval);
        return;
    }

    /* @chmod #N.prop=rw,r,r */
    if (strcmp(tag, "chmod") == 0) {
        char objref[32], propname[64], flagstr[16];
        const char *eq, *dot;
        int objid = OBJ_NONE;
        struct obj *o;

        eq = strchr(p, '=');
        if (!eq) {
            session_write(sid,
                "Usage: @chmod #N.prop=rw,r,r");
            return;
        }
        dot = strchr(p, '.');
        if (!dot || dot > eq) {
            session_write(sid,
                "Usage: @chmod #N.prop=rw,r,r");
            return;
        }
        {
            int n2 = dot - p;
            if (n2 >= (int)sizeof(objref)) n2 = sizeof(objref) - 1;
            memcpy(objref, p, n2);
            objref[n2] = '\0';
        }
        if (*objref == '#')
            objid = strtol(objref + 1, NULL, 10);
        else if (*objref == '&')
            objid = OBJ_EPH_BASE + strtol(objref + 1, NULL, 10);
        {
            int n2 = eq - (dot + 1);
            if (n2 >= (int)sizeof(propname)) n2 = sizeof(propname) - 1;
            memcpy(propname, dot + 1, n2);
            propname[n2] = '\0';
        }
        strncpy(flagstr, eq + 1, sizeof(flagstr) - 1);
        flagstr[sizeof(flagstr) - 1] = '\0';

        o = obj_find(objid);
        if (!o) {
            session_write(sid, "Object not found.");
            return;
        }
        if (!is_wizard(sid) && !obj_owner_match(o, sid)) {
            session_write(sid, "Permission denied.");
            return;
        }
        {
            const char *atom = make_atom(propname);
            int found = 0;
            for (int i = 0; i < o->nprops; i++) {
                if (o->props[i].name == atom) {
                    o->props[i].flags =
                        prop_parse_flags(flagstr);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                session_write(sid, "Property not found.");
                return;
            }
        }
        {
            char idbuf[16], buf[128];
            snprintf(buf, sizeof(buf),
                "Flags on %s.%s set to %s.",
                obj_fmt(objid, idbuf, sizeof(idbuf)),
                propname, flagstr);
            session_write(sid, buf);
        }
        return;
    }

    /* Handle @chown command: @chown #N=#M or @chown #N=name */
    if (strcmp(tag, "chown") == 0) {
        const char *eq;
        int objid = OBJ_NONE, new_owner = OBJ_NONE;
        struct obj *o;
        char buf[128];

        eq = strchr(p, '=');
        if (!eq) {
            session_write(sid,
                "Usage: @chown #N=#owner");
            return;
        }
        if (*p == '#')
            objid = strtol(p + 1, NULL, 10);
        else if (*p == '&')
            objid = OBJ_EPH_BASE + strtol(p + 1, NULL, 10);
        if (*(eq + 1) == '#')
            new_owner = strtol(eq + 2, NULL, 10);
        else {
            struct obj *acct = acct_find(eq + 1);
            if (acct)
                new_owner = acct->id;
        }

        o = obj_find(objid);
        if (!o) {
            session_write(sid, "Object not found.");
            return;
        }
        if (!is_wizard(sid)) {
            session_write(sid,
                "Permission denied. Only wizards can chown.");
            return;
        }
        o->owner = new_owner;
        {
            char id1[16], id2[16];
            snprintf(buf, sizeof(buf), "Owner of %s set to %s.",
                obj_fmt(objid, id1, sizeof(id1)),
                obj_fmt(new_owner, id2, sizeof(id2)));
        }
        session_write(sid, buf);
        return;
    }

    /* @chgrp #N=#G */
    if (strcmp(tag, "chgrp") == 0) {
        const char *eq;
        int objid = OBJ_NONE, new_group = OBJ_NONE;
        struct obj *o;
        char buf[128];

        eq = strchr(p, '=');
        if (!eq) {
            session_write(sid, "Usage: @chgrp #N=#group");
            return;
        }
        if (*p == '#')
            objid = strtol(p + 1, NULL, 10);
        else if (*p == '&')
            objid = OBJ_EPH_BASE + strtol(p + 1, NULL, 10);
        if (*(eq + 1) == '#')
            new_group = strtol(eq + 2, NULL, 10);

        o = obj_find(objid);
        if (!o) {
            session_write(sid, "Object not found.");
            return;
        }
        if (!is_wizard(sid) && !obj_owner_match(o, sid)) {
            session_write(sid, "Permission denied.");
            return;
        }
        if (new_group != OBJ_NONE) {
            struct obj *g = obj_find(new_group);
            if (!g || g->parent != GROUP_PARENT) {
                session_write(sid, "Not a group object.");
                return;
            }
        }
        o->group = new_group;
        {
            char id1[16], id2[16];
            snprintf(buf, sizeof(buf), "Group of %s set to %s.",
                obj_fmt(objid, id1, sizeof(id1)),
                obj_fmt(new_group, id2, sizeof(id2)));
        }
        session_write(sid, buf);
        return;
    }

    /* @group create|add|remove|list */
    if (strcmp(tag, "group") == 0) {
        char subcmd[16], arg1[64], arg2[64];
        int nargs;

        arg1[0] = arg2[0] = '\0';
        nargs = sscanf(p, "%15s %63s %63s", subcmd, arg1, arg2);
        if (nargs < 1) {
            session_write(sid,
                "Usage: @group create|add|remove|list");
            return;
        }

        if (strcmp(subcmd, "create") == 0) {
            if (!arg1[0]) {
                session_write(sid,
                    "Usage: @group create <name>");
                return;
            }
            int newid = 0;
            while (newid < 1000000000 && obj_find(newid))
                newid++;
            struct obj *g = obj_create(newid, GROUP_PARENT);
            if (!g) {
                session_write(sid, "Failed to create group.");
                return;
            }
            {
                struct obj *acct = player_acct(sid);
                if (acct)
                    g->owner = acct->id;
            }
            prop_set(g, make_atom("name"), val_str(arg1));
            prop_set(g, make_atom("members"), val_str(""));
            {
                char buf[128], id1[16];
                snprintf(buf, sizeof(buf), "Group %s created as %s.",
                    arg1, obj_fmt(newid, id1, sizeof(id1)));
                session_write(sid, buf);
            }
            return;
        }

        if (strcmp(subcmd, "add") == 0 ||
            strcmp(subcmd, "remove") == 0) {
            if (!arg1[0] || !arg2[0]) {
                session_write(sid,
                    "Usage: @group add|remove <group> <player>");
                return;
            }
            struct obj *grp = NULL;
            for (int i = 0; i < MAX_OBJ; i++) {
                if (objs[i].id == OBJ_NONE ||
                    objs[i].parent != GROUP_PARENT)
                    continue;
                const char *gn = prop_str(&objs[i],
                    make_atom("name"));
                if (gn && strcasecmp(gn, arg1) == 0) {
                    grp = &objs[i];
                    break;
                }
            }
            if (!grp) {
                session_write(sid, "Group not found.");
                return;
            }
            if (!is_wizard(sid) && !obj_owner_match(grp, sid)) {
                session_write(sid, "Permission denied.");
                return;
            }
            struct obj *target = acct_find(arg2);
            if (!target) {
                session_write(sid, "Player not found.");
                return;
            }
            char ref[16];
            snprintf(ref, sizeof(ref), "#%d", target->id);
            int rlen = strlen(ref);
            const char *members = prop_str(grp,
                make_atom("members"));
            if (!members) members = "";

            if (strcmp(subcmd, "add") == 0) {
                for (const char *mp = members; *mp; ) {
                    while (*mp == ',' || *mp == ' ') mp++;
                    if (!*mp) break;
                    if (strncmp(mp, ref, rlen) == 0 &&
                        (mp[rlen] == ',' || mp[rlen] == '\0')) {
                        session_write(sid, "Already a member.");
                        return;
                    }
                    while (*mp && *mp != ',') mp++;
                }
                char newm[256];
                if (*members)
                    snprintf(newm, sizeof(newm), "%s,%s",
                        members, ref);
                else
                    snprintf(newm, sizeof(newm), "%s", ref);
                prop_set(grp, make_atom("members"),
                    val_str(newm));
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf),
                        "%s added to group %s.", arg2, arg1);
                    session_write(sid, buf);
                }
            } else {
                char newm[256];
                int nw = 0;
                for (const char *mp = members; *mp; ) {
                    while (*mp == ',' || *mp == ' ') mp++;
                    if (!*mp) break;
                    const char *start = mp;
                    while (*mp && *mp != ',') mp++;
                    int slen = mp - start;
                    if (slen == rlen &&
                        strncmp(start, ref, rlen) == 0)
                        continue;
                    if (nw > 0 && nw < (int)sizeof(newm) - 1)
                        newm[nw++] = ',';
                    if (nw + slen < (int)sizeof(newm) - 1) {
                        memcpy(newm + nw, start, slen);
                        nw += slen;
                    }
                }
                newm[nw] = '\0';
                prop_set(grp, make_atom("members"),
                    val_str(newm));
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf),
                        "%s removed from group %s.",
                        arg2, arg1);
                    session_write(sid, buf);
                }
            }
            return;
        }

        if (strcmp(subcmd, "list") == 0) {
            char buf[256];
            if (arg1[0]) {
                struct obj *grp = NULL;
                for (int i = 0; i < MAX_OBJ; i++) {
                    if (objs[i].id == OBJ_NONE ||
                        objs[i].parent != GROUP_PARENT)
                        continue;
                    const char *gn = prop_str(&objs[i],
                        make_atom("name"));
                    if (gn && strcasecmp(gn, arg1) == 0) {
                        grp = &objs[i];
                        break;
                    }
                }
                if (!grp) {
                    session_write(sid, "Group not found.");
                    return;
                }
                const char *members = prop_str(grp,
                    make_atom("members"));
                snprintf(buf, sizeof(buf), "Group %s: %s",
                    arg1, members ?: "(empty)");
                session_write(sid, buf);
            } else {
                session_write(sid, "Groups:");
                for (int i = 0; i < MAX_OBJ; i++) {
                    if (objs[i].id == OBJ_NONE ||
                        objs[i].parent != GROUP_PARENT)
                        continue;
                    const char *gn = prop_str(&objs[i],
                        make_atom("name"));
                    char id1[16];
                    snprintf(buf, sizeof(buf), "  %s  %s",
                        obj_fmt(objs[i].id, id1, sizeof(id1)),
                        gn ?: "(unnamed)");
                    session_write(sid, buf);
                }
            }
            return;
        }

        session_write(sid,
            "Usage: @group create|add|remove|list");
        return;
    }

    /* Handle @create command */
    if (strcmp(tag, "create") == 0) {
        int parent = OBJ_NONE;
        struct obj *o;
        char buf[256];

        if (*p == '#')
            parent = strtol(p + 1, NULL, 10);

        if (parent == OBJ_NONE) {
            session_write(sid,
                "Usage: @create #parent");
            return;
        }

        struct obj *pobj = obj_find(parent);
        if (!pobj) {
            session_write(sid, "Parent object not found.");
            return;
        }

        /* Find next available persistent object ID */
        int newid = 0;
        while (newid < 1000000000 && obj_find(newid))
            newid++;

        if (newid >= 1000000000) {
            session_write(sid,
                "No more object IDs available.");
            return;
        }

        o = obj_create(newid, parent);
        if (!o) {
            session_write(sid, "Failed to create object.");
            return;
        }
        {
            struct obj *acct = acct_find(player_name(sid));
            if (acct)
                o->owner = acct->id;
        }

        {
            char id1[16], id2[16];
            snprintf(buf, sizeof(buf), "Created %s with parent %s.",
                obj_fmt(newid, id1, sizeof(id1)),
                obj_fmt(parent, id2, sizeof(id2)));
        }
        session_write(sid, buf);
        return;
    }

    /* @recycle #N : destroy an object you own (a wizard may destroy any). */
    if (strcmp(tag, "recycle") == 0) {
        int id = olc_ref(sid, p);
        struct obj *o = obj_find(id);
        char b[64], i1[16];

        if (!o || id == 0 || obj_is_ephemeral(id)) {
            session_write(sid, "Usage: @recycle #N");
            return;
        }
        if (!is_wizard(sid) && !obj_owner_match(o, sid)) {
            session_write(sid, "You don't own that.");
            return;
        }
        if (obj_has_dependents(id)) {
            session_write(sid,
                "That still has children or contents; empty it first.");
            return;
        }
        snprintf(b, sizeof(b), "Recycled %s.",
                 obj_fmt(id, i1, sizeof(i1)));
        obj_free(o);
        session_write(sid, b);
        return;
    }

    /* @clone #N : make a copy of an object, owned by you. */
    if (strcmp(tag, "clone") == 0) {
        int srcid = olc_ref(sid, p);
        struct obj *src = obj_find(srcid), *o;
        struct obj *acct;
        int newid;
        char b[64], i1[16], i2[16];

        if (!src || srcid == 0 || obj_is_ephemeral(srcid)) {
            session_write(sid, "Usage: @clone #N");
            return;
        }
        newid = 0;
        while (newid < OBJ_EPH_BASE && obj_find(newid))
            newid++;
        if (newid >= OBJ_EPH_BASE) {
            session_write(sid, "No more object IDs available.");
            return;
        }
        o = obj_create(newid, src->parent);
        if (!o) {
            session_write(sid, "Failed to create object.");
            return;
        }
        for (int i = 0; i < src->nprops; i++) {
            if (src->props[i].val.type == VAL_LIST)
                continue;   /* not used on world objects */
            prop_set(o, src->props[i].name,
                     olc_val_copy(src->props[i].val));
            /* preserve the source's permission flags, so a secret property
               does not become world-readable on the copy. */
            for (int k = 0; k < o->nprops; k++)
                if (o->props[k].name == src->props[i].name) {
                    o->props[k].flags = src->props[i].flags;
                    break;
                }
        }
        acct = acct_find(player_name(sid));
        if (acct)
            o->owner = acct->id;
        snprintf(b, sizeof(b), "Cloned %s as %s.",
                 obj_fmt(srcid, i1, sizeof(i1)),
                 obj_fmt(newid, i2, sizeof(i2)));
        session_write(sid, b);
        return;
    }

    /* @move <obj> to <dest> (alias @teleport): relocate an object you own. */
    if (strcmp(tag, "move") == 0 || strcmp(tag, "teleport") == 0) {
        const char *to = strstr(p, " to ");
        char os[64], b[96], i1[16], i2[16];
        int oid, did, n;
        struct obj *o;

        if (!to) {
            session_write(sid, "Usage: @move <obj> to <dest>");
            return;
        }
        n = (int)(to - p);
        if (n >= (int)sizeof(os)) n = sizeof(os) - 1;
        memcpy(os, p, n);
        os[n] = '\0';
        while (n > 0 && os[n - 1] == ' ') os[--n] = '\0';
        oid = olc_ref(sid, os);
        did = olc_ref(sid, to + 4);
        o = obj_find(oid);
        if (!o) { session_write(sid, "Move what?"); return; }
        if (!obj_find(did)) { session_write(sid, "Move it where?"); return; }
        if (!is_wizard(sid) && !obj_owner_match(o, sid)) {
            session_write(sid, "You don't own that.");
            return;
        }
        prop_set(o, make_atom("location"), val_obj(did));
        snprintf(b, sizeof(b), "Moved %s to %s.",
                 obj_fmt(oid, i1, sizeof(i1)),
                 obj_fmt(did, i2, sizeof(i2)));
        session_write(sid, b);
        return;
    }

    /* @go <room> : teleport your own avatar (wizard-only builder convenience). */
    if (strcmp(tag, "go") == 0) {
        int did = olc_ref(sid, p);
        struct obj *d = obj_find(did), *me = obj_find(cc[sid].obj);
        char b[64], i1[16];

        if (!is_wizard(sid)) {
            session_write(sid, "You are not authorized to do that.");
            return;
        }
        if (!d || !me) { session_write(sid, "Go where?"); return; }
        prop_set(me, make_atom("location"), val_obj(did));
        snprintf(b, sizeof(b), "You go to %s.",
                 obj_fmt(did, i1, sizeof(i1)));
        session_write(sid, b);
        status_update(sid);
        mob_enter(cc[sid].obj, did, sid);   /* wake the room's reactive NPCs */
        return;
    }

    /* @wake #N : start object #N as an autonomous agent (OLC-5). #N.brain names
       the agent verb (a compiled program that links -lverbmain); the agent runs
       under the system session with this = #N, so it keeps ticking with no
       player present. */
    if (strcmp(tag, "wake") == 0) {
        int mid, brain, mode, ti, roomid;
        struct obj *mo, *bo;
        const char *elf;
        char hash[65], b[64], i1[16];
        struct verb_match m;

        if (!is_wizard(sid)) {
            session_write(sid, "You are not authorized to do that.");
            return;
        }
        mid = olc_ref(sid, p);
        mo = obj_find(mid);
        if (!mo) { session_write(sid, "Wake what?"); return; }
        if (handler_task_of(mid) >= 0) {
            session_write(sid, "It is already awake.");
            return;
        }
        brain = prop_objnum(mo, make_atom("brain"));
        bo = obj_find(brain);
        if (!bo) {
            session_write(sid,
                "That has no brain (set #N.brain=#<agent verb>).");
            return;
        }
        elf = prop_str(bo, make_atom("elf"));
        if (!elf || elf_parse(elf, &mode, hash, sizeof(hash)) != OK) {
            session_write(sid, "Its brain has no compiled program.");
            return;
        }
        roomid = prop_objnum(mo, make_atom("location"));
        ti = task_alloc(SYS_SID);
        if (ti < 0) { session_write(sid, "No task slots."); return; }
        memset(&m, 0, sizeof(m));
        m.verb = "boot";
        m.this_obj = mid;
        m.verb_obj = brain;
        m.dobj = OBJ_NONE;
        m.iobj = OBJ_NONE;
        memcpy(m.hash, hash, sizeof(m.hash));
        if (task_setup(ti, hash, mid, roomid, "", &m) != OK) {
            task_free(ti);
            session_write(sid, "Failed to start agent.");
            return;
        }
        /* Register the object on the task now, before the agent runs its own
           sys_listen on the first step. Without this a rapid second @wake would
           not see it as awake and would start a duplicate orphan task. The
           agent's later sys_listen re-sets the same values, and task_free clears
           them via handler_obj. */
        mo->task_id = tasks[ti].id;
        tasks[ti].handler_obj = mid;
        snprintf(b, sizeof(b), "Woke %s.", obj_fmt(mid, i1, sizeof(i1)));
        session_write(sid, b);
        return;
    }

    /* @find <name> : list objects whose name matches (case-insensitive). */
    if (strcmp(tag, "find") == 0) {
        const char *a_name = make_atom("name");
        char b[128], i1[16];
        int found = 0;

        if (!*p) { session_write(sid, "Usage: @find <name>"); return; }
        for (int i = 0; i < MAX_OBJ; i++) {
            const char *nm;
            if (objs[i].id == OBJ_NONE) continue;
            nm = prop_str(&objs[i], a_name);
            if (nm && olc_ci_contains(nm, p)) {
                snprintf(b, sizeof(b), "  %s  %s",
                         obj_fmt(objs[i].id, i1, sizeof(i1)), nm);
                session_write(sid, b);
                if (++found >= 50) {
                    session_write(sid, "  (more matches not shown)");
                    break;
                }
            }
        }
        if (!found) session_write(sid, "Nothing found.");
        return;
    }

    /* @contents [#N] : list the objects located in #N (or the current room). */
    if (strcmp(tag, "contents") == 0) {
        int cid = *p ? olc_ref(sid, p) : room;
        const char *a_loc = make_atom("location");
        const char *a_name = make_atom("name");
        char b[128], i1[16];
        int found = 0;

        if (!obj_find(cid)) { session_write(sid, "No such object."); return; }
        for (int i = 0; i < MAX_OBJ; i++) {
            const char *nm;
            if (objs[i].id == OBJ_NONE) continue;
            if (prop_objnum(&objs[i], a_loc) != cid) continue;
            nm = prop_str(&objs[i], a_name);
            snprintf(b, sizeof(b), "  %s  %s",
                     obj_fmt(objs[i].id, i1, sizeof(i1)), nm ? nm : "(unnamed)");
            session_write(sid, b);
            found++;
        }
        if (!found) session_write(sid, "  (empty)");
        return;
    }

    /* @dig <exit> to <room name | #N> : create a room (or link an existing
       one) and a matching pair of exits. Only the room's owner (or a wizard)
       may add an exit leading out of it. */
    if (strcmp(tag, "dig") == 0) {
        const char *to = strstr(p, " to "), *d;
        char ename[32], b[128], i1[16], i2[16], numbuf[16];
        int n, roomid, fwd = OBJ_NONE, rev;
        struct obj *acct = acct_find(player_name(sid));
        int owner = acct ? acct->id : OBJ_NONE;
        struct obj *here = obj_find(room);

        if (!here || (!is_wizard(sid) && !obj_owner_match(here, sid))) {
            session_write(sid, "You don't own this room.");
            return;
        }
        if (!to || to == p) {
            session_write(sid, "Usage: @dig <exit> to <room name | #N>");
            return;
        }
        n = (int)(to - p);
        if (n >= (int)sizeof(ename)) n = sizeof(ename) - 1;
        memcpy(ename, p, n);
        ename[n] = '\0';
        while (n > 0 && ename[n - 1] == ' ') ename[--n] = '\0';
        d = to + 4;
        while (*d == ' ') d++;
        if (!*ename || !*d) {
            session_write(sid, "Usage: @dig <exit> to <room name | #N>");
            return;
        }

        if (*d == '#') {
            roomid = (int)strtol(d + 1, NULL, 10);
            if (!obj_find(roomid)) {
                session_write(sid, "No such room.");
                return;
            }
        } else {
            struct obj *r;
            roomid = obj_next_id();
            if (roomid == OBJ_NONE) {
                session_write(sid, "No more object IDs available.");
                return;
            }
            r = obj_create(roomid, 100);
            if (!r) {
                session_write(sid, "Failed to create room.");
                return;
            }
            r->owner = owner;
            prop_set(r, make_atom("name"), val_str(d));
        }

        /* forward exit in the current room -> destination */
        fwd = obj_next_id();
        if (fwd != OBJ_NONE) {
            struct obj *e = obj_create(fwd, 100);
            if (e) {
                e->owner = owner;
                prop_set(e, make_atom("name"), val_str(ename));
                prop_set(e, make_atom("location"), val_obj(room));
                snprintf(numbuf, sizeof(numbuf), "%d", roomid);
                prop_set(e, make_atom("dest"), val_str(numbuf));
            }
        }
        /* reverse exit in the destination -> here, named "back" */
        rev = obj_next_id();
        if (rev != OBJ_NONE) {
            struct obj *e = obj_create(rev, 100);
            if (e) {
                e->owner = owner;
                prop_set(e, make_atom("name"), val_str("back"));
                prop_set(e, make_atom("location"), val_obj(roomid));
                snprintf(numbuf, sizeof(numbuf), "%d", room);
                prop_set(e, make_atom("dest"), val_str(numbuf));
            }
        }
        snprintf(b, sizeof(b), "Dug %s to %s (exit '%s', return 'back').",
                 obj_fmt(fwd, i1, sizeof(i1)),
                 obj_fmt(roomid, i2, sizeof(i2)), ename);
        session_write(sid, b);
        return;
    }

    /* @reset [#area] : reconcile reset rules (parent #910). Each rule names a
       room, a proto to clone, and a count; the pass tops each room up to that
       many live (non-downed) children of the proto, cloning the shortfall. It
       is idempotent, so running it again spawns nothing until instances die.
       Wizard-only, since it populates rooms across the world. */
    if (strcmp(tag, "reset") == 0) {
        const char *a_room = make_atom("room");
        const char *a_proto = make_atom("proto");
        const char *a_area = make_atom("area");
        const char *a_loc = make_atom("location");
        const char *a_downed = make_atom("downed");
        int filter = OBJ_NONE, rules = 0, spawned = 0;
        char b[96];

        if (!is_wizard(sid)) {
            session_write(sid, "You are not authorized to do that.");
            return;
        }
        if (*p == '#')
            filter = (int)strtol(p + 1, NULL, 10);

        for (int i = 0; i < MAX_OBJ; i++) {
            int rroom, rproto, rcount, live;

            if (objs[i].id == OBJ_NONE || objs[i].parent != RESET_PARENT)
                continue;
            if (filter != OBJ_NONE &&
                prop_objnum(&objs[i], a_area) != filter)
                continue;
            rroom = prop_objnum(&objs[i], a_room);
            rproto = prop_objnum(&objs[i], a_proto);
            rcount = prop_int(&objs[i], "count", 0);
            if (rroom == OBJ_NONE || rproto == OBJ_NONE || rcount <= 0)
                continue;
            rules++;

            /* count live (non-downed) children of rproto already in rroom */
            live = 0;
            for (int j = 0; j < MAX_OBJ; j++) {
                if (objs[j].id == OBJ_NONE || objs[j].parent != rproto)
                    continue;
                if (prop_objnum(&objs[j], a_loc) != rroom)
                    continue;
                if (prop_int(&objs[j], "downed", 0))
                    continue;
                live++;
            }
            for (; live < rcount; live++) {
                int nid = obj_next_id();
                struct obj *nc, *pr;

                if (nid == OBJ_NONE)
                    break;
                nc = obj_create(nid, rproto);
                if (!nc)
                    break;
                pr = obj_find(rproto);
                nc->owner = pr ? pr->owner : OBJ_NONE;
                prop_set(nc, a_loc, val_obj(rroom));
                prop_set(nc, a_downed, val_str("0"));
                spawned++;
            }
        }
        snprintf(b, sizeof(b), "Reset: %d rule(s), %d spawned.",
                 rules, spawned);
        session_write(sid, b);
        return;
    }

    /* Handle @program command: compile #N.src (MooScript or C) into #N.elf */
    if (strcmp(tag, "program") == 0) {
        int objid = OBJ_NONE;
        struct obj *o;
        const char *src, *elf;
        char hash[65], elf_val[128];
        int mode = 0755, em, is_c;
        char h[65];

        if (*p == '#')
            objid = strtol(p + 1, NULL, 10);
        o = (objid != OBJ_NONE) ? obj_find(objid) : NULL;
        if (!o) {
            session_write(sid,
                "Usage: @program #<object> (write its src property first)");
            return;
        }
        if (!is_wizard(sid) && !obj_owner_match(o, sid)) {
            session_write(sid, "You don't own that object.");
            return;
        }
        src = prop_str(o, make_atom("src"));
        if (!src || !*src) {
            session_write(sid,
                "No src to compile. Use @edit #N.src to write MooScript.");
            return;
        }
        elf = prop_str(o, make_atom("elf"));
        if (elf && elf_parse(elf, &em, h, sizeof(h)) == OK)
            mode = em;
        /* Language: an explicit "c" or "moo" after #N wins, otherwise sniff
         * the source (a C verb has #include, MooScript never does). */
        {
            const char *q = p;
            while (*q && !isspace((unsigned char)*q))
                q++;
            while (isspace((unsigned char)*q))
                q++;
            if (*q == 'c' || *q == 'C')
                is_c = 1;
            else if (*q == 'm' || *q == 'M')
                is_c = 0;
            else
                is_c = strstr(src, "#include") != NULL;
        }
        if (program_compile(sid, src, hash, is_c) != OK)
            return;
        snprintf(elf_val, sizeof(elf_val), "[0%o,b2:%s]", mode, hash);
        prop_set(o, make_atom("elf"), val_str(elf_val));
        session_write(sid, "Programmed.");
        return;
    }

    /* Handle @invite command */
    if (strcmp(tag, "invite") == 0) {
        struct obj *acct = acct_find(player_name(sid));
        int is_admin = acct && prop_str(acct, make_atom("admin"));
        char buf[256];

        if (strcmp(p, "new") == 0 && is_admin) {
            struct obj *inv = invite_create(acct->id,
                                            ADMIN_CODE_USES, 0);
            if (inv) {
                snprintf(buf, sizeof(buf), "New code: %s",
                    prop_str(inv, make_atom("code")));
                session_write(sid, buf);
            } else {
                session_write(sid, "Failed to create invite.");
            }
            return;
        }
        if (!acct) {
            session_write(sid, "No account found.");
            return;
        }
        session_write(sid, "Your invite codes:");
        for (int i = 0; i < MAX_OBJ; i++) {
            if (objs[i].id == OBJ_NONE ||
                objs[i].parent != INVITE_PARENT)
                continue;
            if (objs[i].owner != acct->id)
                continue;
            const char *code = prop_str(&objs[i],
                                        make_atom("code"));
            const char *ms = prop_str(&objs[i],
                                      make_atom("max_uses"));
            const char *us = prop_str(&objs[i],
                                      make_atom("used"));
            snprintf(buf, sizeof(buf), "  %s  (%s/%s used)",
                code ?: "???", us ?: "0", ms ?: "?");
            session_write(sid, buf);
        }
        return;
    }

    /* Handle @standing command (GM-adjudicated faction standing, Section 14):
     * @standing <player> <faction> <step> merges one faction:step into the
     * target's sheet, preserving the other factions. */
    if (strcmp(tag, "standing") == 0) {
        char who[64], fac[32], newval[256], buf[256];
        int step, tsid, len = 0;
        struct obj *sheet;
        const char *old, *s;

        if (!is_wizard(sid)) {
            session_write(sid, "You are not authorized to do that.");
            return;
        }
        if (sscanf(p, "%63s %31s %d", who, fac, &step) != 3) {
            session_write(sid,
                "Usage: @standing <player> <faction> <step>");
            return;
        }
        tsid = find_player_by_name(who, OBJ_NONE);
        if (tsid < 0) {
            session_write(sid, "No such player is connected.");
            return;
        }
        sheet = player_sheet(cc[tsid].obj);
        if (!sheet) {
            session_write(sid, "That player has no sheet.");
            return;
        }

        /* copy every existing pair except `fac`, then append the new one */
        old = prop_str(sheet, make_atom("standing"));
        s = old ? old : "";
        while (*s) {
            const char *start = s, *colon = NULL;
            int spanlen, idlen;

            while (*s && *s != ',') {
                if (*s == ':' && !colon) colon = s;
                s++;
            }
            spanlen = (int)(s - start);
            idlen = colon ? (int)(colon - start) : spanlen;
            if (!(idlen == (int)strlen(fac) &&
                  strncmp(start, fac, idlen) == 0) && spanlen > 0) {
                if (len && len < (int)sizeof(newval) - 1)
                    newval[len++] = ',';
                for (int k = 0; k < spanlen &&
                                len < (int)sizeof(newval) - 1; k++)
                    newval[len++] = start[k];
            }
            if (*s == ',') s++;
        }
        {
            char pair[48];
            int pl = snprintf(pair, sizeof(pair), "%s:%d", fac, step);
            if (len && len < (int)sizeof(newval) - 1)
                newval[len++] = ',';
            for (int k = 0; k < pl && len < (int)sizeof(newval) - 1; k++)
                newval[len++] = pair[k];
        }
        newval[len] = '\0';
        prop_set(sheet, make_atom("standing"), val_str(newval));
        snprintf(buf, sizeof(buf), "%s now stands %s (%d) with %s.",
                 who, standing_word(step), step, fac);
        session_write(sid, buf);
        return;
    }

    /* @kill <player> (GM, Section 11): strike a player dead outright, the
     * deterministic entry to the death-and-recovery track. */
    if (strcmp(tag, "kill") == 0) {
        char who[64], buf[128];
        int tsid, troom;
        struct obj *sheet, *pobj;

        if (!is_wizard(sid)) {
            session_write(sid, "You are not authorized to do that.");
            return;
        }
        if (sscanf(p, "%63s", who) != 1) {
            session_write(sid, "Usage: @kill <player>");
            return;
        }
        tsid = find_player_by_name(who, OBJ_NONE);
        if (tsid < 0) {
            session_write(sid, "No such player is connected.");
            return;
        }
        sheet = player_sheet(cc[tsid].obj);
        if (!sheet) {
            session_write(sid, "That player has no sheet.");
            return;
        }
        prop_set(sheet, make_atom("bp"), val_str("0"));
        prop_set(sheet, make_atom("dying"), val_str("0"));
        prop_set(sheet, make_atom("downed"), val_str("0"));
        prop_set(sheet, make_atom("dead"), val_str("1"));
        pobj = obj_find(cc[tsid].obj);
        troom = pobj ? prop_objnum(pobj, make_atom("location")) : OBJ_NONE;
        snprintf(buf, sizeof(buf), "%s falls dead.", who);
        if (troom != OBJ_NONE) room_broadcast(-1, troom, buf);
        session_write(tsid, "You have been struck down. Your body lies still.");
        session_write(sid, "Done.");
        status_update(tsid);
        return;
    }

    /* @grant <player> <amount> (GM): add creds to a player, the econ knob
     * behind services and the money sink. */
    if (strcmp(tag, "grant") == 0) {
        char who[64], b[16];
        int tsid, amount;
        struct obj *sheet;

        if (!is_wizard(sid)) {
            session_write(sid, "You are not authorized to do that.");
            return;
        }
        if (sscanf(p, "%63s %d", who, &amount) != 2) {
            session_write(sid, "Usage: @grant <player> <amount>");
            return;
        }
        tsid = find_player_by_name(who, OBJ_NONE);
        if (tsid < 0) {
            session_write(sid, "No such player is connected.");
            return;
        }
        sheet = player_sheet(cc[tsid].obj);
        if (!sheet) {
            session_write(sid, "That player has no sheet.");
            return;
        }
        snprintf(b, sizeof(b), "%d", prop_int(sheet, "money", 0) + amount);
        prop_set(sheet, make_atom("money"), val_str(b));
        session_write(sid, "Done.");
        status_update(tsid);
        return;
    }

    /* @teach <player> <skill> <points> (GM): set a skill on a player, the knob
     * behind gated actions such as ally revival (Section 11). */
    if (strcmp(tag, "teach") == 0) {
        char who[64], skill[32], pn[40];
        int tsid, points;
        struct obj *sheet;

        if (!is_wizard(sid)) {
            session_write(sid, "You are not authorized to do that.");
            return;
        }
        if (sscanf(p, "%63s %31s %d", who, skill, &points) != 3) {
            session_write(sid, "Usage: @teach <player> <skill> <points>");
            return;
        }
        tsid = find_player_by_name(who, OBJ_NONE);
        if (tsid < 0) {
            session_write(sid, "No such player is connected.");
            return;
        }
        sheet = player_sheet(cc[tsid].obj);
        if (!sheet) {
            session_write(sid, "That player has no sheet.");
            return;
        }
        snprintf(pn, sizeof(pn), "sk_%s", skill);
        { char pv[16]; snprintf(pv, sizeof(pv), "%d", points);
          prop_set(sheet, make_atom(pn), val_str(pv)); }
        session_write(sid, "Done.");
        status_update(tsid);
        return;
    }

    /* No built-in @tag matched. Try a builder or admin verb named "@tag"
       (see OLC.md) before treating the line as feedback, so online-creation
       tools can ship as verbs on the VM rather than as host code. */
    if (*tag) {
        char vname[sizeof(tag) + 1];

        snprintf(vname, sizeof(vname), "@%s", tag);
        if (verb_dispatch(sid, vname, p) == OK) {
            status_update(sid);
            return;
        }
    }

    /* Default feedback path for @gripe, @typo, @bug, etc. */
    if (!*tag || !*p) {
        session_write(sid,
                  "Usage: @<gripe|typo|bug|idea|suggest|comment>"
                  " <message>");
        return;
    }
    fprintf(stderr, "[%s] by %s in #%d (owner #%d): %s\n",
        tag, name ?: "???", room, owner, p);
    session_write(sid, "Thank you, your feedback has been noted.");
}

static void
cmd_connect(int sid, const char *args)
{
    char name[64], pw[64], msg[256];
    const char *stored;
    struct obj *acct;
    int pid;

    if (sscanf(args, "%63s %63s", name, pw) != 2) {
        out_puts(sid, '!', "Enter name and password.");
        return;
    }
    acct = acct_find(name);
    if (!acct) {
        out_puts(sid, '!', "Unknown account.");
        return;
    }
    stored = prop_str(acct, make_atom("pwhash"));
    if (!stored || pw_verify(pw, stored) != OK) {
        out_puts(sid, '!', "Wrong password.");
        return;
    }
    for (int i = 0; i < MAX_CONN; i++) {
        if (i != sid && cc[i].state == CONN_SSE) {
            const char *n = player_name(i);
            if (n && strcasecmp(n, name) == 0) {
                out_puts(sid, '!', "Already connected.");
                return;
            }
        }
    }
    pid = obj_alloc_ephemeral(200);
    if (pid == OBJ_NONE) {
        out_puts(sid, '!', "Server full.");
        return;
    }
    {
        struct obj *po = obj_find(pid);
        int cid = char_ensure(acct);
        if (po) {
            prop_set(po, make_atom("name"), val_str(name));
            prop_set(po, make_atom("location"), val_obj(101));
            if (prop_str(acct, make_atom("admin")))
                prop_set(po, make_atom("admin"), val_str("1"));
            if (cid != OBJ_NONE) {
                char cb[16];
                snprintf(cb, sizeof(cb), "%d", cid);
                prop_set(po, make_atom("charid"), val_str(cb));
            }
        }
    }
    cc[sid].obj = pid;
    out_puts(sid, '+', "");
    status_update(sid);
    out_flush(sid);
    snprintf(msg, sizeof(msg), "%s has connected.", name);
    room_broadcast(sid, 101, msg);
    snprintf(msg, sizeof(msg),
        "Welcome back, %s. Type 'help' for commands.", name);
    session_write(sid, msg);
    mob_enter(pid, 101, sid);   /* wake the lobby's reactive NPCs on login */
}

static int
name_valid(const char *n)
{
    int len = strlen(n);

    if (len < 2 || len > 20)
        return 0;
    for (int i = 0; i < len; i++)
        if (!isalnum(n[i]) && n[i] != '_')
            return 0;
    return 1;
}

static void
cmd_create_acct(int sid, const char *args)
{
    char name[64], pw[64], code[20], pwhash[PW_STR_LEN + 1], msg[256];
    struct obj *inv, *acct;
    int acct_id, pid;

    if (sscanf(args, "%63s %63s %19s", name, pw, code) != 3) {
        out_puts(sid, '!', "Enter name, password, and invite code.");
        return;
    }
    if (!name_valid(name)) {
        out_puts(sid, '!',
            "Name must be 2-20 chars (letters, digits, _).");
        return;
    }
    if (acct_find(name)) {
        out_puts(sid, '!', "Name already taken.");
        return;
    }
    inv = invite_find(code);
    if (!inv) {
        out_puts(sid, '!', "Invalid invite code.");
        return;
    }
    {
        const char *ms = prop_str(inv, make_atom("max_uses"));
        const char *us = prop_str(inv, make_atom("used"));
        if (ms && us && atoi(us) >= atoi(ms)) {
            out_puts(sid, '!', "Invite code exhausted.");
            return;
        }
    }
    if (pw_hash(pw, pwhash) != OK) {
        out_puts(sid, '!', "Internal error.");
        return;
    }
    acct_id = obj_next_id();
    if (acct_id == OBJ_NONE) {
        out_puts(sid, '!', "Server full.");
        return;
    }
    acct = obj_create(acct_id, ACCT_PARENT);
    if (!acct) {
        out_puts(sid, '!', "Account creation failed.");
        return;
    }
    prop_set(acct, make_atom("name"), val_str(name));
    prop_set(acct, make_atom("pwhash"), val_str(pwhash));
    int is_admin = prop_str(inv, make_atom("admin")) != NULL;
    if (is_admin)
        prop_set(acct, make_atom("admin"), val_str("1"));
    {
        char tmp[16];
        const char *us = prop_str(inv, make_atom("used"));
        snprintf(tmp, sizeof(tmp), "%d", (us ? atoi(us) : 0) + 1);
        prop_set(inv, make_atom("used"), val_str(tmp));
    }
    if (is_admin)
        invite_create(acct_id, ADMIN_CODE_USES, 1);
    else
        for (int i = 0; i < USER_CODE_COUNT; i++)
            invite_create(acct_id, 1, 0);

    pid = obj_alloc_ephemeral(200);
    if (pid == OBJ_NONE) {
        out_puts(sid, '!', "Server full.");
        return;
    }
    {
        struct obj *po = obj_find(pid);
        int cid = char_ensure(acct);
        if (po) {
            prop_set(po, make_atom("name"), val_str(name));
            prop_set(po, make_atom("location"), val_obj(101));
            if (is_admin)
                prop_set(po, make_atom("admin"), val_str("1"));
            if (cid != OBJ_NONE) {
                char cb[16];
                snprintf(cb, sizeof(cb), "%d", cid);
                prop_set(po, make_atom("charid"), val_str(cb));
            }
        }
    }
    cc[sid].obj = pid;
    out_puts(sid, '+', "");
    status_update(sid);
    out_flush(sid);
    snprintf(msg, sizeof(msg), "%s has connected.", name);
    room_broadcast(sid, 101, msg);
    snprintf(msg, sizeof(msg),
        "Welcome, %s! Type 'help' for commands.", name);
    session_write(sid, msg);
}

/* --- M25i: death and recovery (Section 11) -------------------------------- */

#define DEATH_SWEEP_MS   2000            /* how often the death clock ticks */
#define RECOVER_ROOM     101             /* the default recovery point */
#define RECOVER_FEE      100             /* base clinic revival fee, in creds */
#define RECOVER_FACTION  "medtek"        /* clinic faction, priced by standing */

static int death_recover_ms = 24 * 3600 * 1000;   /* auto-recovery span, tunable */

/* Bring a dead player back (Section 11): clear the death state, restore BP and
 * Wounds, wake the body in `room`, and print `wake_msg`. Shared by the paid
 * clinic, the auto-recovery timer, and an ally's in-place revival. Any carrier
 * link is dropped, since a revived player is no longer a body. */
static void
player_revive(int sid, int room, const char *wake_msg)
{
    struct obj *sheet = player_sheet(cc[sid].obj);
    struct obj *pobj = obj_find(cc[sid].obj);
    char b[16];

    if (!sheet) return;
    prop_set(sheet, make_atom("dead"), val_str("0"));
    prop_set(sheet, make_atom("dying"), val_str("0"));
    prop_set(sheet, make_atom("downed"), val_str("0"));
    prop_set(sheet, make_atom("death_tick"), val_str("0"));
    prop_set(sheet, make_atom("wounds"), val_str("0"));
    snprintf(b, sizeof(b), "%d", bp_max(sheet));
    prop_set(sheet, make_atom("bp"), val_str(b));
    if (pobj) {
        prop_set(pobj, make_atom("location"), val_obj(room));
        prop_set(pobj, make_atom("carrier"), val_str("0"));
    }
    session_write(sid, wake_msg);
    status_update(sid);
}

/* The death clock (Section 11): each sweep, every connected dead player's
 * countdown ticks down, and at zero auto-recovery revives it free at the
 * recovery point. A fresh death (death_tick 0) arms the countdown first. */
static void
death_sweep_cb(void *arg)
{
    (void)arg;
    for (int sid = 0; sid < MAX_CONN; sid++) {
        struct obj *sheet;
        int rem;
        char b[16];

        struct obj *pobj;

        if (cc[sid].state != CONN_SSE || cc[sid].obj == OBJ_NONE) continue;
        sheet = player_sheet(cc[sid].obj);
        if (!sheet || !prop_int(sheet, "dead", 0)) continue;
        /* a body an ally is carrying is being handled: pause its clock */
        pobj = obj_find(cc[sid].obj);
        if (pobj && prop_int(pobj, "carrier", 0)) continue;
        rem = prop_int(sheet, "death_tick", 0);
        if (rem <= 0) {                  /* newly dead: arm the countdown */
            snprintf(b, sizeof(b), "%d", death_recover_ms);
            prop_set(sheet, make_atom("death_tick"), val_str(b));
            continue;
        }
        rem -= DEATH_SWEEP_MS;
        if (rem <= 0) {
            player_revive(sid, RECOVER_ROOM,
                "You wake at the recovery point, patched and breathing.");
        } else {
            snprintf(b, sizeof(b), "%d", rem);
            prop_set(sheet, make_atom("death_tick"), val_str(b));
        }
    }
    timer_add(DEATH_SWEEP_MS, death_sweep_cb, NULL);
}

/* The paid clinic path (Section 11): a dead player revives at once for a fee in
 * creds, priced by standing with the clinic faction like any vendor. */
static void
cmd_recover(int sid, const char *args)
{
    struct obj *sheet = player_sheet(cc[sid].obj);
    int money, step, price;
    char b[128];

    (void)args;
    if (!sheet || !prop_int(sheet, "dead", 0)) {
        session_write(sid, "You are not dead.");
        return;
    }
    step = standing_step(sheet, RECOVER_FACTION);
    if (step <= -2) {
        session_write(sid, "The clinics will not treat you.");
        return;
    }
    price = RECOVER_FEE * (10 - step) / 10;
    money = prop_int(sheet, "money", 0);
    if (money < price) {
        snprintf(b, sizeof(b),
                 "Recovery costs %d creds; you have %d. Wait it out.",
                 price, money);
        session_write(sid, b);
        return;
    }
    snprintf(b, sizeof(b), "%d", money - price);
    prop_set(sheet, make_atom("money"), val_str(b));
    player_revive(sid, RECOVER_ROOM,
        "You wake at the recovery point, patched and breathing.");
    snprintf(b, sizeof(b), "The clinic revives you for %d creds.", price);
    session_write(sid, b);
}

/* --- M25i ally slice: carry a body between rooms and revive it in place ---
 *
 * The body stays on the room floor with a `carrier` link to its bearer, and the
 * bearer keeps a `carrying` link to it; `go` drags the linked body along
 * (verb_go). A revive needs Medicine or Cybertech and restores the body where
 * it lies, so an ally can haul a fallen friend somewhere safe and patch them. */

/* Is `name` a dead body in sid's room? Returns its sid, or -1 with a message. */
static int
find_body(int sid, const char *name)
{
    int room = player_room(sid);
    int tsid = find_player_by_name(name, room);
    struct obj *tsheet;

    if (tsid < 0) {
        session_write(sid, "There is no one here by that name.");
        return -1;
    }
    tsheet = player_sheet(cc[tsid].obj);
    if (!tsheet || !prop_int(tsheet, "dead", 0)) {
        session_write(sid, "They are not a body to tend.");
        return -1;
    }
    return tsid;
}

/* A dead actor is an inert body and cannot tend anyone. */
static int
actor_dead(int sid)
{
    struct obj *s = player_sheet(cc[sid].obj);

    if (s && prop_int(s, "dead", 0)) {
        session_write(sid, "You are dead.");
        return 1;
    }
    return 0;
}

static void
cmd_carry(int sid, const char *args)
{
    struct obj *self = obj_find(cc[sid].obj);
    char who[64], b[128];
    int tsid;

    if (actor_dead(sid)) return;
    if (sscanf(args, "%63s", who) != 1) {
        session_write(sid, "Carry whom?");
        return;
    }
    if (self && prop_int(self, "carrying", 0)) {
        session_write(sid, "Your hands are already full.");
        return;
    }
    tsid = find_body(sid, who);
    if (tsid < 0) return;
    /* store the links as decimal strings: the VM `go` reads `carrying` through
     * sys_getprop/cs_geti, which stringifies like `wielded`, not an obj ref */
    { char cb[16];
      snprintf(cb, sizeof(cb), "%d", cc[tsid].obj);
      prop_set(self, make_atom("carrying"), val_str(cb));
      snprintf(cb, sizeof(cb), "%d", cc[sid].obj);
      prop_set(obj_find(cc[tsid].obj), make_atom("carrier"), val_str(cb)); }
    snprintf(b, sizeof(b), "%s lifts %s's body.", player_name(sid), who);
    room_broadcast(-1, player_room(sid), b);
    session_write(sid, "You take up the body; it will follow as you move.");
}

static void
cmd_release(int sid, const char *args)
{
    struct obj *self = obj_find(cc[sid].obj);
    int body;
    char b[128];

    (void)args;
    if (actor_dead(sid)) return;
    body = self ? prop_int(self, "carrying", 0) : 0;
    if (body <= 0) {
        session_write(sid, "You are not carrying anyone.");
        return;
    }
    prop_set(self, make_atom("carrying"), val_str("0"));
    { struct obj *bo = obj_find(body);
      if (bo) prop_set(bo, make_atom("carrier"), val_str("0")); }
    snprintf(b, sizeof(b), "%s sets a body down.", player_name(sid));
    room_broadcast(-1, player_room(sid), b);
    session_write(sid, "You set the body down.");
}

static void
cmd_revive(int sid, const char *args)
{
    struct obj *self = obj_find(cc[sid].obj);
    struct obj *skills = player_sheet(cc[sid].obj);
    char who[64], b[128];
    int tsid;

    if (actor_dead(sid)) return;
    if (sscanf(args, "%63s", who) != 1) {
        session_write(sid, "Revive whom?");
        return;
    }
    if (!skills || (prop_int(skills, "sk_medicine", 0) <= 0 &&
                    prop_int(skills, "sk_cybertech", 0) <= 0)) {
        session_write(sid, "You lack the Medicine or Cybertech training to revive anyone.");
        return;
    }
    tsid = find_body(sid, who);
    if (tsid < 0) return;
    /* if we were carrying this body, we are no longer */
    if (self && prop_int(self, "carrying", 0) == cc[tsid].obj)
        prop_set(self, make_atom("carrying"), val_str("0"));
    player_revive(tsid, player_room(sid), "An ally pulls you back from the brink.");
    snprintf(b, sizeof(b), "%s patches %s back to life.", player_name(sid), who);
    room_broadcast(sid, player_room(sid), b);
    session_write(sid, b);
}

struct cmd {
    const char *name;
    void (*fn)(int, const char *);
    int cost;
};

static const struct cmd cmds[] = {
    {"say",      cmd_say,      1},
    {"\"",       cmd_say,      1},
    {"emote",    cmd_emote,    1},
    {":",        cmd_emote,    1},
    {"::",       cmd_emote2,   1},
    {"whisper",  cmd_whisper,  1},
    {"page",     cmd_page,     2},
    {"help",     cmd_help,     0},
    {"quit",     cmd_quit,     0},
    {"recover",  cmd_recover,  0},
    {"carry",    cmd_carry,    1},
    {"release",  cmd_release,  1},
    {"revive",   cmd_revive,   1},
    {"@",        cmd_feedback, 0},
};

#define NCMDS (sizeof(cmds) / sizeof(cmds[0]))

static void fuel_schedule_regen(int sid);

static void
regen_cb(void *arg)
{
    int sid = (int)(intptr_t)arg;

    if (sid < 0 || sid >= MAX_CONN || cc[sid].state != CONN_SSE)
        return;
    cc[sid].regen_timer = -1;
    cc[sid].fuel += FUEL_REGEN;
    if (cc[sid].fuel > FUEL_MAX)
        cc[sid].fuel = FUEL_MAX;
    status_update(sid);
    if (cc[sid].fuel < FUEL_MAX)
        fuel_schedule_regen(sid);
}

#define AUTOSAVE_MS 300000

static void
autosave_cb(void *arg)
{
    (void)arg;
    world_save_async();
    timer_add(AUTOSAVE_MS, autosave_cb, NULL);
}

static void
fuel_schedule_regen(int sid)
{
    if (cc[sid].regen_timer >= 0)
        return;
    cc[sid].regen_timer = timer_add(TICK_MS, regen_cb,
                                    (void *)(intptr_t)sid);
}

/* Slow downtime Grit trickle (Section 7): credit one point, write it to the
 * creature's grit property (the store of record), and let status_update show
 * it and reschedule while still below Max Grit. Combat Grit recovery is not
 * this; it comes from Command and consumables. */
static void
grit_regen_cb(void *arg)
{
    int sid = (int)(intptr_t)arg;
    struct obj *po;
    int gmax, g;
    char b[16];

    if (sid < 0 || sid >= MAX_CONN || cc[sid].state != CONN_SSE)
        return;
    cc[sid].grit_timer = -1;
    po = player_sheet(cc[sid].obj);
    if (!po)
        return;
    gmax = grit_max(po);
    g = prop_int(po, "grit", gmax) + GRIT_REGEN;
    if (g > gmax)
        g = gmax;
    snprintf(b, sizeof(b), "%d", g);
    prop_set(po, make_atom("grit"), val_str(b));
    cc[sid].grit = g;
    status_update(sid);
}

static void
grit_schedule_regen(int sid)
{
    if (cc[sid].grit_timer >= 0)
        return;
    cc[sid].grit_timer = timer_add(GRIT_TICK_MS, grit_regen_cb,
                                   (void *)(intptr_t)sid);
}

static void
dispatch(int sid, char *line)
{
    char cmdbuf[16], *args;
    int n = 0;

    while (isspace(*line))
        line++;
    if (!*line)
        return;

    /* pre-login: only connect/create allowed */
    if (cc[sid].obj == OBJ_NONE) {
        char *sp = strchr(line, ' ');
        const char *rest = sp ? sp + 1 : "";
        if (sp) *sp = '\0';
        if (strcmp(line, "connect") == 0)
            cmd_connect(sid, rest);
        else if (strcmp(line, "create") == 0)
            cmd_create_acct(sid, rest);
        return;
    }

    if (*line == '"' || *line == ':' || *line == '@') {
        char lead = *line;
        cmdbuf[n++] = *line++;
        while (*line == lead && n < (int)sizeof(cmdbuf) - 1)
            cmdbuf[n++] = *line++;
    } else {
        while (*line && !isspace(*line) &&
               n < (int)sizeof(cmdbuf) - 1)
            cmdbuf[n++] = *line++;
        if (*line)
            line++;
    }
    cmdbuf[n] = '\0';
    while (isspace(*line))
        line++;
    args = line;

    for (unsigned i = 0; i < NCMDS; i++) {
        if (strcmp(cmdbuf, cmds[i].name) == 0) {
            if (cmds[i].cost > 0 && cc[sid].fuel < cmds[i].cost) {
                session_write(sid,
                          "You're too busy to do that!");
                return;
            }
            cc[sid].fuel -= cmds[i].cost;
            fuel_schedule_regen(sid);
            cmds[i].fn(sid, args);
            status_update(sid);
            return;
        }
    }
    /* A dead player is an inert body (Section 11): action verbs are refused.
     * The passive host commands above (say, emote, help, recover) already ran;
     * of the object verbs only look and the sheet still work. */
    {
        struct obj *dsheet = player_sheet(cc[sid].obj);

        if (dsheet && prop_int(dsheet, "dead", 0) &&
            strcmp(cmdbuf, "look") != 0 && strcmp(cmdbuf, "sheet") != 0) {
            session_write(sid,
                "You are dead. Wait to recover, or 'recover' to pay for revival.");
            return;
        }
    }
    if (verb_dispatch(sid, cmdbuf, args) != OK)
        session_write(sid, "Unknown command. Type 'help' for a list.");
    else
        status_update(sid);
}

/****************************************************************
 * Web editor (M23)
 ****************************************************************/

static int
parse_qs(const char *path, int *obj, char *prop, int propsz, int *sid)
{
    const char *q = strchr(path, '?');

    if (!q)
        return ERR;
    q++;
    *obj = OBJ_NONE;
    *sid = -1;
    prop[0] = '\0';
    while (*q) {
        if (strncmp(q, "obj=", 4) == 0)
            *obj = strtol(q + 4, NULL, 10);
        else if (strncmp(q, "prop=", 5) == 0) {
            const char *end = strchr(q + 5, '&');
            int len = end ? (int)(end - q - 5) : (int)strlen(q + 5);
            if (len >= propsz)
                return ERR;
            memcpy(prop, q + 5, len);
            prop[len] = '\0';
        } else if (strncmp(q, "sid=", 4) == 0)
            *sid = strtol(q + 4, NULL, 10);
        q = strchr(q, '&');
        if (!q)
            break;
        q++;
    }
    return OK;
}

static void
handle_prop_get(int fd, const char *path)
{
    int objid, sid;
    char propname[64];
    struct obj *o;
    const char *atom;

    if (parse_qs(path, &objid, propname, sizeof(propname), &sid) != OK ||
        sid < 0 || sid >= MAX_CONN || cc[sid].state != CONN_SSE) {
        http_respond(fd, "403 Forbidden", "text/plain", "Forbidden", 9);
        return;
    }
    o = obj_find(objid);
    if (!o) {
        http_respond(fd, "404 Not Found", "text/plain",
                 "Object not found", 16);
        return;
    }
    atom = make_atom(propname);
    if (!atom) {
        http_respond(fd, "404 Not Found", "text/plain",
                 "Property not found", 18);
        return;
    }
    for (int i = 0; i < o->nprops; i++) {
        if (o->props[i].name == atom) {
            if (!perm_can_read(sid, o, &o->props[i])) {
                http_respond(fd, "403 Forbidden", "text/plain",
                         "Permission denied", 17);
                return;
            }
            if (o->props[i].val.type != VAL_STR) {
                http_respond(fd, "400 Bad Request", "text/plain",
                         "Not a string", 12);
                return;
            }
            http_respond(fd, "200 OK", "text/plain",
                     o->props[i].val.str,
                     strlen(o->props[i].val.str));
            return;
        }
    }
    http_respond(fd, "404 Not Found", "text/plain",
             "Property not found", 18);
}

static void
handle_prop_post(int fd, const char *path, const char *body, int bodylen)
{
    int objid, sid;
    char propname[64], *text;
    struct obj *o;
    const char *atom;

    if (parse_qs(path, &objid, propname, sizeof(propname), &sid) != OK ||
        sid < 0 || sid >= MAX_CONN || cc[sid].state != CONN_SSE) {
        http_respond(fd, "403 Forbidden", "text/plain", "Forbidden", 9);
        return;
    }
    o = obj_find(objid);
    if (!o) {
        http_respond(fd, "404 Not Found", "text/plain",
                 "Object not found", 16);
        return;
    }
    if (prop_is_reserved(propname)) {
        http_respond(fd, "403 Forbidden", "text/plain",
                 "Reserved property", 17);
        return;
    }
    atom = make_atom(propname);
    if (!atom) {
        http_respond(fd, "500 Internal Server Error", "text/plain",
                 "Atom failed", 11);
        return;
    }
    {
        int found = 0;
        for (int i = 0; i < o->nprops; i++) {
            if (o->props[i].name == atom) {
                found = 1;
                if (!perm_can_write(sid, o, &o->props[i])) {
                    http_respond(fd, "403 Forbidden", "text/plain",
                             "Permission denied", 17);
                    return;
                }
                break;
            }
        }
        if (!found && !is_wizard(sid) &&
            !obj_owner_match(o, sid)) {
            http_respond(fd, "403 Forbidden", "text/plain",
                     "Permission denied", 17);
            return;
        }
    }
    text = strndup(body, bodylen);
    if (!text) {
        http_respond(fd, "500 Internal Server Error", "text/plain",
                 "Out of memory", 13);
        return;
    }
    if (prop_set(o, atom, val_str(text)) != OK) {
        free(text);
        http_respond(fd, "500 Internal Server Error", "text/plain",
                 "Failed", 6);
        return;
    }
    free(text);
    http_respond(fd, "200 OK", "text/plain", "OK", 2);
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Property %s updated.", propname);
        session_write(sid, buf);
    }
}

/****************************************************************
 * HTTP request handling
 ****************************************************************/

static void
conn_dispatch(int i)
{
    char method[8], path[256], msg[256];
    int sid;
    char *body;

    if (sscanf(cc[i].buf, "%7s %255s", method, path) != 2) {
        conn_close(i);
        return;
    }

    if (strcmp(method, "GET") == 0 &&
        (strcmp(path, "/") == 0 ||
         strncmp(path, "/invite/", 8) == 0)) {
        serve_file(cc[i].fd, html_file, "text/html");
        conn_close(i);
    } else if (strcmp(method, "GET") == 0 &&
           strcmp(path, "/events") == 0) {
        dprintf(cc[i].fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "\r\n");
        sid = i;
        cc[i].state = CONN_SSE;
        cc[i].len = 0;
        cc[i].fuel = FUEL_MAX;
        cc[i].regen_timer = -1;
        cc[i].grit_timer = -1;
        cc[i].last_cmd = time(NULL);
        snprintf(msg, sizeof(msg), "%d", sid);
        out_puts(sid, 'I', msg);
        out_flush(sid);
    } else if (strcmp(method, "POST") == 0 &&
           strcmp(path, "/cmd") == 0) {
        body = strstr(cc[i].buf, "\r\n\r\n");
        if (body) {
            body += 4;
            sid = strtol(body, &body, 10);
            while (isspace(*body))
                body++;
            if (sid >= 0 && sid < MAX_CONN) {
                if (cc[sid].state == CONN_STALLED) {
                    http_respond(cc[i].fd,
                             "429 Too Many Requests",
                             "text/plain", "", 0);
                    conn_close(i);
                    return;
                }
                if (cc[sid].state == CONN_SSE) {
                    dispatch(sid, body);
                    cc[sid].last_cmd = time(NULL);
                }
            }
        }
        http_respond(cc[i].fd, "200 OK", "text/plain", "", 0);
        conn_close(i);
    } else if (strcmp(method, "GET") == 0 &&
           (strncmp(path, "/edit?", 6) == 0 ||
            strncmp(path, "/view?", 6) == 0)) {
        serve_file(cc[i].fd, "editor.html", "text/html");
        conn_close(i);
    } else if (strcmp(method, "GET") == 0 &&
           strncmp(path, "/prop?", 6) == 0) {
        handle_prop_get(cc[i].fd, path);
        conn_close(i);
    } else if (strcmp(method, "POST") == 0 &&
           strncmp(path, "/prop?", 6) == 0) {
        body = strstr(cc[i].buf, "\r\n\r\n");
        if (body) {
            body += 4;
            int clen = cc[i].len - (int)(body - cc[i].buf);
            handle_prop_post(cc[i].fd, path, body, clen);
        } else {
            http_respond(cc[i].fd, "400 Bad Request",
                     "text/plain", "No body", 7);
        }
        conn_close(i);
    } else {
        http_respond(cc[i].fd, "404 Not Found", "text/plain",
                 "Not Found", 9);
        conn_close(i);
    }
}

static void
conn_read(int i)
{
    int n, room;

    room = BUFSIZE - cc[i].len - 1;
    if (room <= 0) {
        conn_close(i);
        return;
    }
    n = read(cc[i].fd, cc[i].buf + cc[i].len, room);
    if (n <= 0) {
        conn_close(i);
        return;
    }
    cc[i].len += n;
    cc[i].buf[cc[i].len] = '\0';

    if (strstr(cc[i].buf, "\r\n\r\n"))
        conn_dispatch(i);
}

/****************************************************************
 * Main
 ****************************************************************/

/* Match id against export specs given as argv[start..argc).
 * Each spec is "N" (single) or "N-M" (inclusive range). With no
 * specs, everything matches. */
static int
id_in_specs(int id, int argc, char **argv, int start)
{
    if (start >= argc)
        return 1;
    for (int i = start; i < argc; i++) {
        char *dash;
        long lo = strtol(argv[i], &dash, 10);
        long hi = (*dash == '-') ? strtol(dash + 1, NULL, 10) : lo;
        if (id >= lo && id <= hi)
            return 1;
    }
    return 0;
}

static int
cmd_export(int argc, char **argv, int start)
{
    cas_store = cas_new(depot_dir);
    if (!cas_store) return 1;
    obj_map = cas_omap_new(cas_store);
    if (!obj_map) return 1;
    obj_init();
    if (world_load("world.data") != OK) return 1;

    for (int i = 0; i < MAX_OBJ; i++) {
        struct obj *o = &objs[i];
        char *buf;
        size_t len;
        if (o->id == OBJ_NONE || obj_is_ephemeral(o->id))
            continue;
        if (!id_in_specs(o->id, argc, argv, start))
            continue;
        if (obj_serialize(o, &buf, &len) == OK) {
            fwrite(buf, 1, len, stdout);
            putchar('\n');
            free(buf);
        }
    }
    world_free();
    cas_omap_free(obj_map);
    cas_free(cas_store);
    return 0;
}

/* Merge exported objects from a file into the current world,
 * renumbering them into a fresh contiguous range above the highest
 * existing object id. References among the merged objects are
 * rewritten to the new ids; references to objects outside the merge
 * set are left untouched. */
static int
cmd_merge(const char *path)
{
    char *data;
    long len;
    int from[MAX_OBJ], to[MAX_OBJ], n = 0, used = 0, maxid = 0, base;
    struct load_ctx lc = { 0 };

    cas_store = cas_new(depot_dir);
    if (!cas_store) return 1;
    obj_map = cas_omap_new(cas_store);
    if (!obj_map) return 1;
    obj_init();
    if (world_load("world.data") != OK) return 1;

    data = file_slurp(path, &len);
    if (!data) { perror(path); return 1; }

    /* pass 1: collect the distinct object ids in the input */
    {
        const char *p = data, *end = data + len;
        while (p < end) {
            const char *eol = memchr(p, '\n', end - p);
            if (!eol) eol = end;
            if (p < eol && *p == '#') {
                int id = (int)strtol(p + 1, NULL, 10), seen = 0;
                for (int i = 0; i < n; i++)
                    if (from[i] == id) { seen = 1; break; }
                if (!seen && n < MAX_OBJ)
                    from[n++] = id;
            }
            p = eol + 1;
        }
    }
    if (n == 0) {
        fprintf(stderr, "[merge] no objects in %s\n", path);
        free(data);
        return 1;
    }

    /* sort input ids so the new range mirrors their order */
    for (int i = 1; i < n; i++) {
        int v = from[i], j = i - 1;
        while (j >= 0 && from[j] > v) { from[j + 1] = from[j]; j--; }
        from[j + 1] = v;
    }

    for (int i = 0; i < MAX_OBJ; i++) {
        if (objs[i].id == OBJ_NONE)
            continue;
        used++;
        if (!obj_is_ephemeral(objs[i].id) && objs[i].id > maxid)
            maxid = objs[i].id;
    }
    if (used + n > MAX_OBJ) {
        fprintf(stderr, "[merge] not enough slots (%d + %d > %d)\n",
                used, n, MAX_OBJ);
        free(data);
        return 1;
    }

    base = maxid + 1;
    for (int i = 0; i < n; i++)
        to[i] = base + i;

    /* pass 2: load with the id remapping applied */
    lc.map_from = from;
    lc.map_to = to;
    lc.map_n = n;
    obj_load_buf(data, (size_t)len, &lc);
    free(data);

    if (world_save(NULL) != OK) {
        fprintf(stderr, "[merge] save failed\n");
        return 1;
    }
    fprintf(stderr, "[merge] %d objects from %s -> #%d..#%d\n",
            lc.count, path, base, base + n - 1);

    world_free();
    cas_omap_free(obj_map);
    cas_free(cas_store);
    return 0;
}

static int
cmd_migrate(void)
{
    cas_store = cas_new(depot_dir);
    if (!cas_store) return 1;
    obj_map = cas_omap_new(cas_store);
    if (!obj_map) return 1;
    obj_init();
    if (world_migrate("world.data") != OK) return 1;
    world_free();
    cas_omap_free(obj_map);
    cas_free(cas_store);
    return 0;
}

static int
cmd_install(const char *conf_path, const char *sdk)
{
    FILE *f;
    char line[BUFSIZE];
    int seen[1024];         /* object ids already defined in this conf */
    int nseen = 0;
    int had_dup = 0;

    cas_store = cas_new(depot_dir);
    if (!cas_store) return 1;
    obj_map = cas_omap_new(cas_store);
    if (!obj_map) return 1;
    obj_init();
    world_load("world.data");
    world_bootstrap();

    f = fopen(conf_path, "r");
    if (!f) { perror(conf_path); return 1; }

    while (fgets(line, sizeof(line), f)) {
        int len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' ||
               line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        int obj_id, parent;
        char verb_name[64], elf_src[256], mode_str[8] = "0755";
        char args_spec[128] = "";
        int nf = sscanf(line, "%d %d %63s %255s %7s %127[^\n]",
                         &obj_id, &parent, verb_name, elf_src,
                         mode_str, args_spec);
        if (nf < 4) continue;

        /* Reject a duplicate object id: two lines claiming the same id would
         * silently clobber each other (last write wins), leaving one verb
         * unresolvable. Report it and skip the line. */
        {
            int dup = 0;
            for (int i = 0; i < nseen; i++)
                if (seen[i] == obj_id) { dup = 1; break; }
            if (dup) {
                fprintf(stderr, "error: %s: duplicate object id %d "
                        "(verb %s); skipping\n", conf_path, obj_id, verb_name);
                had_dup = 1;
                continue;
            }
            if (nseen < (int)(sizeof(seen) / sizeof(seen[0])))
                seen[nseen++] = obj_id;
        }

        /* compile elf (check if .elf exists and is newer) */
        char elf_path[260];
        snprintf(elf_path, sizeof(elf_path), "%s", elf_src);
        char *dot = strrchr(elf_path, '.');
        if (dot) { strcpy(dot, ".elf"); }

        struct stat ss, se;
        if (stat(elf_src, &ss) != 0) {
            fprintf(stderr, "skip %s: source not found\n",
                    elf_src);
            continue;
        }
        int need_compile = (stat(elf_path, &se) != 0 ||
                            ss.st_mtime > se.st_mtime);
        if (need_compile) {
            char cmd[2048];
            const char *ext = strrchr(elf_src, '.');
            int ok = 0;

            if (ext && strcmp(ext, ".moo") == 0 && sdk) {
                /* MooScript verbs. skj-mooc-rv emits stack-convention RV32
                 * assembly; the verb links against the MooScript runtime
                 * (moo_rt entry/arena, host bridge, str/list, and the
                 * stack-convention syscall stubs), all built into the SDK by
                 * `make sdk`. The compiled verb exports `main`, which moo_rt's
                 * _start calls with the vm_args context. */
                snprintf(cmd, sizeof(cmd),
                    "%s/skj-mooc-rv -o %s/_verb.s %s && "
                    "%s/skj-as-rv -o %s/_verb.o %s/_verb.s && "
                    "%s/skj-ld-rv -T vm_rv.ld -o %s %s/_verb.o "
                    "%s/moo_rt.o %s/host_vm.o %s/str.o %s/list.o "
                    "%s/moo_syscall_rv.o",
                    sdk, sdk, elf_src,
                    sdk, sdk, sdk,
                    sdk, elf_path, sdk,
                    sdk, sdk, sdk, sdk,
                    sdk);
                fprintf(stderr, "[build:moo] %s\n", elf_src);
                ok = (system(cmd) == 0);
            } else if (ext && strcmp(ext, ".c") == 0 && sdk) {
                /* C verbs. skj-cc-rv-psabi emits the standard RISC-V ILP32
                 * psABI, matching the ecall stubs in verb_rt_rv.o, which also
                 * supplies the CRT _start that calls the verb's main(). A plain
                 * verb links only verb_rt_rv.o; an agent_*.c program also links
                 * verbmain.o for the generic event-loop main(). -I. finds
                 * mulibc.h / chromesix_verb.h at the repo root. */
                const char *base = strrchr(elf_src, '/');
                char aobj[160];

                base = base ? base + 1 : elf_src;
                if (strncmp(base, "agent_", 6) == 0)
                    snprintf(aobj, sizeof(aobj), " %s/verbmain.o", sdk);
                else
                    aobj[0] = '\0';
                snprintf(cmd, sizeof(cmd),
                    "%s/skj-cc-rv-psabi -I. -I sdk/runtime "
                    "-o %s/_verb.s %s && "
                    "%s/skj-as-rv -o %s/_verb.o "
                    "%s/_verb.s && "
                    "%s/skj-ld-rv -T vm_rv.ld -o %s "
                    "%s/_verb.o %s/verb_rt_rv.o%s",
                    sdk,
                    sdk, elf_src,
                    sdk, sdk,
                    sdk,
                    sdk, elf_path,
                    sdk, sdk, aobj);
                fprintf(stderr, "[build:skj] %s\n", elf_src);
                ok = (system(cmd) == 0);
            }
            if (!ok) {
                fprintf(stderr, "compile failed: %s\n", elf_src);
                continue;
            }
        }

        /* store ELF in CAS */
        FILE *ef = fopen(elf_path, "rb");
        if (!ef) { perror(elf_path); continue; }
        fseek(ef, 0, SEEK_END);
        size_t efsz = ftell(ef);
        rewind(ef);
        unsigned char *edata = malloc(efsz);
        if (!edata) { fclose(ef); continue; }
        if (fread(edata, 1, efsz, ef) != efsz) {
            free(edata); fclose(ef); continue;
        }
        fclose(ef);

        char hash[CAS_HASH_HEX + 1];
        if (cas_put(cas_store, edata, efsz, hash) != CAS_OK) {
            free(edata); continue;
        }
        free(edata);

        /* create or update verb object */
        struct obj *o = obj_find(obj_id);
        if (!o) o = obj_create(obj_id, parent);
        if (!o) {
            fprintf(stderr, "cannot create #%d\n", obj_id);
            continue;
        }
        o->parent = parent;
        /* Verbs installed from verbs.conf are trusted system verbs: owned by
           the System Object (#0), they carry wizard authority when they run,
           so combat and other shared-state verbs work for any player. */
        o->owner = 0;
        prop_set(o, make_atom("verb"), val_str(verb_name));

        char elf_val[128];
        snprintf(elf_val, sizeof(elf_val), "[%s,b2:%s]",
                 mode_str, hash);
        prop_set(o, make_atom("elf"), val_str(elf_val));

        if (args_spec[0])
            prop_set(o, make_atom("args"), val_str(args_spec));

        fprintf(stderr, "[install] #%d verb=%s elf=%s\n",
                obj_id, verb_name, hash);
    }
    fclose(f);

    world_save(NULL);
    world_free();
    cas_omap_free(obj_map);
    cas_free(cas_store);
    return had_dup ? 1 : 0;
}

int
main(int argc, char *argv[])
{
    int port = 7777, lfd, fd, maxfd, n, opt, bootstrap = 0;
    struct sockaddr_in addr;
    fd_set rfds, wfds;
    char tmp[1];

    if (argc < 2) {
        fprintf(stderr,
            "usage: smolmoo <command> [options]\n"
            "commands:\n"
            "  serve [--bootstrap]  start the server\n"
            "  install [--sdk DIR] <verbs.conf>\n"
            "  export [N|N-M ...]   write world objects to stdout\n"
            "  merge <file>         import objects, renumbered\n"
            "  migrate              migrate world.data to omap\n"
            "\n"
            "environment:\n"
            "  SMOLMOO_PORT   listen port (default 7777)\n"
            "  SMOLMOO_DEPOT  depot directory (default depot)\n"
            "  SMOLMOO_HTML   html file (default index.html)\n");
        return 1;
    }

    { const char *v;
        if ((v = getenv("SMOLMOO_DEPOT"))) depot_dir = v;
        if ((v = getenv("SMOLMOO_HTML"))) html_file = v;
    }
    snprintf(root_file, sizeof(root_file), "%s/root", depot_dir);
    snprintf(head_file, sizeof(head_file), "%s/head", depot_dir);
    { const char *v = getenv("SMOLMOO_KEY");
        if (v) snprintf(key_file, sizeof(key_file), "%s", v);
        else   snprintf(key_file, sizeof(key_file), "%s.key", depot_dir);
    }

    if (strcmp(argv[1], "export") == 0 || strcmp(argv[1], "dump") == 0)
        return cmd_export(argc, argv, 2);
    if (strcmp(argv[1], "merge") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: smolmoo merge <file>\n");
            return 1;
        }
        return cmd_merge(argv[2]);
    }
    if (strcmp(argv[1], "migrate") == 0)
        return cmd_migrate();
    if (strcmp(argv[1], "install") == 0) {
        const char *sdk_dir = NULL, *conf = NULL;
        char sdk_buf[4096];
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--sdk") == 0 && i + 1 < argc)
                sdk_dir = argv[++i];
            else
                conf = argv[i];
        }
        if (!conf) {
            fprintf(stderr,
                "usage: smolmoo install [--sdk DIR] <verbs.conf>\n");
            return 1;
        }
        if (!sdk_dir) {
            ssize_t len = readlink("/proc/self/exe",
                sdk_buf, sizeof(sdk_buf) - 5);
            if (len > 0) {
                sdk_buf[len] = '\0';
                char *sl = strrchr(sdk_buf, '/');
                if (sl) {
                    strcpy(sl + 1, "sdk");
                    sdk_dir = sdk_buf;
                }
            }
        }
        return cmd_install(conf, sdk_dir);
    }

    if (strcmp(argv[1], "serve") != 0) {
        fprintf(stderr, "unknown command: %s\n", argv[1]);
        return 1;
    }

    { const char *v;
        if ((v = getenv("SMOLMOO_PORT"))) port = atoi(v);
        if ((v = getenv("SMOLMOO_DEATH_MS"))) death_recover_ms = atoi(v);
    }

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--bootstrap") == 0)
            bootstrap = 1;
    }

    cas_store = cas_new(depot_dir);
    if (!cas_store) {
        fprintf(stderr, "failed to initialize CAS\n");
        return 1;
    }
    obj_map = cas_omap_new(cas_store);
    if (!obj_map) {
        fprintf(stderr, "failed to initialize object map\n");
        return 1;
    }

    obj_init();
    if (world_load("world.data") != OK)
        return 1;
    world_bootstrap();
    task_init();
    timer_init();
    /* reserve the system session (slot 0): agent tasks run under it, so the
       task loop never reaps them and the accept path never hands the slot to a
       client (OLC-5). */
    cc[SYS_SID].state = CONN_SYS;
    cc[SYS_SID].obj = OBJ_NONE;
    history_init();
    if (saver_start() != OK) {
        fprintf(stderr, "failed to start save thread\n");
        return 1;
    }
    timer_add(AUTOSAVE_MS, autosave_cb, NULL);
    timer_add(INVITE_REFRESH_MS, invite_refresh_cb, NULL);
    timer_add(DEATH_SWEEP_MS, death_sweep_cb, NULL);

    /* bootstrap admin invite on first boot or --bootstrap */
    {
        int have_accts = 0;
        for (int i = 0; i < MAX_OBJ; i++)
            if (objs[i].id != OBJ_NONE &&
                objs[i].parent == ACCT_PARENT)
                { have_accts = 1; break; }
        if (bootstrap || !have_accts) {
            struct obj *inv = invite_create(OBJ_NONE, 50, 1);
            if (inv) {
                const char *code = prop_str(inv,
                    make_atom("code"));
                fprintf(stderr,
                    "[bootstrap] admin invite: %s\n",
                    code ?: "???");
            }
        }
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("socket");
        return 1;
    }
    opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(lfd);
        return 1;
    }
    if (listen(lfd, 8) < 0) {
        perror("listen");
        close(lfd);
        return 1;
    }
    printf("smolmoo listening on port %d\n", port);

    while (running) {
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(lfd, &rfds);
        maxfd = lfd;
        for (int i = 0; i < MAX_CONN; i++) {
            if (cc[i].fd > 0) {
                if (cc[i].state == CONN_PENDING ||
                    cc[i].state == CONN_SSE)
                    FD_SET(cc[i].fd, &rfds);
                if (cc[i].len > 0 &&
                    (cc[i].state == CONN_SSE ||
                     cc[i].state == CONN_STALLED))
                    FD_SET(cc[i].fd, &wfds);
                if (cc[i].fd > maxfd)
                    maxfd = cc[i].fd;
            }
        }

        {
            struct timeval tv, *tvp = NULL;
            int tms = timer_next_ms();
            int has_ready = 0;

            for (int i = 0; i < MAX_TASK && !has_ready; i++)
                has_ready = (tasks[i].state == TASK_READY);
            if (has_ready) {
                tv.tv_sec = 0;
                tv.tv_usec = 0;
                tvp = &tv;
            } else if (tms >= 0) {
                tv.tv_sec = tms / 1000;
                tv.tv_usec = (tms % 1000) * 1000;
                tvp = &tv;
            }
            n = select(maxfd + 1, &rfds, &wfds, NULL, tvp);
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        if (FD_ISSET(lfd, &rfds)) {
            fd = accept(lfd, NULL, NULL);
            if (fd >= 0) {
                int slot = -1;
                fcntl(fd, F_SETFL, O_NONBLOCK);
                for (int i = 0; i < MAX_CONN; i++) {
                    if (cc[i].state == CONN_FREE) {
                        slot = i;
                        break;
                    }
                }
                if (slot < 0) {
                    http_respond(fd, "503 Service Unavailable",
                             "text/plain",
                             "Server full", 11);
                    close(fd);
                } else {
                    memset(&cc[slot], 0, sizeof(cc[slot]));
                    cc[slot].fd = fd;
                    cc[slot].state = CONN_PENDING;
                    cc[slot].obj = OBJ_NONE;
                }
            }
        }

        for (int i = 0; i < MAX_CONN; i++) {
            if (cc[i].fd > 0 && FD_ISSET(cc[i].fd, &rfds)) {
                if (cc[i].state == CONN_PENDING)
                    conn_read(i);
                else if (cc[i].state == CONN_SSE) {
                    if (read(cc[i].fd, tmp, 1) <= 0)
                        conn_close(i);
                }
            }
        }

        for (int i = 0; i < MAX_CONN; i++) {
            if (cc[i].fd > 0 && cc[i].len > 0 &&
                FD_ISSET(cc[i].fd, &wfds)) {
                if (out_flush(i) != OK)
                    conn_close(i);
                else if (cc[i].state == CONN_STALLED &&
                     cc[i].len <= BUFSIZE / 2)
                    cc[i].state = CONN_SSE;
            }
        }

        timer_dispatch();
        task_run_all();
    }

    for (int i = 0; i < MAX_CONN; i++)
        conn_close(i);
    close(lfd);
    timer_shutdown();
    task_shutdown();
    world_save_async();     /* queue a final snapshot of remaining changes */
    saver_stop();           /* drain the queue and join the writer thread */
    world_free();
    cas_omap_free(obj_map);
    cas_free(cas_store);
    return 0;
}
