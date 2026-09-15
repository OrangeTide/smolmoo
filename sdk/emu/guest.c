/* guest.c : sparse guest memory, the syscall layer, and the initial stack
 * Copyright (c) 2026 Jon Mayo
 * SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "guest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/****************************************************************
 * Sparse memory
 ****************************************************************/

void
gm_init(guest_mem *m, uint64_t limit)
{
    memset(m, 0, sizeof(*m));
    m->limit = limit;
}

void
gm_free(guest_mem *m)
{
    int i, j;

    for (i = 0; i < GM_L1_ENTRIES; i++) {
        if (!m->dir[i])
            continue;
        for (j = 0; j < GM_L2_ENTRIES; j++)
            free(m->dir[i][j]);
        free(m->dir[i]);
        m->dir[i] = NULL;
    }
    m->committed = 0;
}

int
gm_map(guest_mem *m, uint32_t base, uint32_t size, int prot, const char *name)
{
    struct gm_region *r;

    if (m->nregions >= GM_MAX_REGIONS)
        return -1;
    r = &m->regions[m->nregions++];
    r->base = base;
    r->size = size;
    r->prot = prot;
    r->name = name;
    return 0;
}

/** Region containing addr, or NULL.  Accesses cluster, so the last hit is
 * tried first and the scan is the exception. */
static struct gm_region *
region_of(guest_mem *m, uint32_t addr)
{
    struct gm_region *r = m->last;
    int i;

    if (r && addr >= r->base && addr - r->base < r->size)
        return r;
    for (i = 0; i < m->nregions; i++) {
        r = &m->regions[i];
        if (addr >= r->base && addr - r->base < r->size) {
            m->last = r;
            return r;
        }
    }
    return NULL;
}

/** Record the first fault; later ones do not overwrite it. */
static void
gm_fault(guest_mem *m, uint32_t addr, int is_write, const char *why)
{
    if (m->fault)
        return;
    m->fault = 1;
    m->fault_addr = addr;
    m->fault_write = is_write;
    m->fault_why = why;
}

uint8_t *
gm_page(guest_mem *m, uint32_t addr, int want_write)
{
    uint32_t l1 = addr >> 22;
    uint32_t l2 = (addr >> GM_PAGE_SHIFT) & (GM_L2_ENTRIES - 1);
    struct gm_region *r;
    uint8_t *page = m->dir[l1] ? m->dir[l1][l2] : NULL;

    /* A committed page satisfies a read outright; a write still has to
     * clear the region's protection. */
    if (page && !want_write)
        return page;

    r = region_of(m, addr);
    if (!r) {
        gm_fault(m, addr, want_write, "unmapped");
        return NULL;
    }
    if (want_write && !(r->prot & GM_W) && !m->loading) {
        gm_fault(m, addr, 1, "read-only");
        return NULL;
    }
    if (page)
        return page;
    if (m->committed + GM_PAGE_SIZE > m->limit) {
        gm_fault(m, addr, want_write, "memory limit");
        return NULL;
    }

    if (!m->dir[l1]) {
        m->dir[l1] = calloc(GM_L2_ENTRIES, sizeof(uint8_t *));
        if (!m->dir[l1]) {
            gm_fault(m, addr, want_write, "out of host memory");
            return NULL;
        }
    }
    page = calloc(1, GM_PAGE_SIZE);
    if (!page) {
        gm_fault(m, addr, want_write, "out of host memory");
        return NULL;
    }
    m->dir[l1][l2] = page;
    m->committed += GM_PAGE_SIZE;
    return page;
}

uint8_t
gm_read8(guest_mem *m, uint32_t addr)
{
    uint8_t *p = gm_page(m, addr, 0);

    return p ? p[addr & (GM_PAGE_SIZE - 1)] : 0;
}

void
gm_write8(guest_mem *m, uint32_t addr, uint8_t val)
{
    uint8_t *p = gm_page(m, addr, 1);

    if (p)
        p[addr & (GM_PAGE_SIZE - 1)] = val;
}

