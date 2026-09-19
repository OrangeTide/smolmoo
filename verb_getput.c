/* verb_getput.c : pick up, drop, and stash loose items. get/drop move an item
 * between the room floor and your hands; put stows one in an open container;
 * "get X from Y" pulls from an open container or strips a fallen body. Worn or
 * wielded gear is left to the remove verb. (ChromeSix, Section 9.) */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* case-insensitive equality, item names match the way the parser resolved them */
static int
ci_eq(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = *a, cb = *b;

        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* is `item` a direct child of `parent`? */
static int
holds(int parent, int item)
{
    int it = 0;

    while ((it = sys_next(parent, it)) != 0)
        if (it == item) return 1;
    return 0;
}

/* first child of `parent` whose name matches `name`, or 0 */
static int
child_named(int parent, const char *name)
{
    int it = 0;
    char nm[32];

    while ((it = sys_next(parent, it)) != 0) {
        cs_getstr(it, "name", nm, sizeof(nm));
        if (ci_eq(nm, name)) return it;
    }
    return 0;
}

/* an item bound to a body slot is worn or wielded; get/drop/put leave it be. */
static int
slotted(int item)
{
    char s[16];

    cs_getstr(item, "slot", s, sizeof(s));
    return s[0] != '\0';
}

/* a creature (has anatomy) is not a loose item, and a body is carried, not
 * pocketed. */
static int
is_creature(int obj)
{
    char an[8];

    cs_getstr(obj, "anatomy", an, sizeof(an));
    if (an[0]) return 1;
    return cs_geti(obj, "charid", -1) >= 0;
}

/* broadcast "<actor> <tail>" to the room, excluding no one but the parser
 * already shows the actor their own private line. */
static void
announce(int room, const char *actor, const char *tail)
{
    struct cs_out o;

    o.len = 0;
    cs_s(&o, actor);
    cs_s(&o, tail);
    sys_broadcast(room, cs_cstr(&o));
}

/* private "You <verb> the <name>[ <tail>]." line to the actor */
static void
tell(const char *lead, const char *name, const char *tail)
{
    struct cs_out o;

    o.len = 0;
    cs_s(&o, lead);
    cs_s(&o, name);
    if (tail) cs_s(&o, tail);
    cs_s(&o, ".");
    puts(cs_cstr(&o));
}

int
main(void)
{
    int self = vm_args->player;
    int room = vm_args->room;
    int item = vm_args->dobj;
    int dst = vm_args->iobj;
    char *verb = (char *)vm_args->verb;
    char nm[32], an[32];

    grant_accept();   /* moves items in and out of containers not owned by the caller */

    cs_getstr(self, "name", an, sizeof(an));

    if (cs_streq(verb, "drop")) {
        if (item <= 0) { puts("Drop what?"); _exit(0); }
        if (!holds(self, item)) { puts("You aren't carrying that."); _exit(0); }
        if (slotted(item)) {
            puts("You are using that; remove it first.");
            _exit(0);
        }
        cs_getstr(item, "name", nm, sizeof(nm));
        sys_move(item, room);
        tell("You drop the ", nm, 0);
        announce(room, an, " drops something.");
        _exit(0);
    }

    if (cs_streq(verb, "put")) {
        if (item <= 0) { puts("Put what?"); _exit(0); }
        if (dst <= 0) { puts("Put it in what?"); _exit(0); }
        if (!holds(self, item)) { puts("You aren't carrying that."); _exit(0); }
        if (slotted(item)) {
            puts("You are using that; remove it first.");
            _exit(0);
        }
        if (cs_geti(dst, "cap", 0) <= 0) {
            puts("You can't put things in that.");
            _exit(0);
        }
        if (item == dst || cs_geti(item, "cap", 0) > 0) {
            puts("That won't nest inside another container.");
            _exit(0);
        }
        if (cs_count(dst) >= cs_geti(dst, "cap", 0)) {
            char cn[32];

            cs_getstr(dst, "name", cn, sizeof(cn));
            tell("It won't fit in the ", cn, 0);
            _exit(0);
        }
        cs_getstr(item, "name", nm, sizeof(nm));
        sys_move(item, dst);
        tell("You stow the ", nm, 0);
        announce(room, an, " stows something away.");
        _exit(0);
    }

    /* get: from a source (container or body) when one is named, else the floor */
    if (item <= 0 && dst <= 0) { puts("Get what?"); _exit(0); }

    if (dst > 0) {
        int body = 0, from;
        struct cs_out o;
        char cn[32];

        cs_getstr(dst, "name", cn, sizeof(cn));
        if (cs_geti(dst, "cap", 0) <= 0) {
            if (cs_geti(cs_sheet(dst), "dead", 0)) body = 1;
            else { puts("You can't take things from that."); _exit(0); }
        }
        /* resolve the item within the named source, ignoring any same-named
         * item the parser found in a different place. */
        from = child_named(dst, vm_args->dobjstr);
        if (from <= 0) {
            o.len = 0;
            cs_s(&o, "There is no ");
            cs_s(&o, vm_args->dobjstr);
            cs_s(&o, " in the ");
            cs_s(&o, cn);
            cs_s(&o, ".");
            puts(cs_cstr(&o));
            _exit(0);
        }
        cs_getstr(from, "name", nm, sizeof(nm));
        /* a looted item may still name a slot on the body; clear it so it reads
         * as loose gear once it is in your hands. */
        sys_setprop(from, "slot", "");
        sys_move(from, self);
        if (body) {
            o.len = 0;
            cs_s(&o, "You strip the ");
            cs_s(&o, nm);
            cs_s(&o, " from ");
            cs_s(&o, cn);
            cs_s(&o, ".");
            puts(cs_cstr(&o));
            announce(room, an, " strips gear from a body.");
        } else {
            o.len = 0;
            cs_s(&o, "You take the ");
            cs_s(&o, nm);
            cs_s(&o, " from the ");
            cs_s(&o, cn);
            cs_s(&o, ".");
            puts(cs_cstr(&o));
            o.len = 0;
            cs_s(&o, an);
            cs_s(&o, " takes something from the ");
            cs_s(&o, cn);
            cs_s(&o, ".");
            sys_broadcast(room, cs_cstr(&o));
        }
        if (cs_encumbrance(self) >= 2) puts("You are overloaded.");
        _exit(0);
    }

    if (item <= 0) { puts("You don't see that here."); _exit(0); }
    if (is_creature(item)) { puts("You can't pick that up."); _exit(0); }
    if (holds(self, item)) { puts("You already have that."); _exit(0); }
    cs_getstr(item, "name", nm, sizeof(nm));
    sys_move(item, self);
    tell("You pick up the ", nm, 0);
    announce(room, an, " picks something up.");
    if (cs_encumbrance(self) >= 2) puts("You are overloaded.");
    _exit(0);
}
