/* ambient_fly.c : room ambiance — a tiny fly buzzes past */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "mulibc.h"

static unsigned int seed;

static unsigned int
rng(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

void __attribute__((section(".text.entry")))
_start(void)
{
    int room = vm_args->room;

    seed = (unsigned int)room * 2654435761u + 1;
    sys_wait(0, 0, 0, 0);

    for (;;) {
        int delay = 15000 + (int)(rng() % 15001);
        sys_suspend(delay);
        sys_broadcast(room, "A tiny fly buzzes past.");
    }
}
