/* version.h : smolvfs library version */
/* PUBLIC DOMAIN (CC0-1.0) */

#ifndef SMOLVFS_VERSION_H
#define SMOLVFS_VERSION_H

/* Single source of truth for the library version.  Bump these on a
 * release and tag the commit v<MAJOR>.<MINOR>.<PATCH>; CI rejects a
 * v* tag whose number does not match (see
 * .github/workflows/release.yml).  The version travels with the
 * source, so a vendored copy is identifiable from this header alone. */
#define SMOLVFS_VERSION_MAJOR 0
#define SMOLVFS_VERSION_MINOR 4
#define SMOLVFS_VERSION_PATCH 1

#define SMOLVFS_VERSION_STR_(x) #x
#define SMOLVFS_VERSION_STR(x)  SMOLVFS_VERSION_STR_(x)

/* "MAJOR.MINOR.PATCH", derived from the numbers above so there is one
 * place to edit. */
#define SMOLVFS_VERSION \
    SMOLVFS_VERSION_STR(SMOLVFS_VERSION_MAJOR) "." \
    SMOLVFS_VERSION_STR(SMOLVFS_VERSION_MINOR) "." \
    SMOLVFS_VERSION_STR(SMOLVFS_VERSION_PATCH)

#endif /* SMOLVFS_VERSION_H */
