/* verb_test_create.c : test the sys_create and sys_recycle syscalls */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

void
_start(void)
{
    int id = sys_create(100);   /* new child of the Room Prototype (#100) */

    if (id <= 0) {
        puts("CREATE:FAIL");
        _exit(0);
    }
    /* the new object is usable: set a property, then destroy it */
    if (sys_setprop(id, "_test", "made") != 0) {
        puts("CREATE:NOPROP");
        _exit(0);
    }
    if (sys_recycle(id) != 0) {
        puts("CREATE:NORECYCLE");
        _exit(0);
    }
    puts("CREATE:OK");
    _exit(0);
}