uint32_t
gm_load(guest_mem *m, uint32_t addr, int size, int be)
{
    uint32_t off = addr & (GM_PAGE_SIZE - 1);
    uint8_t b[4];
    int i;

    if (off + (uint32_t)size <= GM_PAGE_SIZE) {
        uint8_t *p = gm_page(m, addr, 0);
        if (!p)
            return 0;
        memcpy(b, p + off, (size_t)size);
    } else {
        for (i = 0; i < size; i++)
            b[i] = gm_read8(m, addr + (uint32_t)i);
    }

    switch (size) {
    case 1:
        return b[0];
    case 2:
        return be ? (uint32_t)((b[0] << 8) | b[1])
                  : (uint32_t)(b[0] | (b[1] << 8));
    default:
        return be ? ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
                        ((uint32_t)b[2] << 8) | b[3]
                  : (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                        ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    }
}

void
gm_store(guest_mem *m, uint32_t addr, int size, int be, uint32_t val)
{
    uint32_t off = addr & (GM_PAGE_SIZE - 1);
    uint8_t b[4];
    int i;

    switch (size) {
    case 1:
        b[0] = (uint8_t)val;
        break;
    case 2:
        b[0] = be ? (uint8_t)(val >> 8) : (uint8_t)val;
        b[1] = be ? (uint8_t)val : (uint8_t)(val >> 8);
        break;
    default:
        for (i = 0; i < 4; i++)
            b[i] = be ? (uint8_t)(val >> (24 - 8 * i))
                      : (uint8_t)(val >> (8 * i));
        break;
    }

    if (off + (uint32_t)size <= GM_PAGE_SIZE) {
        uint8_t *p = gm_page(m, addr, 1);
        if (p)
            memcpy(p + off, b, (size_t)size);
        return;
    }
    for (i = 0; i < size; i++)
        gm_write8(m, addr + (uint32_t)i, b[i]);
}

void
gm_write(guest_mem *m, uint32_t addr, const void *src, uint32_t len)
{
    const uint8_t *s = src;
    uint32_t i;

    for (i = 0; i < len; i++)
        gm_write8(m, addr + i, s[i]);
}

void
gm_zero(guest_mem *m, uint32_t addr, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++)
        gm_write8(m, addr + i, 0);
}

/****************************************************************
 * Syscalls
 *
 * Only the calls a freestanding guest can reach.  Everything else
 * answers -ENOSYS the way Linux would, so an unexpected call shows up as
 * a guest-visible error rather than a silent success.
 ****************************************************************/

#define GUEST_EBADF     9
#define GUEST_ENOMEM    12
#define GUEST_EFAULT    14
#define GUEST_ENOSYS    38

/** Copy a guest buffer into a host one.  Returns 0 on a fault. */
static int
copy_from_guest(guest *g, uint32_t addr, void *dst, uint32_t len)
{
    uint8_t *d = dst;
    uint32_t i;

    for (i = 0; i < len; i++) {
        uint8_t *p = gm_page(&g->mem, addr + i, 0);
        if (!p)
            return 0;
        d[i] = p[(addr + i) & (GM_PAGE_SIZE - 1)];
    }
    return 1;
}

static int32_t
sys_write(guest *g, uint32_t fd, uint32_t buf, uint32_t len)
{
    uint8_t tmp[4096];
    uint32_t done = 0;

    if (fd != 1 && fd != 2)
        return -GUEST_EBADF;
    while (done < len) {
        uint32_t chunk = len - done;
        ssize_t n;
        if (chunk > sizeof(tmp))
            chunk = sizeof(tmp);
        if (!copy_from_guest(g, buf + done, tmp, chunk))
            return -GUEST_EFAULT;
        if (g->quiet) {
            done += chunk;
            continue;
        }
        n = write((int)fd, tmp, chunk);
        if (n <= 0)
            return done ? (int32_t)done : -GUEST_EBADF;
        done += (uint32_t)n;
    }
    return (int32_t)done;
}

static int32_t
sys_read(guest *g, uint32_t fd, uint32_t buf, uint32_t len)
{
    uint8_t tmp[4096];
    ssize_t n;

    if (fd != 0)
        return -GUEST_EBADF;
    if (len > sizeof(tmp))
        len = sizeof(tmp);
    n = read(0, tmp, len);
    if (n < 0)
        return -GUEST_EBADF;
    gm_write(&g->mem, buf, tmp, (uint32_t)n);
    if (g->mem.fault)
        return -GUEST_EFAULT;
    return (int32_t)n;
}

/* Linux brk: return the new break, or the current one when the request
 * cannot be met.  The heap is a region mapped at startup, so growing the
 * break only moves a pointer inside it. */
static int32_t
sys_brk(guest *g, uint32_t want)
{
    if (want >= g->brk_base && want <= g->brk_limit)
        g->brk = want;
    return (int32_t)g->brk;
}

