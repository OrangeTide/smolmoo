/* cas-sign.c : signed version records for CAS refs */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#define _POSIX_C_SOURCE 200809L

#include "cas-sign.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/****************************************************************
 * Backend selection
 ****************************************************************
 *
 * Compile time, no runtime registry, mirroring cas-codec.c.  Exactly
 * one backend or none.
 */

#if defined(CAS_SIGN_USER)
/* Supplied by the application; the declarations come with it. */
#elif defined(CAS_WITH_MONOCYPHER)
int cas_sign_eddsa_sign(unsigned char sig[CAS_SIGN_SIG_LEN],
                        const unsigned char sk[CAS_SIGN_SECKEY_LEN],
                        const void *msg, size_t len);
int cas_sign_eddsa_check(const unsigned char sig[CAS_SIGN_SIG_LEN],
                         const unsigned char pk[CAS_SIGN_PUBKEY_LEN],
                         const void *msg, size_t len);
int cas_sign_eddsa_key_pair(unsigned char sk[CAS_SIGN_SECKEY_LEN],
                            unsigned char pk[CAS_SIGN_PUBKEY_LEN],
                            const unsigned char seed[CAS_SIGN_SEED_LEN]);
#define SIGN_BACKEND_SIGN     cas_sign_eddsa_sign
#define SIGN_BACKEND_CHECK    cas_sign_eddsa_check
#define SIGN_BACKEND_KEYPAIR  cas_sign_eddsa_key_pair
#endif

int
cas_sign_available(void)
{
#ifdef SIGN_BACKEND_SIGN
    return 1;
#else
    return 0;
#endif
}

const char *
cas_sign_strerror(int err)
{
    switch (err) {
    case CAS_OK:                 return "success";
    case CAS_SIGN_ENOBACKEND:    return "no signing backend compiled in";
    case CAS_SIGN_EBADFORM:      return "malformed version record";
    case CAS_SIGN_EBADBIND:      return "topic id does not match key";
    case CAS_SIGN_EBADSIG:       return "signature does not verify";
    case CAS_SIGN_ETOPIC:        return "record belongs to another topic";
    case CAS_SIGN_ESTALE:        return "record is not newer than the head";
    case CAS_SIGN_EFORK:         return "two records at one seq: key compromise";
    case CAS_SIGN_ECHAIN:        return "prev does not link to the predecessor";
    case CAS_SIGN_EGAP:          return "sequence gap; intermediates unseen";
    case CAS_SIGN_EINCOMPLETE:   return "chain incomplete in this store";
    case CAS_SIGN_EKEYPERM:      return "key file is readable by others";
    case CAS_SIGN_EKEYEXISTS:    return "key file already exists";
    case CAS_SIGN_EKEYFORM:      return "not a key file";
    default:                     return cas_strerror(err);
    }
}

int
cas_sign_key_pair(unsigned char sk[CAS_SIGN_SECKEY_LEN],
                  unsigned char pk[CAS_SIGN_PUBKEY_LEN],
                  const unsigned char seed[CAS_SIGN_SEED_LEN])
{
#ifdef SIGN_BACKEND_KEYPAIR
    return SIGN_BACKEND_KEYPAIR(sk, pk, seed);
#else
    (void)sk; (void)pk; (void)seed;
    return CAS_SIGN_ENOBACKEND;
#endif
}

void
cas_sign_topic_id(unsigned char out[CAS_HASH_LEN],
                  const unsigned char pk[CAS_SIGN_PUBKEY_LEN])
{
    /* The bare key bytes, with none of the "type len\0" framing an
     * object address carries.  A topic id names a key, not an object,
     * and keeping the two derivations distinct means a record can
     * never be mistaken for the identity it belongs to. */
    cas_digest(pk, CAS_SIGN_PUBKEY_LEN, out);
}

/****************************************************************
 * Record layout
 ****************************************************************/

#define VREC_MAGIC      "VRv1"
#define VREC_MAGIC_LEN  4

#define VREC_OFF_MAGIC     0
#define VREC_OFF_RESERVED  4
#define VREC_OFF_TOPIC     8
#define VREC_OFF_PUBKEY   40
#define VREC_OFF_SEQ      72
#define VREC_OFF_TIME     80
#define VREC_OFF_ROOT     88
#define VREC_OFF_PREV    120
#define VREC_OFF_SIG     152

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

