/* verb_greet.c : a target verb for testing sys_call verb-to-verb dispatch */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

int
main(void)
{
    write(1, "GREETED:", 8);
    if (vm_args->arglen > 0)
        write(1, vm_args->argstr, vm_args->arglen);
    write(1, "\n", 1);
    _exit(0);
}
