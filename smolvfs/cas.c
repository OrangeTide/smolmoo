/* cas.c : content-addressable store using BLAKE2b hashing */
/* Copyright (c) 2026 Jon Mayo <jon@rm-f.net>
 * SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#define _POSIX_C_SOURCE 200809L

#include "cas.h"
#include "cas-pack.h"
#include "cas-codec.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/****************************************************************
 * Internal constants
 ****************************************************************/

#define CAS_PATH_MAX    512
#define CAS_HEADER_MAX  32
#define BLAKE2B_BLOCKLEN 128

/****************************************************************
 * Error strings
 ****************************************************************/

const char *
cas_strerror(int err)
{
    switch (err) {
    case CAS_OK:        return "success";
    case CAS_ERR:       return "generic error";
    case CAS_ENOTFOUND: return "not found";
    case CAS_ENOMEM:    return "out of memory";
    case CAS_EIO:       return "I/O error";
    case CAS_ETYPE:     return "type mismatch";
    default:            return "unknown error";
    }
}

/****************************************************************
 * BLAKE2b-256 (RFC 7693)
 ****************************************************************/

struct blake2b {
    uint64_t h[8];
    uint64_t t[2];
    uint64_t f[2];
    uint8_t buf[BLAKE2B_BLOCKLEN];
    size_t buflen;
    size_t outlen;
};

static const uint64_t blake2b_iv[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

static const uint8_t blake2b_sigma[12][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3, },
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4, },
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8, },
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13, },
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9, },
    { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11, },
    { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10, },
    {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5, },
    { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0, },
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3, },
};