static const unsigned char vrec_zero[CAS_HASH_LEN];

int
cas_vrec_encode(struct cas_vrec *v,
                const unsigned char sk[CAS_SIGN_SECKEY_LEN],
                unsigned char out[CAS_VREC_LEN])
{
    if (!v || !sk || !out)
        return CAS_ERR;
    if (!cas_valid_hash(v->root))
        return CAS_ERR;
    if (v->prev[0] != '\0' && !cas_valid_hash(v->prev))
        return CAS_ERR;

    memset(out, 0, CAS_VREC_LEN);
    memcpy(out + VREC_OFF_MAGIC, VREC_MAGIC, VREC_MAGIC_LEN);

    cas_sign_topic_id(v->topic_id, v->pubkey);
    memcpy(out + VREC_OFF_TOPIC, v->topic_id, CAS_HASH_LEN);
    memcpy(out + VREC_OFF_PUBKEY, v->pubkey, CAS_SIGN_PUBKEY_LEN);

    store_le64(out + VREC_OFF_SEQ, v->seq);
    store_le64(out + VREC_OFF_TIME, (uint64_t)v->timestamp);

    if (cas_hex_decode(v->root, CAS_HASH_HEX,
                       out + VREC_OFF_ROOT, CAS_HASH_LEN) != 0)
        return CAS_ERR;

    /* An absent predecessor is 32 zero bytes, which is already there
     * from the memset.  A first record and one whose predecessor
     * happened to hash to zero are indistinguishable, which is fine:
     * BLAKE2b does not produce that. */
    if (v->prev[0] != '\0' &&
        cas_hex_decode(v->prev, CAS_HASH_HEX,
                       out + VREC_OFF_PREV, CAS_HASH_LEN) != 0)
        return CAS_ERR;

#ifdef SIGN_BACKEND_SIGN
    return SIGN_BACKEND_SIGN(out + VREC_OFF_SIG, sk, out,
                             CAS_VREC_SIGNED_LEN);
#else
    return CAS_SIGN_ENOBACKEND;
#endif
}

int
cas_vrec_decode(const unsigned char *buf, size_t len,
                struct cas_vrec *out)
{
    if (!buf || !out || len != CAS_VREC_LEN)
        return CAS_SIGN_EBADFORM;
    if (memcmp(buf + VREC_OFF_MAGIC, VREC_MAGIC, VREC_MAGIC_LEN) != 0)
        return CAS_SIGN_EBADFORM;

    /* The reserved word exists to keep the record's one variable point
     * of the future from being a new length.  It has to stay zero, or
     * a signer could smuggle bytes past a reader that ignored it. */
    for (int i = 0; i < 4; i++) {
        if (buf[VREC_OFF_RESERVED + i] != 0)
            return CAS_SIGN_EBADFORM;
    }

    memcpy(out->topic_id, buf + VREC_OFF_TOPIC, CAS_HASH_LEN);
    memcpy(out->pubkey, buf + VREC_OFF_PUBKEY, CAS_SIGN_PUBKEY_LEN);

    /* The binding is what makes the name self-certifying, so it is
     * checked before the signature: a record signed by the wrong key
     * for this topic is not a signature failure, it is the wrong
     * identity, and the two deserve different answers. */
    unsigned char expect[CAS_HASH_LEN];

    cas_sign_topic_id(expect, out->pubkey);
    if (memcmp(expect, out->topic_id, CAS_HASH_LEN) != 0)
        return CAS_SIGN_EBADBIND;

    out->seq = load_le64(buf + VREC_OFF_SEQ);
    out->timestamp = (int64_t)load_le64(buf + VREC_OFF_TIME);

    cas_hex_encode(buf + VREC_OFF_ROOT, CAS_HASH_LEN, out->root);

