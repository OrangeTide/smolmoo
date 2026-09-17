/* verb_test_priv.c : exercise privilege bracketing. A setuid verb writes a
 * property on its dobj before elevation (caller authority), after grant_accept
 * (verb owner authority), and after grant_release (caller again). */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

void
_start(void)
{
    int d = vm_args->dobj;

    puts(sys_setprop(d, "_pa", "x") == 0 ? "PRE:OK" : "PRE:DENIED");
    grant_accept();
    puts(sys_setprop(d, "_pb", "x") == 0 ? "POST:OK" : "POST:DENIED");
    grant_release();
    puts(sys_setprop(d, "_pc", "x") == 0 ? "DROP:OK" : "DROP:DENIED");
    _exit(0);
}
