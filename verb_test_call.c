/* verb_test_call.c : test the sys_call verb-to-verb dispatch syscall */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

void
_start(void)
{
    /* invoke the `greet` verb on the current room, passing a string argument */
    int rc = sys_call(vm_args->room, "greet", "world");

    if (rc == 0)
        puts("CALL:OK");
    else
        puts("CALL:FAIL");

    /* sys_hasverb: greet resolves, a made-up name does not */
    puts(sys_hasverb(vm_args->room, "greet") ? "HASVERB:YES" : "HASVERB:NO");
    puts(sys_hasverb(vm_args->room, "nonesuch") ? "HASVERB:BAD"
                                                : "HASVERB:MISS");
    _exit(0);
}
