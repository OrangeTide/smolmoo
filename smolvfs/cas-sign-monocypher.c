/* cas-sign-monocypher.c : bundled EdDSA-BLAKE2b backend for cas-sign */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

/*
 * Optional.  Compiled only when built with -DCAS_WITH_MONOCYPHER, this
 * supplies the three primitives cas-sign.c dispatches to.
 *
 * The scheme is EdDSA over curve25519 with BLAKE2b, not RFC 8032
 * Ed25519 with SHA-512.  See the note in cas-sign.h: it is a
 * deliberate choice to keep one hash primitive in the system, and it
 * means these signatures do not interoperate with stock Ed25519.
 *
 * An application that would rather bring its own implementation leaves
 * this file out and supplies compatible functions via CAS_SIGN_USER.
 */

#include "cas-sign.h"

#include "monocypher.h"

#include <string.h>

int
cas_sign_eddsa_key_pair(unsigned char sk[CAS_SIGN_SECKEY_LEN],
                        unsigned char pk[CAS_SIGN_PUBKEY_LEN],
                        const unsigned char seed[CAS_SIGN_SEED_LEN])
{
    unsigned char work[CAS_SIGN_SEED_LEN];

    /* crypto_eddsa_key_pair wipes the seed it is handed, which is the
     * right default and the wrong behaviour for a caller who wants to
     * keep theirs.  Copy first. */
    memcpy(work, seed, sizeof(work));
    crypto_eddsa_key_pair(sk, pk, work);
    crypto_wipe(work, sizeof(work));
    return CAS_OK;
}

int
cas_sign_eddsa_sign(unsigned char sig[CAS_SIGN_SIG_LEN],
                    const unsigned char sk[CAS_SIGN_SECKEY_LEN],
                    const void *msg, size_t len)
{
    crypto_eddsa_sign(sig, sk, msg, len);
    return CAS_OK;
}

int
cas_sign_eddsa_check(const unsigned char sig[CAS_SIGN_SIG_LEN],
                     const unsigned char pk[CAS_SIGN_PUBKEY_LEN],
                     const void *msg, size_t len)
{
    /* crypto_eddsa_check returns 0 on success, -1 otherwise, and is
     * constant time. */
    return crypto_eddsa_check(sig, pk, msg, len) == 0
           ? CAS_OK : CAS_ERR;
}