    if (memcmp(buf + VREC_OFF_PREV, vrec_zero, CAS_HASH_LEN) == 0)
        out->prev[0] = '\0';
    else
        cas_hex_encode(buf + VREC_OFF_PREV, CAS_HASH_LEN, out->prev);

#ifdef SIGN_BACKEND_CHECK
    if (SIGN_BACKEND_CHECK(buf + VREC_OFF_SIG, out->pubkey, buf,
                           CAS_VREC_SIGNED_LEN) != CAS_OK)
        return CAS_SIGN_EBADSIG;
    return CAS_OK;
#else
    /* Parsed, but nothing here can vouch for it.  Reporting success
     * would make a build without a backend look like a verifying one. */
    return CAS_SIGN_ENOBACKEND;
#endif
}

int
cas_vrec_address(const unsigned char rec[CAS_VREC_LEN], char *hash_out)
{
    if (!rec || !hash_out)
        return CAS_ERR;
    return cas_hash_object(CAS_VREC_TYPE, rec, CAS_VREC_LEN, hash_out);
}

/****************************************************************
 * Chains
 ****************************************************************/

int
cas_vrec_succeeds(const struct cas_vrec *cur, const char *cur_addr,
                  const struct cas_vrec *cand)
{
    if (!cand)
        return CAS_ERR;

    /* No head yet: any valid record for the topic starts one. */
    if (!cur)
        return CAS_OK;
    if (!cur_addr)
        return CAS_ERR;

    if (memcmp(cur->topic_id, cand->topic_id, CAS_HASH_LEN) != 0)
        return CAS_SIGN_ETOPIC;

    if (cand->seq < cur->seq)
        return CAS_SIGN_ESTALE;

    if (cand->seq == cur->seq) {
        /* Same generation.  Either it is the record we already hold,
         * or the key signed two different things at one seq, which an
         * honest single writer cannot do. */
        if (strcmp(cand->root, cur->root) == 0 &&
            strcmp(cand->prev, cur->prev) == 0 &&
            cand->timestamp == cur->timestamp)
            return CAS_SIGN_ESTALE;
        return CAS_SIGN_EFORK;
    }

    if (cand->seq == cur->seq + 1) {
        if (strcmp(cand->prev, cur_addr) != 0)
            return CAS_SIGN_ECHAIN;
        return CAS_OK;
    }

    return CAS_SIGN_EGAP;
}

/** Load and verify one record from the store. */
static int
vchain_load(struct cas *store, const char *addr, struct cas_vrec *out)
{
    struct cas_file cf;
    char type[CAS_TYPE_MAX + 1];
    int rc = cas_open_object(store, &cf, addr, type, sizeof(type));

    if (rc != CAS_OK)
        return rc;

    if (strcmp(type, CAS_VREC_TYPE) != 0) {
        cas_close(&cf);
        return CAS_ETYPE;
    }

    rc = cas_vrec_decode(cf.data, cf.len, out);
    cas_close(&cf);
    return rc;
}

int
cas_vchain_walk(struct cas *store, const char *head_addr,
                uint64_t stop_seq, cas_vchain_fn fn, void *ctx)
{
    if (!store || !head_addr)
        return CAS_ERR;

    char addr[CAS_HASH_HEX + 1];
    struct cas_vrec v;
    int rc = vchain_load(store, head_addr, &v);

    if (rc != CAS_OK)
        return rc;

    snprintf(addr, sizeof(addr), "%s", head_addr);

    for (;;) {
        if (fn && fn(&v, addr, ctx))
            return CAS_OK;

        if (v.seq <= stop_seq || v.prev[0] == '\0')
            return CAS_OK;

        struct cas_vrec prev;

        rc = vchain_load(store, v.prev, &prev);
        if (rc == CAS_ENOTFOUND)
            return CAS_SIGN_EINCOMPLETE;
        if (rc != CAS_OK)
            return rc;

        /* The link is only as good as what it connects.  Checking the
         * predecessor's own signature is not enough: it has to be the
         * predecessor of *this* record, in this topic, at the seq the
         * chain requires. */
        if (memcmp(prev.topic_id, v.topic_id, CAS_HASH_LEN) != 0)
            return CAS_SIGN_ETOPIC;
        if (prev.seq + 1 != v.seq)
            return CAS_SIGN_ECHAIN;

        snprintf(addr, sizeof(addr), "%s", v.prev);
        v = prev;
    }
}

/****************************************************************
 * Key files
 ****************************************************************/

/* memset on a buffer whose lifetime is ending is a dead store, and a
 * compiler may drop it.  It happens to survive here at -O2, which is
 * not a guarantee worth resting a secret on.  Volatile stores are
 * observable, so these cannot be elided. */