int32_t
guest_syscall(guest *g, int sys, uint32_t a0, uint32_t a1, uint32_t a2,
              uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a3;
    (void)a4;
    (void)a5;

    g->syscalls++;

    switch (sys) {
    case GSYS_WRITE:
        return sys_write(g, a0, a1, a2);

    case GSYS_READ:
        return sys_read(g, a0, a1, a2);

    case GSYS_WRITEV: {
        /* struct iovec { void *base; size_t len; } laid out in guest
         * order; both targets are ILP32 so an entry is two words. */
        uint32_t i, total = 0;
        for (i = 0; i < a2; i++) {
            uint32_t ent = a1 + i * 8;
            uint8_t w[8];
            uint32_t base, len;
            int32_t r;
            if (!copy_from_guest(g, ent, w, 8))
                return -GUEST_EFAULT;
            if (g->big_endian) {
                base = ((uint32_t)w[0] << 24) | ((uint32_t)w[1] << 16) |
                       ((uint32_t)w[2] << 8) | w[3];
                len = ((uint32_t)w[4] << 24) | ((uint32_t)w[5] << 16) |
                      ((uint32_t)w[6] << 8) | w[7];
            } else {
                base = (uint32_t)w[0] | ((uint32_t)w[1] << 8) |
                       ((uint32_t)w[2] << 16) | ((uint32_t)w[3] << 24);
                len = (uint32_t)w[4] | ((uint32_t)w[5] << 8) |
                      ((uint32_t)w[6] << 16) | ((uint32_t)w[7] << 24);
            }
            r = sys_write(g, a0, base, len);
            if (r < 0)
                return total ? (int32_t)total : r;
            total += (uint32_t)r;
        }
        return (int32_t)total;
    }

    case GSYS_BRK:
        return sys_brk(g, a0);

    case GSYS_CLOSE:
        return 0;

    case GSYS_LSEEK:
        return -GUEST_EBADF;

    case GSYS_EXIT:
    case GSYS_EXIT_GROUP:
        g->exited = 1;
        g->exit_code = (int)(a0 & 0xFF);
        return 0;

    default:
        return -GUEST_ENOSYS;
    }
}

/****************************************************************
 * Initial stack
 *
 * The layout Linux hands a fresh process: argc, the argv pointers, a
 * NULL, the envp pointers, a NULL, then a terminated auxv.  The strings
 * live above it.  Skjegg's own start.S switches to a private stack and
 * ignores all of this, but a guest built against a real libc does not,
 * and building it costs nothing.
 ****************************************************************/

static void
poke32(guest *g, uint32_t addr, uint32_t val)
{
    if (g->big_endian) {
        gm_write8(&g->mem, addr + 0, (uint8_t)(val >> 24));
        gm_write8(&g->mem, addr + 1, (uint8_t)(val >> 16));
        gm_write8(&g->mem, addr + 2, (uint8_t)(val >> 8));
        gm_write8(&g->mem, addr + 3, (uint8_t)val);
    } else {
        gm_write8(&g->mem, addr + 0, (uint8_t)val);
        gm_write8(&g->mem, addr + 1, (uint8_t)(val >> 8));
        gm_write8(&g->mem, addr + 2, (uint8_t)(val >> 16));
        gm_write8(&g->mem, addr + 3, (uint8_t)(val >> 24));
    }
}

#define AT_NULL     0
#define AT_PAGESZ   6

uint32_t
guest_build_stack(guest *g, const run_opts *o)
{
    uint32_t sp = g->stack_top;
    uint32_t str[64] = { 0 };
    int argc = o->argc;
    int i;

    if (argc > (int)(sizeof(str) / sizeof(str[0])))
        argc = (int)(sizeof(str) / sizeof(str[0]));

    /* strings, top down */
    for (i = argc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(o->argv[i]) + 1;
        sp -= len;
        gm_write(&g->mem, sp, o->argv[i], len);
        str[i] = sp;
    }
    sp &= ~15u;

    /* auxv (two words), the envp NULL, the argv NULL, argv, argc */
    sp -= 4 * (2 + 2 + 1 + 1 + (uint32_t)argc + 1);
    sp &= ~15u;

    {
        uint32_t p = sp;
        poke32(g, p, (uint32_t)argc);
        p += 4;
        for (i = 0; i < argc; i++, p += 4)
            poke32(g, p, str[i]);
        poke32(g, p, 0);            /* argv terminator */
        p += 4;
        poke32(g, p, 0);            /* envp terminator */
        p += 4;
        poke32(g, p, AT_PAGESZ);
        p += 4;
        poke32(g, p, GM_PAGE_SIZE);
        p += 4;
        poke32(g, p, AT_NULL);
        p += 4;
        poke32(g, p, 0);
    }
    return sp;
}
