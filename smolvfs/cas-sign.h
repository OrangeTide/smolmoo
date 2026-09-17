/* cas-sign.h : signed version records for CAS refs */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#ifndef CAS_SIGN_H
#define CAS_SIGN_H

#include "cas.h"

#include <stddef.h>
#include <stdint.h>

/****************************************************************
 * What this is for
 ****************************************************************
 *
 * A ref is a mutable pointer to a root address, and whoever controls
 * it controls which tree a client materializes.  Fetching one over
 * TLS moves that trust to the serving origin, which is the wrong
 * party: a mirror, a cache, or a community server should be able to
 * carry a ref without being able to change what it means.
 *
 * A version record moves the trust anchor to the publisher.  It is a
 * fixed-size, self-addressed CAS object that names a root, carries a
 * sequence number, links to its predecessor, and is signed by the key
 * the topic is named after.  Verifying one needs no registry and no
 * connection to anybody: the name binds to the key.
 *
 * The record says nothing about transport.  It verifies the same
 * whether it arrived over HTTP, from a peer, or off a USB stick.
 *
 * Chain and monotonicity checks are a layer above this one, which
 * concerns itself with a single record.
 */

/****************************************************************
 * Signature scheme
 ****************************************************************
 *
 * EdDSA over curve25519 with **BLAKE2b**, as implemented by
 * monocypher's crypto_eddsa_* family.
 *
 * This is deliberately *not* RFC 8032 Ed25519, which is the same
 * construction with SHA-512.  A signature here will not verify with a
 * library advertising "Ed25519", and one produced by such a library
 * will not verify here.  The choice keeps BLAKE2b as the only hash
 * primitive in the system, which smolvfs already depends on for every
 * address.  An implementation in another language wanting to
 * interoperate implements Ed25519 with BLAKE2b-512 substituted for
 * SHA-512 throughout; monocypher is the normative reference.
 *
 * The backend is selected at compile time, mirroring the codec table
 * in cas-codec.h.  Build with -DCAS_WITH_MONOCYPHER for the bundled
 * one.  An application bringing its own defines CAS_SIGN_USER when
 * building cas-sign.c:
 *
 *   #define CAS_SIGN_USER(X) X(my_sign, my_check)
 *
 * With no backend the module still parses records but cannot verify
 * them, and says so with CAS_SIGN_ENOBACKEND rather than reporting
 * success.  An absent verifier must never look like a passing one.
 */

#define CAS_SIGN_SEED_LEN    32
#define CAS_SIGN_PUBKEY_LEN  32
#define CAS_SIGN_SECKEY_LEN  64
#define CAS_SIGN_SIG_LEN     64

/** Errors specific to this module.  Numbered clear of the CAS_* codes
 *  in cas.h so a caller can pass either through one int. */
enum {
    CAS_SIGN_ENOBACKEND = -20,  /* no signer compiled in */
    CAS_SIGN_EBADFORM   = -21,  /* not a well-formed record */
    CAS_SIGN_EBADBIND   = -22,  /* topic id is not the hash of the key */
    CAS_SIGN_EBADSIG    = -23,  /* signature does not verify */

    /* chain */
    CAS_SIGN_ETOPIC     = -24,  /* belongs to a different topic */
    CAS_SIGN_ESTALE     = -25,  /* not newer than the current head */
    CAS_SIGN_EFORK      = -26,  /* two records at one seq; see below */
    CAS_SIGN_ECHAIN     = -27,  /* prev does not link to the predecessor */
    CAS_SIGN_EGAP       = -28,  /* seq jumped; intermediates unseen */
    CAS_SIGN_EINCOMPLETE = -29, /* chain ran out before its first record */

    CAS_SIGN_EKEYPERM   = -30,  /* key file is readable by others */
    CAS_SIGN_EKEYEXISTS = -31,  /* refusing to replace an existing key */
    CAS_SIGN_EKEYFORM   = -32,  /* file is not a key file */
};

/** Human-readable text for a CAS_SIGN_* code. */
const char *
cas_sign_strerror(int err);

/** Nonzero if a signing backend is compiled in. */
int
cas_sign_available(void);

