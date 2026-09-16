/* moo_rt.c : MooScript verb runtime for the smolmoo RV32 VM.
 *
 * Compiled with the stack-convention compiler (skj-cc-rv) so its calling
 * convention matches the MooScript backend (all call arguments on the stack,
 * result in a0). It supplies the ELF entry (_start), the bump arena the
 * MooScript runtime and host bridge allocate from, and the mapping from the
 * host's vm_args block to the compiled verb's entry function.
 *
 * Every MooScript verb file defines its entry as `verb main(...)`, which the
 * backend emits as a global `main` symbol. The verb word players type is set
 * separately on the verb object (verbs.conf), so it is independent of this
 * symbol. Entry arguments are passed in vm_args order:
 *
 *     verb main(player: obj, room: obj, this: obj, dobj: obj, iobj: obj,
 *               arg: str)
 *
 * A verb declares only the leading parameters it uses; the rest are ignored.
 */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

struct moo_str {
    int len;
    const char *data;
};

struct vm_args {
    int player;
    int room;
    char *argstr;
    int arglen;
    int this_obj;
    int dobj;
    int iobj;
    char *dobjstr;
    char *iobjstr;
    char *prepstr;
    char *verb;
};

/* End of BSS, defined by vm_rv.ld: the arena grows up from here, the stack
 * grows down from the top of guest RAM. */
extern char __moo_arena[];

extern void _exit(int status);
extern void main(int player, int room, int this_obj, int dobj, int iobj,
                 void *arg);

static char *arena_ptr;

void
__moo_arena_reset(void)
{
    arena_ptr = __moo_arena;
}

void *
__moo_arena_alloc(int size)
{
    char *p = arena_ptr;

    arena_ptr = p + ((size + 7) & ~7);
    return p;
}

void
_start(void)
{
    volatile struct vm_args *va = (volatile struct vm_args *)0x380;
    struct moo_str *arg;

    __moo_arena_reset();

    arg = __moo_arena_alloc(8);
    arg->len = va->arglen;
    arg->data = va->argstr;

    main(va->player, va->room, va->this_obj, va->dobj, va->iobj, arg);
    _exit(0);
}
