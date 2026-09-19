/* verb_reflect.c : reports whether its dobj argument equals `this`, used to
 * check that a verb call delivered an object argument as dobj. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

int
main(void)
{
    if (vm_args->dobj == vm_args->this_obj)
        puts("REFLECT:MATCH");
    else
        puts("REFLECT:NOMATCH");
    _exit(0);
}