static uint64_t
load64(const void *src)
{
    const uint8_t *p = src;

    return ((uint64_t)p[0])       | ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static void
store64(void *dst, uint64_t w)
{
    uint8_t *p = dst;

    p[0] = (uint8_t)(w);
    p[1] = (uint8_t)(w >> 8);
    p[2] = (uint8_t)(w >> 16);
    p[3] = (uint8_t)(w >> 24);
    p[4] = (uint8_t)(w >> 32);
    p[5] = (uint8_t)(w >> 40);
    p[6] = (uint8_t)(w >> 48);
    p[7] = (uint8_t)(w >> 56);
}

#define ROTR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static void
blake2b_g(uint64_t *v, int a, int b, int c, int d,
          uint64_t x, uint64_t y)
{
    v[a] = v[a] + v[b] + x;
    v[d] = ROTR64(v[d] ^ v[a], 32);
    v[c] = v[c] + v[d];
    v[b] = ROTR64(v[b] ^ v[c], 24);
    v[a] = v[a] + v[b] + y;
    v[d] = ROTR64(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];
    v[b] = ROTR64(v[b] ^ v[c], 63);
}

static void
blake2b_compress(struct blake2b *S, const uint8_t *block)
{
    uint64_t m[16], v[16];

    for (int i = 0; i < 16; i++)
        m[i] = load64(block + i * 8);

    for (int i = 0; i < 8; i++) {
        v[i] = S->h[i];
        v[i + 8] = blake2b_iv[i];
    }
    v[12] ^= S->t[0];
    v[13] ^= S->t[1];
    v[14] ^= S->f[0];
    v[15] ^= S->f[1];

    for (int i = 0; i < 12; i++) {
        const uint8_t *s = blake2b_sigma[i];

        blake2b_g(v, 0, 4,  8, 12, m[s[ 0]], m[s[ 1]]);
        blake2b_g(v, 1, 5,  9, 13, m[s[ 2]], m[s[ 3]]);
        blake2b_g(v, 2, 6, 10, 14, m[s[ 4]], m[s[ 5]]);
        blake2b_g(v, 3, 7, 11, 15, m[s[ 6]], m[s[ 7]]);
        blake2b_g(v, 0, 5, 10, 15, m[s[ 8]], m[s[ 9]]);
        blake2b_g(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
        blake2b_g(v, 2, 7,  8, 13, m[s[12]], m[s[13]]);
        blake2b_g(v, 3, 4,  9, 14, m[s[14]], m[s[15]]);
    }

    for (int i = 0; i < 8; i++)
        S->h[i] ^= v[i] ^ v[i + 8];
}

static void
blake2b_init(struct blake2b *S, size_t outlen)
{
    memset(S, 0, sizeof(*S));
    for (int i = 0; i < 8; i++)
        S->h[i] = blake2b_iv[i];
    S->h[0] ^= 0x01010000 ^ (uint64_t)outlen;
    S->outlen = outlen;
}

static void
blake2b_update(struct blake2b *S, const void *data, size_t len)
{
    const uint8_t *p = data;
    size_t left, fill;

    if (len == 0)
        return;

    left = S->buflen;
    fill = BLAKE2B_BLOCKLEN - left;

    if (len > fill) {
        memcpy(S->buf + left, p, fill);
        S->t[0] += BLAKE2B_BLOCKLEN;
        S->t[1] += (S->t[0] < BLAKE2B_BLOCKLEN);
        blake2b_compress(S, S->buf);
        p += fill;
        len -= fill;
        S->buflen = 0;

        while (len > BLAKE2B_BLOCKLEN) {
            S->t[0] += BLAKE2B_BLOCKLEN;
            S->t[1] += (S->t[0] < BLAKE2B_BLOCKLEN);
            blake2b_compress(S, p);
            p += BLAKE2B_BLOCKLEN;
            len -= BLAKE2B_BLOCKLEN;
        }
    }

    memcpy(S->buf + S->buflen, p, len);
    S->buflen += len;
}

static void
blake2b_final(struct blake2b *S, void *out)
{
    uint8_t tmp[64];

    S->t[0] += (uint64_t)S->buflen;
    S->t[1] += (S->t[0] < (uint64_t)S->buflen);
    S->f[0] = ~(uint64_t)0;

    memset(S->buf + S->buflen, 0, BLAKE2B_BLOCKLEN - S->buflen);
    blake2b_compress(S, S->buf);

    for (int i = 0; i < 8; i++)
        store64(tmp + i * 8, S->h[i]);
    memcpy(out, tmp, S->outlen);
}

/****************************************************************
 * Hex encoding and hash validation
 ****************************************************************/

static const char hex_chars[] = "0123456789abcdef";

static void
to_hex(const uint8_t *bin, size_t len, char *out)
{
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex_chars[bin[i] >> 4];
        out[i * 2 + 1] = hex_chars[bin[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static int
valid_hash(const char *hash)
{
    if (strlen(hash) != CAS_HASH_HEX)
        return 0;
    for (int i = 0; i < CAS_HASH_HEX; i++) {
        char c = hash[i];

        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

/****************************************************************
 * Path helpers
 ****************************************************************/

struct cas {
    char *basedir;
    struct cas_pack *pack;
};

static int
build_path(const struct cas *store, const char *hash,
           char *buf, size_t bufsz)
{
    int n = snprintf(buf, bufsz, "%s/%.2s/%s",
                     store->basedir, hash, hash);

    if (n < 0 || (size_t)n >= bufsz)
        return CAS_ERR;
    return CAS_OK;
}

static int
build_dir(const struct cas *store, const char *hash,
          char *buf, size_t bufsz)
{
    int n = snprintf(buf, bufsz, "%s/%.2s",
                     store->basedir, hash);

    if (n < 0 || (size_t)n >= bufsz)
        return CAS_ERR;
    return CAS_OK;
}

/****************************************************************
 * Lifecycle
 ****************************************************************/

struct cas *
cas_new(const char *basedir)
{
    struct cas *store = calloc(1, sizeof(*store));

    if (!store)
        return NULL;
    store->basedir = strdup(basedir ? basedir : "depot");
    if (!store->basedir) {
        free(store);
        return NULL;
    }

    char packpath[CAS_PATH_MAX];

    if (snprintf(packpath, sizeof(packpath), "%s/pack.dat",
                 store->basedir) < (int)sizeof(packpath))
        store->pack = cas_pack_open(packpath);

    return store;
}

void
cas_free(struct cas *store)
{
    if (!store)
        return;
    cas_pack_close(store->pack);
    free(store->basedir);
    free(store);
}

const char *
cas_basedir(struct cas *store)
{
    return store->basedir;
}

/****************************************************************
 * Write / header helpers
 ****************************************************************/

static int
write_full(int fd, const void *data, size_t len)
{
    const uint8_t *p = data;

    while (len > 0) {
        ssize_t n = write(fd, p, len);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return CAS_EIO;
        }
        p += n;
        len -= (size_t)n;
    }
    return CAS_OK;
}

/** Format "type len\0" into buf, return total length including NUL. */
static int
format_header(char *buf, size_t bufsz, const char *type, size_t len)
{
    int n = snprintf(buf, bufsz, "%s %zu", type, len);

    if (n < 0 || (size_t)n + 1 > bufsz)
        return -1;
    return n + 1;
}

/** Parse "type len\0" from a nul-padded header buffer.
 *  Copies type into type_out (NUL-terminated).
 *  Sets content_len on success.
 */
static int
parse_header(const char *hdr, size_t hdrsz,
             char *type_out, size_t type_bufsz,
             size_t *content_len)
{
    size_t limit = hdrsz < CAS_HEADER_MAX ? hdrsz : CAS_HEADER_MAX;
    const char *nul = memchr(hdr, '\0', limit);

    if (!nul)
        return CAS_ERR;

    const char *sp = memchr(hdr, ' ', (size_t)(nul - hdr));

    if (!sp)
        return CAS_ERR;

    size_t typelen = (size_t)(sp - hdr);

    if (typelen == 0 || typelen > CAS_TYPE_MAX ||
        typelen + 1 > type_bufsz)
        return CAS_ERR;

    const char *d = sp + 1;

    if (d >= nul)
        return CAS_ERR;

    size_t len = 0;

    for (; d < nul; d++) {
        if (*d < '0' || *d > '9')
            return CAS_ERR;
        size_t prev = len;

        len = len * 10 + (size_t)(*d - '0');
        if (len < prev)
            return CAS_ERR;
    }

    memcpy(type_out, hdr, typelen);
    type_out[typelen] = '\0';
    *content_len = len;
    return CAS_OK;
}

/****************************************************************
 * Operations
 ****************************************************************/

int
cas_hash(const void *data, size_t len, char *hash_out)
{
    struct blake2b state;
    uint8_t digest[CAS_HASH_LEN];

    blake2b_init(&state, CAS_HASH_LEN);
    blake2b_update(&state, data, len);
    blake2b_final(&state, digest);
    to_hex(digest, CAS_HASH_LEN, hash_out);
    return CAS_OK;
}

int
cas_hash_object(const char *type, const void *data, size_t len,
                char *hash_out)
{
    char hdr[CAS_HEADER_MAX];
    int hdrlen = format_header(hdr, sizeof(hdr), type, len);

    if (hdrlen < 0)
        return CAS_ERR;

    struct blake2b state;
    uint8_t digest[CAS_HASH_LEN];

    blake2b_init(&state, CAS_HASH_LEN);
    blake2b_update(&state, hdr, (size_t)hdrlen);
    blake2b_update(&state, data, len);
    blake2b_final(&state, digest);
    to_hex(digest, CAS_HASH_LEN, hash_out);
    return CAS_OK;
}

/****************************************************************
 * cas_file teardown
 ****************************************************************/

static void
release_munmap(struct cas_file *cf)
{
    munmap(cf->_priv, cf->_privlen);
}

static void
release_free(struct cas_file *cf)
{
    free(cf->_priv);
}

int
cas_open_object(struct cas *store, struct cas_file *cf,
                const char *hash, char *type_out, size_t type_bufsz)
{
    if (!valid_hash(hash))
        return CAS_ERR;

    if (store->pack) {
        int rc = cas_pack_lookup(store->pack, cf, hash,
                                 type_out, type_bufsz);

        if (rc != CAS_ENOTFOUND)
            return rc;
    }

    char path[CAS_PATH_MAX];

    if (build_path(store, hash, path, sizeof(path)) != CAS_OK)
        return CAS_ERR;

    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return CAS_ENOTFOUND;

    struct stat sb;

    if (fstat(fd, &sb) != 0) {
        close(fd);
        return CAS_EIO;
    }

    size_t filesz = (size_t)sb.st_size;

    if (filesz < CAS_PACK_BLOCK) {
        close(fd);
        return CAS_ERR;
    }

    void *ptr = mmap(NULL, filesz, PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);
    if (ptr == MAP_FAILED)
        return CAS_EIO;

    const struct cas_pack_trailer *tr =
        (const struct cas_pack_trailer *)
        ((char *)ptr + filesz - CAS_PACK_BLOCK);

    static const unsigned char magic_v1[] = CAS_PACK_TRAILER_MAGIC;
    static const unsigned char magic_v2[] = CAS_PACK_TRAILER_MAGIC_V2;
    int framed;

    if (memcmp(tr->magic, magic_v1, CAS_PACK_MAGIC_LEN) == 0)
        framed = 0;
    else if (memcmp(tr->magic, magic_v2, CAS_PACK_MAGIC_LEN) == 0)
        framed = 1;
    else {
        munmap(ptr, filesz);
        return CAS_ERR;
    }

    char type[CAS_TYPE_MAX + 1];
    size_t content_len;

    if (parse_header(tr->header, CAS_PACK_HEADER_LEN,
                     type, sizeof(type),
                     &content_len) != CAS_OK) {
        munmap(ptr, filesz);
        return CAS_ERR;
    }

    if (type_out) {
        if (strlen(type) >= type_bufsz) {
            munmap(ptr, filesz);
            return CAS_ERR;
        }
        memcpy(type_out, type, strlen(type) + 1);
    }

    const unsigned char *data;
    size_t len;
    unsigned char *owned;
    int rc = cas_codec_region_decode(ptr, filesz - CAS_PACK_BLOCK,
                                     framed, content_len,
                                     &data, &len, &owned);

    if (rc != CAS_OK) {
        munmap(ptr, filesz);
        return rc;
    }

    if (owned) {
        /* decoded into a heap buffer; the source is no longer needed */
        munmap(ptr, filesz);
        cf->_priv = owned;
        cf->_privlen = 0;
        cf->_release = release_free;
    } else {
        /* zero-copy view into the mmap */
        cf->_priv = ptr;
        cf->_privlen = filesz;
        cf->_release = release_munmap;
    }
    cf->data = data;
    cf->len = len;
    return CAS_OK;
}

int
cas_open_loose_raw(struct cas *store, struct cas_file *cf,
                   const char *hash, void *trailer_out)
{
    if (!valid_hash(hash))
        return CAS_ERR;

    char path[CAS_PATH_MAX];

    if (build_path(store, hash, path, sizeof(path)) != CAS_OK)
        return CAS_ERR;

    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return CAS_ENOTFOUND;

    struct stat sb;

    if (fstat(fd, &sb) != 0) {
        close(fd);
        return CAS_EIO;
    }

    size_t filesz = (size_t)sb.st_size;

    if (filesz < CAS_PACK_BLOCK) {
        close(fd);
        return CAS_ERR;
    }

    void *ptr = mmap(NULL, filesz, PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);
    if (ptr == MAP_FAILED)
        return CAS_EIO;

    const struct cas_pack_trailer *tr =
        (const struct cas_pack_trailer *)
        ((char *)ptr + filesz - CAS_PACK_BLOCK);

    static const unsigned char magic_v1[] = CAS_PACK_TRAILER_MAGIC;
    static const unsigned char magic_v2[] = CAS_PACK_TRAILER_MAGIC_V2;

    if (memcmp(tr->magic, magic_v1, CAS_PACK_MAGIC_LEN) != 0 &&
        memcmp(tr->magic, magic_v2, CAS_PACK_MAGIC_LEN) != 0) {
        munmap(ptr, filesz);
        return CAS_ERR;
    }

    memcpy(trailer_out, tr, CAS_PACK_BLOCK);

    cf->_priv = ptr;
    cf->_privlen = filesz;
    cf->_release = release_munmap;
    cf->data = (const unsigned char *)ptr;
    cf->len = filesz - CAS_PACK_BLOCK;
    return CAS_OK;
}

int
cas_open(struct cas *store, struct cas_file *cf, const char *hash)
{
    char type[CAS_TYPE_MAX + 1];
    int rc = cas_open_object(store, cf, hash, type, sizeof(type));

    if (rc != CAS_OK)
        return rc;
    if (strcmp(type, "blob") != 0) {
        cas_close(cf);
        return CAS_ETYPE;
    }
    return CAS_OK;
}

void
cas_close(struct cas_file *cf)
{
    if (cf->_release)
        cf->_release(cf);
    cf->data = NULL;
    cf->len = 0;
    cf->_priv = NULL;
    cf->_privlen = 0;
    cf->_release = NULL;
}

int
cas_put_object(struct cas *store, const char *type,
               const void *data, size_t len, char *hash_out)
{
    char hdr[CAS_HEADER_MAX];
    int hdrlen = format_header(hdr, sizeof(hdr), type, len);

    if (hdrlen < 0 || (size_t)hdrlen > CAS_PACK_HEADER_LEN)
        return CAS_ERR;

    struct blake2b state;
    uint8_t digest[CAS_HASH_LEN];

    blake2b_init(&state, CAS_HASH_LEN);
    blake2b_update(&state, hdr, (size_t)hdrlen);
    blake2b_update(&state, data, len);
    blake2b_final(&state, digest);

    char hash[CAS_HASH_HEX + 1];

    to_hex(digest, CAS_HASH_LEN, hash);

    char path[CAS_PATH_MAX];
    char dir[CAS_PATH_MAX];
    char tmp[CAS_PATH_MAX];

    if (build_path(store, hash, path, sizeof(path)) != CAS_OK)
        return CAS_ERR;
    if (build_dir(store, hash, dir, sizeof(dir)) != CAS_OK)
        return CAS_ERR;

    if (access(path, F_OK) == 0 ||
        (store->pack && cas_pack_exists(store->pack, hash))) {
        memcpy(hash_out, hash, CAS_HASH_HEX + 1);
        return CAS_OK;
    }

    mkdir(store->basedir, 0755);
    mkdir(dir, 0755);

    if (snprintf(tmp, sizeof(tmp), "%s/.cas.XXXXXX", dir) >=
        (int)sizeof(tmp))
        return CAS_ERR;

    int fd = mkstemp(tmp);

    if (fd < 0)
        return CAS_EIO;

    int rc;

    if (len > 0) {
        rc = write_full(fd, data, len);
        if (rc != CAS_OK) {
            close(fd);
            unlink(tmp);
            return rc;
        }
    }

    struct cas_pack_trailer tr;
    static const unsigned char magic[] = CAS_PACK_TRAILER_MAGIC;

    memcpy(tr.magic, magic, CAS_PACK_MAGIC_LEN);
    memset(tr.header, 0, CAS_PACK_HEADER_LEN);
    memcpy(tr.header, hdr, (size_t)hdrlen);

    rc = write_full(fd, &tr, sizeof(tr));
    if (rc != CAS_OK) {
        close(fd);
        unlink(tmp);
        return rc;
    }

    close(fd);

    if (rename(tmp, path) != 0) {
        unlink(tmp);
        if (access(path, F_OK) != 0)
            return CAS_EIO;
    }

    memcpy(hash_out, hash, CAS_HASH_HEX + 1);
    return CAS_OK;
}

int
cas_put_object_at(struct cas *store, const char *type,
                  const void *data, size_t len,
                  const char *hash)
{
    if (!valid_hash(hash))
        return CAS_ERR;

    char hdr[CAS_HEADER_MAX];
    int hdrlen = format_header(hdr, sizeof(hdr), type, len);

    if (hdrlen < 0 || (size_t)hdrlen > CAS_PACK_HEADER_LEN)
        return CAS_ERR;

    char path[CAS_PATH_MAX];
    char dir[CAS_PATH_MAX];
    char tmp[CAS_PATH_MAX];

    if (build_path(store, hash, path, sizeof(path)) != CAS_OK)
        return CAS_ERR;
    if (build_dir(store, hash, dir, sizeof(dir)) != CAS_OK)
        return CAS_ERR;

    if (access(path, F_OK) == 0 ||
        (store->pack && cas_pack_exists(store->pack, hash)))
        return CAS_OK;

    mkdir(store->basedir, 0755);
    mkdir(dir, 0755);

    if (snprintf(tmp, sizeof(tmp), "%s/.cas.XXXXXX", dir) >=
        (int)sizeof(tmp))
        return CAS_ERR;

    int fd = mkstemp(tmp);

    if (fd < 0)
        return CAS_EIO;

    int rc;

    if (len > 0) {
        rc = write_full(fd, data, len);
        if (rc != CAS_OK) {
            close(fd);
            unlink(tmp);
            return rc;
        }
    }

    struct cas_pack_trailer tr;
    static const unsigned char magic[] = CAS_PACK_TRAILER_MAGIC;

    memcpy(tr.magic, magic, CAS_PACK_MAGIC_LEN);
    memset(tr.header, 0, CAS_PACK_HEADER_LEN);
    memcpy(tr.header, hdr, (size_t)hdrlen);

    rc = write_full(fd, &tr, sizeof(tr));
    if (rc != CAS_OK) {
        close(fd);
        unlink(tmp);
        return rc;
    }

    close(fd);

    if (rename(tmp, path) != 0) {
        unlink(tmp);
        if (access(path, F_OK) != 0)
            return CAS_EIO;
    }

    return CAS_OK;
}

int
cas_put_precompressed(struct cas *store, const char *type, int codec,
                      const void *payload, size_t payload_len,
                      size_t plaintext_len, const char *hash)
{
    if (!valid_hash(hash) || codec < 0 || codec > 255)
        return CAS_ERR;

    /* an uncompressed payload must match the plaintext length */
    if (codec == CAS_CODEC_NONE && payload_len != plaintext_len)
        return CAS_ERR;

    char hdr[CAS_HEADER_MAX];
    int hdrlen = format_header(hdr, sizeof(hdr), type, plaintext_len);

    if (hdrlen < 0 || (size_t)hdrlen > CAS_PACK_HEADER_LEN)
        return CAS_ERR;

    char path[CAS_PATH_MAX];
    char dir[CAS_PATH_MAX];
    char tmp[CAS_PATH_MAX];

    if (build_path(store, hash, path, sizeof(path)) != CAS_OK)
        return CAS_ERR;
    if (build_dir(store, hash, dir, sizeof(dir)) != CAS_OK)
        return CAS_ERR;

    if (access(path, F_OK) == 0 ||
        (store->pack && cas_pack_exists(store->pack, hash)))
        return CAS_OK;

    mkdir(store->basedir, 0755);
    mkdir(dir, 0755);

    if (snprintf(tmp, sizeof(tmp), "%s/.cas.XXXXXX", dir) >=
        (int)sizeof(tmp))
        return CAS_ERR;

    int fd = mkstemp(tmp);

    if (fd < 0)
        return CAS_EIO;

    /* data region: [ codec tag(1) | payload ] */
    unsigned char cb = (unsigned char)codec;
    int rc = write_full(fd, &cb, 1);

    if (rc == CAS_OK && payload_len > 0)
        rc = write_full(fd, payload, payload_len);
    if (rc != CAS_OK) {
        close(fd);
        unlink(tmp);
        return rc;
    }

    struct cas_pack_trailer tr;
    static const unsigned char magic[] = CAS_PACK_TRAILER_MAGIC_V2;

    memcpy(tr.magic, magic, CAS_PACK_MAGIC_LEN);
    memset(tr.header, 0, CAS_PACK_HEADER_LEN);
    memcpy(tr.header, hdr, (size_t)hdrlen);

    rc = write_full(fd, &tr, sizeof(tr));
    if (rc != CAS_OK) {
        close(fd);
        unlink(tmp);
        return rc;
    }

    close(fd);

    if (rename(tmp, path) != 0) {
        unlink(tmp);
        if (access(path, F_OK) != 0)
            return CAS_EIO;
    }

    return CAS_OK;
}

int
cas_put_object_z(struct cas *store, const char *type, int codec,
                 const void *data, size_t len, char *hash_out)
{
    char hash[CAS_HASH_HEX + 1];

    if (cas_hash_object(type, data, len, hash) != CAS_OK)
        return CAS_ERR;

    /* skip a pointless compression attempt on a known object */
    if (cas_exists(store, hash)) {
        memcpy(hash_out, hash, CAS_HASH_HEX + 1);
        return CAS_OK;
    }

    /* try to compress; keep it only if the payload plus its one
     * codec tag byte beats the raw size by more than ~12% */
    if (codec != CAS_CODEC_NONE && len > 0 &&
        cas_codec_can_encode(codec)) {
        unsigned char *buf = malloc(len);

        if (buf) {
            size_t plen = len;
            int rc = cas_codec_encode(codec, data, len, buf, &plen);

            if (rc == CAS_OK && plen + 1 < len - len / 8) {
                rc = cas_put_precompressed(store, type, codec, buf,
                                           plen, len, hash);
                free(buf);
                if (rc == CAS_OK)
                    memcpy(hash_out, hash, CAS_HASH_HEX + 1);
                return rc;
            }
            free(buf);
        }
    }

    /* not worth compressing: store raw at the same address */
    int rc = cas_put_object_at(store, type, data, len, hash);

    if (rc == CAS_OK)
        memcpy(hash_out, hash, CAS_HASH_HEX + 1);
    return rc;
}

int
cas_put(struct cas *store, const void *data, size_t len,
        char *hash_out)
{
    return cas_put_object(store, "blob", data, len, hash_out);
}

int
cas_exists(struct cas *store, const char *hash)
{
    if (!valid_hash(hash))
        return 0;
    if (store->pack && cas_pack_exists(store->pack, hash))
        return 1;

    char path[CAS_PATH_MAX];

    if (build_path(store, hash, path, sizeof(path)) != CAS_OK)
        return 0;
    return access(path, F_OK) == 0;
}

/****************************************************************
 * Iteration
 ****************************************************************/

int
cas_foreach(struct cas *store, cas_foreach_fn fn, void *ctx)
{
    DIR *top = opendir(store->basedir);

    if (!top)
        return CAS_OK;

    struct dirent *bucket;

    while ((bucket = readdir(top)) != NULL) {
        if (strlen(bucket->d_name) != 2)
            continue;
        if (!((bucket->d_name[0] >= '0' && bucket->d_name[0] <= '9') ||
              (bucket->d_name[0] >= 'a' && bucket->d_name[0] <= 'f')))
            continue;

        char subpath[CAS_PATH_MAX];

        if (snprintf(subpath, sizeof(subpath), "%s/%s",
                     store->basedir, bucket->d_name) >=
            (int)sizeof(subpath))
            continue;

        DIR *sub = opendir(subpath);

        if (!sub)
            continue;

        struct dirent *ent;

        while ((ent = readdir(sub)) != NULL) {
            if (!valid_hash(ent->d_name))
                continue;
            if (strncmp(ent->d_name, bucket->d_name, 2) != 0)
                continue;
            if (fn(ent->d_name, ctx) != 0) {
                closedir(sub);
                closedir(top);
                return CAS_OK;
            }
        }
        closedir(sub);
    }
    closedir(top);
    return CAS_OK;
}

/****************************************************************
 * Fsck
 ****************************************************************/

int
cas_type_is_reencoded(const char *type)
{
    return strcmp(type, "htree") == 0;
}

int
cas_fsck_object(struct cas *store, const char *hash)
{
    if (!valid_hash(hash))
        return CAS_FSCK_BADNAME;

    struct cas_file cf;
    struct cas_pack_trailer tr;
    int rc = cas_open_loose_raw(store, &cf, hash, &tr);

    if (rc == CAS_ENOTFOUND || rc == CAS_EIO)
        return CAS_FSCK_IOERR;
    if (rc != CAS_OK)
        return CAS_FSCK_CORRUPT;

    static const unsigned char magic_v2[] = CAS_PACK_TRAILER_MAGIC_V2;
    int framed = memcmp(tr.magic, magic_v2, CAS_PACK_MAGIC_LEN) == 0;

    char type[CAS_TYPE_MAX + 1];
    size_t content_len;

    if (parse_header(tr.header, CAS_PACK_HEADER_LEN,
                     type, sizeof(type), &content_len) != CAS_OK) {
        cas_close(&cf);
        return CAS_FSCK_CORRUPT;
    }

    /* An htree is addressed by its canonical text form, not its own
     * bytes, so it cannot be verified here; cas_tree_fsck does it. */
    if (cas_type_is_reencoded(type)) {
        cas_close(&cf);
        return CAS_FSCK_REENCODED;
    }

    /* decode to plaintext, then rehash "type len\0" || plaintext */
    const unsigned char *data;
    size_t len;
    unsigned char *owned;

    rc = cas_codec_region_decode(cf.data, cf.len, framed, content_len,
                                 &data, &len, &owned);
    if (rc == CAS_ETYPE) {
        /* codec not compiled in: the object cannot be verified */
        cas_close(&cf);
        return CAS_FSCK_NOCODEC;
    }
    if (rc == CAS_ENOMEM) {
        cas_close(&cf);
        return CAS_FSCK_IOERR;
    }
    if (rc != CAS_OK) {
        cas_close(&cf);
        return CAS_FSCK_CORRUPT;
    }

    unsigned char digest[CAS_HASH_LEN];
    char actual[CAS_HASH_HEX + 1];

    cas_digest_object(type, data, len, digest);
    to_hex(digest, CAS_HASH_LEN, actual);

    free(owned);
    cas_close(&cf);

    return strcmp(actual, hash) == 0 ? CAS_FSCK_OK : CAS_FSCK_CORRUPT;
}

struct cas_fsck_ctx {
    struct cas *store;
    cas_fsck_fn fn;
    void *ctx;
    int errors;
};

static int
fsck_visitor(const char *hash, void *ctx)
{
    struct cas_fsck_ctx *fc = ctx;
    int status = cas_fsck_object(fc->store, hash);

    /* NOCODEC and REENCODED are skips, not corruption: reported,
     * not counted */
    if (status != CAS_FSCK_OK && status != CAS_FSCK_NOCODEC &&
        status != CAS_FSCK_REENCODED)
        fc->errors++;
    if (fc->fn)
        return fc->fn(hash, status, fc->ctx);
    return 0;
}

int
cas_fsck(struct cas *store, cas_fsck_fn fn, void *ctx)
{
    struct cas_fsck_ctx fc = {
        .store = store,
        .fn = fn,
        .ctx = ctx,
        .errors = 0,
    };

    cas_foreach(store, fsck_visitor, &fc);
    return fc.errors ? CAS_ERR : CAS_OK;
}

/****************************************************************
 * Remove
 ****************************************************************/

int
cas_remove(struct cas *store, const char *hash)
{
    char path[CAS_PATH_MAX];

    if (!valid_hash(hash))
        return CAS_ERR;
    if (build_path(store, hash, path, sizeof(path)) != CAS_OK)
        return CAS_ERR;
    if (unlink(path) != 0)
        return errno == ENOENT ? CAS_ENOTFOUND : CAS_EIO;
    return CAS_OK;
}

int
cas_object_mtime(struct cas *store, const char *hash, time_t *mtime_out)
{
    char path[CAS_PATH_MAX];

    if (!valid_hash(hash))
        return CAS_ERR;
    if (build_path(store, hash, path, sizeof(path)) != CAS_OK)
        return CAS_ERR;

    struct stat sb;

    if (stat(path, &sb) != 0)
        return errno == ENOENT ? CAS_ENOTFOUND : CAS_EIO;
    *mtime_out = sb.st_mtime;
    return CAS_OK;
}

/****************************************************************
 * Public binary hash helpers
 ****************************************************************/

void
cas_digest(const void *data, size_t len, unsigned char *out)
{
    struct blake2b state;

    blake2b_init(&state, CAS_HASH_LEN);
    blake2b_update(&state, data, len);
    blake2b_final(&state, out);
}

void
cas_digest_object(const char *type, const void *data, size_t len,
                  unsigned char *out)
{
    char hdr[CAS_HEADER_MAX];
    int hdrlen = snprintf(hdr, sizeof(hdr), "%s %zu", type, len);

    if (hdrlen < 0 || (size_t)hdrlen >= sizeof(hdr))
        return;
    hdrlen++;

    struct blake2b state;

    blake2b_init(&state, CAS_HASH_LEN);
    blake2b_update(&state, hdr, (size_t)hdrlen);
    blake2b_update(&state, data, len);
    blake2b_final(&state, out);
}

void
cas_hex_encode(const unsigned char *bin, size_t len, char *out)
{
    to_hex(bin, len, out);
}

int
cas_hex_decode(const char *hex, size_t hexlen,
               unsigned char *out, size_t outsz)
{
    if (hexlen % 2 != 0 || hexlen / 2 > outsz)
        return CAS_ERR;
    for (size_t i = 0; i < hexlen; i += 2) {
        int hi, lo;
        char c;

        c = hex[i];
        if (c >= '0' && c <= '9')      hi = c - '0';
        else if (c >= 'a' && c <= 'f')  hi = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')  hi = c - 'A' + 10;
        else return CAS_ERR;

        c = hex[i + 1];
        if (c >= '0' && c <= '9')      lo = c - '0';
        else if (c >= 'a' && c <= 'f')  lo = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')  lo = c - 'A' + 10;
        else return CAS_ERR;

        out[i / 2] = (unsigned char)((hi << 4) | lo);
    }
    return CAS_OK;
}

int
cas_valid_hash(const char *hash)
{
    return valid_hash(hash);
}
