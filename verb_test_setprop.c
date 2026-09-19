/* verb_test_setprop.c : test the sys_setprop syscall */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

int
main(void)
{
    int rc = sys_setprop(vm_args->dobj, "_test", "hello");

    if (rc == 0)
        puts("SETPROP:OK");
    else
        puts("SETPROP:DENIED");
    _exit(0);
}