static void
wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;

    while (n--)
        *v++ = 0;
}

static int
read_entropy(unsigned char *out, size_t len)
{
    FILE *fp = fopen("/dev/urandom", "rb");

    if (!fp)
        return CAS_EIO;

    size_t n = fread(out, 1, len, fp);

    fclose(fp);
    return n == len ? CAS_OK : CAS_EIO;
}

int
cas_sign_key_generate(const char *path,
                      unsigned char pk[CAS_SIGN_PUBKEY_LEN])
{
    if (!path || !pk)
        return CAS_ERR;
    if (!cas_sign_available())
        return CAS_SIGN_ENOBACKEND;

    unsigned char seed[CAS_SIGN_SEED_LEN];
    unsigned char sk[CAS_SIGN_SECKEY_LEN];
    int rc = read_entropy(seed, sizeof(seed));

    if (rc != CAS_OK)
        return rc;

    rc = cas_sign_key_pair(sk, pk, seed);
    if (rc != CAS_OK) {
        wipe(seed, sizeof(seed));
        return rc;
    }

    char hex[CAS_HASH_HEX + 1];

    cas_hex_encode(seed, sizeof(seed), hex);
    wipe(seed, sizeof(seed));
    wipe(sk, sizeof(sk));

    /* O_EXCL so an existing key is never replaced by accident, and 0600
     * from the start rather than chmod afterwards, which would leave a
     * window where the seed sat on disk readable. */
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);

    if (fd < 0) {
        int err = errno == EEXIST ? CAS_SIGN_EKEYEXISTS : CAS_EIO;

        wipe(hex, sizeof(hex));
        return err;
    }

    char buf[128];
    int n = snprintf(buf, sizeof(buf), "%s\n%s\n",
                     CAS_SIGN_KEY_MAGIC, hex);

    wipe(hex, sizeof(hex));
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        close(fd);
        unlink(path);
        wipe(buf, sizeof(buf));
        return CAS_ERR;
    }

    ssize_t w = write(fd, buf, (size_t)n);

    wipe(buf, sizeof(buf));
    if (w != n || fsync(fd) != 0) {
        close(fd);
        unlink(path);
        return CAS_EIO;
    }
    if (close(fd) != 0) {
        unlink(path);
        return CAS_EIO;
    }
    return CAS_OK;
}

int
cas_sign_key_load(const char *path,
                  unsigned char sk[CAS_SIGN_SECKEY_LEN],
                  unsigned char pk[CAS_SIGN_PUBKEY_LEN])
{
    if (!path || !sk || !pk)
        return CAS_ERR;
    if (!cas_sign_available())
        return CAS_SIGN_ENOBACKEND;

    struct stat st;

    if (stat(path, &st) != 0)
        return CAS_ENOTFOUND;

    /* A secret any other account can read is not a secret.  Saying so
     * here is cheaper than the alternative, and it is the check every
     * tool that handles private keys has learned to make. */
    if (st.st_mode & 0077)
        return CAS_SIGN_EKEYPERM;

    FILE *fp = fopen(path, "r");

    if (!fp)
        return CAS_ENOTFOUND;

    char magic[64] = {0};
    char hex[128] = {0};
    int rc = CAS_SIGN_EKEYFORM;

    if (!fgets(magic, sizeof(magic), fp))
        goto out;
    magic[strcspn(magic, "\r\n")] = '\0';
    if (strcmp(magic, CAS_SIGN_KEY_MAGIC) != 0)
        goto out;

    if (!fgets(hex, sizeof(hex), fp))
        goto out;
    hex[strcspn(hex, "\r\n")] = '\0';
    if (strlen(hex) != CAS_HASH_HEX)
        goto out;

    unsigned char seed[CAS_SIGN_SEED_LEN];

    if (cas_hex_decode(hex, CAS_HASH_HEX, seed, sizeof(seed)) != 0) {
        wipe(seed, sizeof(seed));
        goto out;
    }

    rc = cas_sign_key_pair(sk, pk, seed);
    wipe(seed, sizeof(seed));

out:
    wipe(hex, sizeof(hex));
    fclose(fp);
    return rc;
}
