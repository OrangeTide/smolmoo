/* verb_test_args.c : test verb for typed slot matching */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

int
main(void)
{
    char *p;

    write(1, "DOBJ=", 5);
    p = (char *)vm_args->dobjstr;
    if (p && *p) write(1, p, strlen(p));
    write(1, " PREP=", 6);
    p = (char *)vm_args->prepstr;
    if (p && *p) write(1, p, strlen(p));
    write(1, " IOBJ=", 6);
    p = (char *)vm_args->iobjstr;
    if (p && *p) write(1, p, strlen(p));
    write(1, "\n", 1);
    _exit(0);
}
