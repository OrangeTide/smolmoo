/* verb_look.c : look verb for the smolmoo RV32 VM */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

void
_start(void)
{
    int room = vm_args->room;
    char buf[256];
    int n;

    n = sys_getprop(room, "name", buf, sizeof(buf));
    if (n > 0) {
        write(1, "=== ", 4);
        write(1, buf, n);
        write(1, " ===\n", 5);
    }

    n = sys_getprop(room, "description", buf, sizeof(buf));
    if (n > 0) {
        write(1, buf, n);
        write(1, "\n", 1);
    }

    _exit(0);
}
