<!-- SPDX-License-Identifier: 0BSD OR CC0-1.0 -->
# Online Creation (OLC) Plan

The goal is to let builders log into the running game and create content:
areas, rooms, exits, items, NPCs and mobs, stores and vending machines,
and vehicles such as carriages, trains, and elevators. This document is a
plan, not committed work. It records the design decision, the small host
prerequisites, and a milestone sequence.

## Design decision: build OLC as verbs

Almost all of OLC can run as verbs on the RV32 VM rather than as host C in
`smolmoo.c`. This is the preferred approach for three reasons: it keeps the
core server small (the project's central constraint), it lets builders revise
the OLC tools in-game through `@program` without a server rebuild, and it
reuses the pattern the ChromeSix combat and social systems already prove works.

The verb syscall surface already covers what OLC needs:

- `sys_create` (16), `sys_recycle` (17): object lifecycle
- `sys_setprop` (8), `sys_getprop` (7): property read and write
- `sys_move` (13), `sys_next` (14): containment move and iteration
- `sys_objfind` (9): find an object by name
- `sys_spawn` (10), `sys_suspend` (11), `sys_wait` (5): timed and event-driven tasks
- `sys_call` (18), `sys_hasverb` (19): verb-to-verb dispatch
- `sys_random` (12), `sys_rollup` (15), `sys_broadcast` (6)

Permissions are enforced host-side inside these calls, so a builder verb
cannot escalate. `sys_recycle` checks `is_wizard || obj_owner_match` before
freeing, `sys_setprop` checks `perm_can_write` on existing properties, and
`sys_create` stamps the caller's account as the new object's `owner`. A verb
inherits correct ownership behavior for free.

### What stays in the host

The existing builder commands are host C and working: `@create`, `@set`,
`@examine`, `@chmod`, `@chown`, `@chgrp`, and `@group`. Porting them to verbs
would add VM code and duplicate logic for no gain, against the size
constraint. They stay where they are. OLC builds new tools alongside them.

## Host prerequisites

Two small host changes unlock the verb-based approach. Both are one-time.

### P1: Route `@`-commands to verbs

Today an unknown `@tag` never reaches `verb_dispatch`. The `@` handler runs
its chain of built-in tags and, on no match, falls through to the feedback
log (the `@gripe` / `@typo` path). Plain-word commands already fall through
to verbs, so a builder verb named `dig` or `recycle` works today with no host
change. To keep the `@` convention for builder verbs, the `@` handler needs a
`verb_dispatch` attempt before the feedback fallback. About three lines.

Decision needed: keep the `@` prefix for builder commands (needs P1), or use
plain words for the verb-based tools (no host change). The `@` prefix reads as
"builder or admin command" and is the MUD convention, so P1 is recommended.

### P2: Tighten `sys_setprop`, and add a clone primitive

`sys_setprop` only permission-checks a property that already exists. Setting a
new property on an object is not gated by ownership. Once builder tools run as
verbs this matters more, since a verb could add properties to an object it
does not own. Add an ownership check for the new-property case.

Separately, a generic `@clone` (copy an arbitrary object's properties) cannot
be written purely in the VM, because `getprop` is by-name and `sys_next` walks
containment, not property keys. A type-aware clone that copies a known
property list is fully VM-doable today and covers the common cases (clone an
item, clone a mob). A truly generic clone needs one new host primitive, either
`sys_nextprop(obj, idx) -> name` to enumerate keys or `sys_clone(src) -> id`.
Add this only if a generic clone is wanted; the type-aware form needs nothing.

## Milestones

Each milestone after OLC-1 is entirely verb work, on top of the prerequisites.

### OLC-1: Builder toolkit

The convenience commands that make building practical. The raw operations
exist; these package them, and one operation (delete) has no in-game form yet.

- `@recycle #N`: delete an object (`sys_recycle`, owner or wizard). Closes the
  current gap where builders can create but not destroy.
- `@dig <exit> to [#room]`: create a room if no destination is given, then
  create the exit object in the current room and a matching return exit
  (`sys_create`, `sys_setprop`, `sys_move`). One step instead of several.
- `@clone #N`: instance an object from a template with its properties copied.
  Type-aware form works today; generic form needs P2.
- `@move` / `@teleport #N to #R`: relocate an object (`sys_move`).
- `@go #R`: move the builder's own avatar (`sys_move`; `verb_go` already moves
  a player through an exit).
- `@find <name>`, `@contents #R`: locate and list (`sys_objfind`, `sys_next`).

Depends on: P1 (for the `@` names), P2 (only for generic `@clone`).

### OLC-2: Area and reset system

The largest and most important subsystem. Nothing currently respawns a killed
mob or restocks a room, so a world dies after one sweep. This adds a living
world.

- An Area object groups a set of rooms for shared ownership, reset rules, and
  export as a unit. Today `export` and `merge` move id ranges, which is
  area-like but not modeled.
- Reset rules describe what a room should hold: for example, one raider in
  room #R, repop five minutes after death; a crate holding two stims,
  refilled on reset.
- A reset runner is a persistent task verb that wakes on a timer
  (`sys_spawn` with delay, or `sys_suspend`), checks each rule against the
  current world (`sys_next`, `sys_getprop`), and creates or moves objects to
  satisfy it (`sys_create`, `sys_move`).

Depends on: OLC-1 (for building the areas and rules in-game).

### OLC-3: Generic stores and vending machines

The current store is hardwired in `verb_buy.c`: a single `vendor` property on
the room and one product, the stim. Generalize it.

- A stock data model on the store or vendor object: a list of items for sale
  with prices, quantities, and restock behavior, all settable by a builder
  through `@set` and the web editor, with no C changes.
- A generic `buy` / `list` / `sell` verb that reads that stock
  (`sys_getprop`), applies the existing faction pricing, and completes the
  sale (`sys_move`, `sys_setprop`).
- A vending machine is the same model without an NPC: an object the player
  `buy`s from or `use`s directly.

Depends on: OLC-2 (restock is a reset rule).

### OLC-4: Mob behavior

Templates fight but do not act on their own. Add behavior verbs.

- Wander: move between adjacent rooms on a timer (`sys_move`, `sys_next`,
  `sys_random`, `sys_spawn`).
- Aggro: attack an eligible target on sight (`sys_call` into the existing
  combat verb).
- Patrol: follow a fixed route.
- Greet and idle chatter, building on the existing `verb_greet` demo.

Depends on: OLC-2 (spawned mobs need somewhere to come from and return to).

### OLC-5: Vehicles

Carriages, trains, and elevators are moving rooms: an enterable object that
relocates between rooms on a schedule or on command, carrying its occupants.
The primitives exist; this defines the pattern.

- Board and disembark: enter and leave a room-like container (`sys_move` the
  player into or out of the vehicle object).
- A route data model: the ordered stops and, for a train or elevator, a
  timetable.
- A scheduler verb that advances the vehicle along its route on a timer,
  relocating the vehicle (and thus its occupants) and announcing arrivals
  (`sys_spawn` / `sys_suspend`, `sys_move`, `sys_broadcast`). An elevator is
  the on-command variant; a train is the timed variant; a carriage can be
  either.

Depends on: OLC-1 (build the vehicle and its stops), OLC-2 (a vehicle can be a
reset-managed object).

## Cross-cutting notes

- Builder sandbox: the deferred world-branching idea in FUTURE.md would let
  builders work off a fork of the live world and merge when ready. It pairs
  naturally with OLC but is not a prerequisite.
- Ownership quotas: as builders gain `@create` and `@clone`, a per-account cap
  on object count would prevent id exhaustion. A host-side check, small, worth
  considering with OLC-1.
- Prototype discovery: `@create` needs the builder to know parent ids. A small
  `@proto list` (host or verb) that lists the base prototypes would help.
</content>
