/* verb_ignore.c : no-op default verb for the smolmoo RV32 VM */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

int
main(void)
{
    _exit(0);
}
