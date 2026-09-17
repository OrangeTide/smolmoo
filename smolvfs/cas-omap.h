/* cas-omap.h : sparse object map -- numeric ID to CAS hash */
/* PUBLIC DOMAIN (CC0-1.0) */

#ifndef CAS_OMAP_H
#define CAS_OMAP_H

#include "cas.h"

#include <stdint.h>

/****************************************************************
 * Constants
 ****************************************************************/

#define CAS_OMAP_PAGE_SLOTS 256
#define CAS_OMAP_PAGE_SIZE  (CAS_OMAP_PAGE_SLOTS * CAS_HASH_LEN)

enum {
    CAS_OMAP_EFULL = -6,
};

/****************************************************************
 * Data structures
 ****************************************************************/

/** Opaque object map handle. */
struct cas_omap;

/****************************************************************
 * Lifecycle
 ****************************************************************/

/** Create a new empty object map backed by a CAS store. */
struct cas_omap *
cas_omap_new(struct cas *store);

/** Destroy an object map and free all memory. */
void
cas_omap_free(struct cas_omap *om);

/** Return the underlying CAS store. */
struct cas *
cas_omap_cas(struct cas_omap *om);

/****************************************************************
 * Snapshot
 ****************************************************************/

/** Load an object map from a previously stored root hash.
 *  Replaces any existing in-memory state.
 */
int
cas_omap_load(struct cas_omap *om, const char *root_hash);

/** Store the current object map state to CAS.
 *  Returns the root hash in hash_out (must be CAS_HASH_HEX+1).
 *  Only dirty pages are written; clean pages reuse existing hashes.
 */
int
cas_omap_store(struct cas_omap *om, char *hash_out);

/****************************************************************
 * Lookup
 ****************************************************************/

/** Look up the hash for a numeric ID.
 *  hash_out must be at least CAS_HASH_HEX+1 bytes.
 *  Returns CAS_ENOTFOUND if the ID is not mapped.
 */
int
cas_omap_get(struct cas_omap *om, uint64_t id, char *hash_out);

/** Check whether an ID is mapped. Returns nonzero if present. */
int
cas_omap_exists(struct cas_omap *om, uint64_t id);

/****************************************************************
 * Mutation
 ****************************************************************/

/** Map an ID to a hash. Creates the page if needed.
 *  hash must be a valid CAS_HASH_HEX-length hex string.
 */
int
cas_omap_put(struct cas_omap *om, uint64_t id, const char *hash);

/** Remove a mapping. Returns CAS_ENOTFOUND if not mapped. */
int
cas_omap_del(struct cas_omap *om, uint64_t id);

/** Clear all mappings, resetting to an empty map. */
void
cas_omap_clear(struct cas_omap *om);

/****************************************************************
 * Allocation
 ****************************************************************/

/** Find the first unmapped ID >= start, below start + limit.
 *  Pass limit=0 for no upper bound.
 *  Does NOT reserve the ID -- caller must cas_omap_put() afterward.
 *  Returns CAS_OMAP_EFULL if no free ID exists in the range.
 */
int
cas_omap_alloc(struct cas_omap *om, uint64_t start, uint64_t limit,
               uint64_t *id_out);

/****************************************************************
 * Iteration and statistics
 ****************************************************************/

/** Return the total number of mapped entries. */
uint64_t
cas_omap_count(struct cas_omap *om);

/** Return the number of populated pages. */
int
cas_omap_page_count(struct cas_omap *om);

/** Callback for cas_omap_foreach. Return 0 to continue. */
typedef int (*cas_omap_foreach_fn)(uint64_t id, const char *hash,
                                   void *ctx);

/** Iterate over all mapped entries in ID order. */
int
cas_omap_foreach(struct cas_omap *om, cas_omap_foreach_fn fn,
                 void *ctx);

#endif /* CAS_OMAP_H */