/** Derive a key pair from a 32-byte seed.
 *  The seed is the secret; keep it out of any depot that is served.
 *  Returns CAS_OK, or CAS_SIGN_ENOBACKEND.
 */
int
cas_sign_key_pair(unsigned char sk[CAS_SIGN_SECKEY_LEN],
                  unsigned char pk[CAS_SIGN_PUBKEY_LEN],
                  const unsigned char seed[CAS_SIGN_SEED_LEN]);

/****************************************************************
 * Key files
 ****************************************************************
 *
 * A secret key is local-domain data (ATOLL A3): it is never served, and
 * it does not belong in a depot, whose whole purpose is to hand objects
 * to other people.  So it lives in an ordinary file the caller names,
 * outside any depot, and the two are never mixed.
 *
 * The file holds the 32-byte seed, from which the pair is derived on
 * load.  Storing the seed rather than the expanded key keeps the file
 * small and makes it obvious there is exactly one secret in it.
 *
 * Format, two text lines so the file is greppable and identifiable:
 *
 *     smolvfs-secret-key-v1
 *     <64 hex characters>
 *
 * Permissions are enforced, not merely suggested.  Generation creates
 * the file 0600 and refuses to overwrite one that exists, and loading
 * refuses a key any group or other user can read.  A secret readable by
 * the wrong account is not a secret, and reporting that as an error the
 * moment it is noticed is cheaper than the alternative.
 *
 * Passphrase protection is deliberately absent for now: it needs a key
 * derivation function and a decision about where the passphrase comes
 * from, and a wrong answer there is worse than an honest plaintext file
 * with strict permissions.
 */

#define CAS_SIGN_KEY_MAGIC "smolvfs-secret-key-v1"

/** Generate a key pair and write the secret to `path`.
 *
 *  The seed comes from the system entropy source.  `path` must not
 *  already exist, so an existing key is never silently replaced.  The
 *  public key is returned for the caller to publish; it is not stored,
 *  since it derives from the seed.
 *
 *  Returns CAS_OK, CAS_SIGN_EKEYEXISTS if `path` is already there,
 *  CAS_SIGN_ENOBACKEND, or CAS_EIO.
 */
int
cas_sign_key_generate(const char *path,
                      unsigned char pk[CAS_SIGN_PUBKEY_LEN]);

/** Load a secret key file and derive the pair.
 *
 *  Returns CAS_OK, CAS_ENOTFOUND if there is no such file,
 *  CAS_SIGN_EKEYPERM if its permissions are too open,
 *  CAS_SIGN_EKEYFORM if the contents are not a key file, or
 *  CAS_SIGN_ENOBACKEND.
 */
int
cas_sign_key_load(const char *path,
                  unsigned char sk[CAS_SIGN_SECKEY_LEN],
                  unsigned char pk[CAS_SIGN_PUBKEY_LEN]);

/** Topic id for a public key: BLAKE2b-256 of the 32 key bytes, with no
 *  object framing.  This is the binding that makes a topic name
 *  self-certifying, so it is spelled out rather than left to a caller.
 */
void
cas_sign_topic_id(unsigned char out[CAS_HASH_LEN],
                  const unsigned char pk[CAS_SIGN_PUBKEY_LEN]);

/****************************************************************
 * Version records
 ****************************************************************
 *
 * Fixed-width binary, so exactly one byte sequence encodes a given
 * record and there is no canonical-form question to get wrong.  Byte
 * layout is in FORMAT.md under "Version record".
 */

#define CAS_VREC_TYPE        "vrec"
#define CAS_VREC_LEN         216
#define CAS_VREC_SIGNED_LEN  152  /* the prefix the signature covers */

struct cas_vrec {
    unsigned char topic_id[CAS_HASH_LEN];
    unsigned char pubkey[CAS_SIGN_PUBKEY_LEN];
    uint64_t seq;
    int64_t timestamp;                  /* seconds; informational only */
    char root[CAS_HASH_HEX + 1];        /* the address published */
    char prev[CAS_HASH_HEX + 1];        /* predecessor, "" if first */
};

/** Fill in topic_id from pubkey, then serialize and sign.
 *
 *  The caller supplies seq, timestamp, root, and prev; pubkey must
 *  match sk.  Returns CAS_OK, CAS_SIGN_ENOBACKEND, or CAS_ERR if a
 *  field is malformed.
 */
