/* verb_test_random.c : test the sys_random syscall */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

void
_start(void)
{
    int ok = 1;

    /* 50 d6 rolls must all land in [0,6) */
    for (int i = 0; i < 50; i++) {
        int r = sys_random(6);
        if (r < 0 || r >= 6)
            ok = 0;
    }
    /* range of 1 is always 0 */
    if (sys_random(1) != 0)
        ok = 0;
    /* invalid range reports an error (negative) */
    if (sys_random(0) >= 0)
        ok = 0;

    puts(ok ? "RANDOM:OK" : "RANDOM:BAD");
    _exit(0);
}
