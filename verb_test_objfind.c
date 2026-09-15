/* verb_test_objfind.c : test the sys_objfind syscall */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

void
_start(void)
{
    char *name = (char *)vm_args->dobjstr;
    int obj = sys_objfind(name);

    if (obj == -1)
        puts("OBJFIND:NONE");
    else
        puts("OBJFIND:OK");
    _exit(0);
}