int
cas_vrec_encode(struct cas_vrec *v,
                const unsigned char sk[CAS_SIGN_SECKEY_LEN],
                unsigned char out[CAS_VREC_LEN]);

/** Parse and fully verify a record.
 *
 *  Checks the length and magic, that topic_id is the hash of the
 *  embedded key, and that the signature covers the record.  There is
 *  no unverified parse: nothing wants one, and offering it would
 *  invite a caller to skip the part that matters.
 *
 *  Returns CAS_OK, or CAS_SIGN_EBADFORM / EBADBIND / EBADSIG /
 *  ENOBACKEND.
 */
int
cas_vrec_decode(const unsigned char *buf, size_t len,
                struct cas_vrec *out);

/** Address of a record: the ordinary object address of its bytes. */
int
cas_vrec_address(const unsigned char rec[CAS_VREC_LEN], char *hash_out);

/****************************************************************
 * Chains
 ****************************************************************
 *
 * A single record proves who published a root.  It says nothing about
 * whether that publication is the newest one, which is a question
 * about two records, and it is the question a subscriber actually has:
 * a signature is just as valid on a version the publisher has since
 * replaced.
 *
 * Records form a chain.  Each names its predecessor by address and
 * carries a sequence number one higher, so the sequence is signed
 * end to end and cannot be reordered or trimmed without the key.
 *
 * seq advances by exactly one.  The publisher assigns it and has no
 * reason to skip, and requiring it makes any hole visible instead of
 * indistinguishable from a record that never reached us.
 */

/** Decide whether `cand` may replace `cur` as a topic's head.
 *
 *  Both must have passed cas_vrec_decode already; this adds nothing to
 *  their individual validity.  `cur` may be NULL for a first head, in
 *  which case only the record's own consistency is required.
 *
 *  Returns CAS_OK to accept, or:
 *
 *    ETOPIC  a different topic entirely
 *    ESTALE  older than, or identical to, the current head
 *    EFORK   a *different* record at the same seq
 *    ECHAIN  the immediate successor, but prev names something else
 *    EGAP    newer, but with seq holes this node has not seen
 *
 *  EFORK deserves its own answer.  A single writer that is behaving
 *  cannot produce two records at one seq, so seeing both is evidence
 *  its key is being used by someone else.  It is not a validation
 *  failure to retry past; it is the one outcome that should stop a
 *  subscriber and raise an alarm.
 *
 *  EGAP is not a rejection either.  It means the caller has to decide:
 *  fetch the intermediate records and re-check, which gets full
 *  continuity, or accept the jump on the signature alone, which is
 *  sound for advancing a head but abandons the audit trail.  Hiding
 *  that choice inside a yes-or-no answer would make it invisible.
 */
int
cas_vrec_succeeds(const struct cas_vrec *cur, const char *cur_addr,
                  const struct cas_vrec *cand);

/** Callback for cas_vchain_walk.  Return nonzero to stop the walk. */
typedef int (*cas_vchain_fn)(const struct cas_vrec *v, const char *addr,
                             void *ctx);

/** Walk a chain backwards from `head_addr`, verifying as it goes.
 *
 *  Each record is fetched from `store`, decoded and verified, and
 *  checked against its successor: same topic, seq exactly one lower,
 *  and the successor's prev naming its address.  `fn` sees each record
 *  from the head down.
 *
 *  Stops when it reaches a record with seq == stop_seq, or the first
 *  record of the chain.  Returns CAS_OK if it got there, or
 *  CAS_SIGN_EINCOMPLETE if a predecessor is not in the store, which is
 *  an ordinary state for a node holding only part of a history rather
 *  than a sign of anything wrong.
 *
 *  A cycle is not possible: a record's address covers its prev field,
 *  so closing a loop would mean predicting a hash.  The walk is bounded
 *  regardless, since seq falls by one each step.
 */
int
cas_vchain_walk(struct cas *store, const char *head_addr,
                uint64_t stop_seq, cas_vchain_fn fn, void *ctx);

#endif /* CAS_SIGN_H */
