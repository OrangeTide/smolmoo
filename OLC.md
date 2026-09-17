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

### P1: Route `@`-commands to verbs (shipped)

The `@` handler (`cmd_feedback`) now tries a verb named `@tag` through
`verb_dispatch` after its chain of built-in tags and before the feedback
fallback. So a verb whose `verb` property is `@dig` is reached by typing
`@dig`, while an unmatched `@tag` with no verb still logs as feedback
(`@gripe` and friends are unchanged). Built-in tags keep priority, so a verb
cannot shadow `@create` or `@set`. The smoke suite covers both paths.

Decision made: keep the `@` prefix for builder commands. It reads as "builder
or admin command" and is the MUD convention.

### P2: Privilege bracketing (shipped)

The first cut checked the caller's ownership in `sys_setprop`, which blocked
ordinary players from the property writes that combat and other installed verbs
make to shared objects (rooms, NPC sheets). It only appeared to work because
every test account is an admin, which bypasses the checks. A second cut ran
every verb with its owner's authority (LambdaMOO ambient setuid). That worked
but gave trusted verbs broad ambient power and had no way for a verb to say
"only this part is privileged."

The shipped model is explicit privilege bracketing, the setuid/seteuid pattern:

- A verb runs with the caller's authority by default. `sys_setprop` and
  `sys_recycle` check the effective account, which starts as the caller.
- `grant_accept()` raises the effective account to the verb owner, but only if
  the verb carries the setuid capability; `grant_release()` drops back. Both are
  thin wrappers over `sys_setpriv(on)` (syscall 20). Elevation also ends when
  the verb task exits, so a missed release cannot leak past the verb.
- The capability is the setuid bit (`04000`) in the verb's `elf` mode. A verb
  elevates to its owner, so the dangerous case, setuid on a `#0`- or admin-owned
  verb, is reachable only through `@chown`, which is wizard-only. Setuid on a
  verb you own only elevates to yourself, which is no gain.
- Verbs installed from `verbs.conf` that write shared state carry `04755` and
  call `grant_accept()`; a verb written in-game with `@program` is mode `0755`,
  so `grant_accept()` fails for it and a player cannot escalate through a verb
  they own. A spawned task (`sys_spawn`) inherits the spawner's privilege state.

The VM tracks this per task: `vm.caller_acct`, `vm.verb_owner`, `vm.can_elevate`
(from the setuid bit), and `vm.elevated`. A verb owned by the System Object (#0)
elevates to wizard authority. `owner` and `group` stay unsettable as ordinary
properties through `sys_setprop`, matching `@set`, and execute permission still
gates who may run a verb.

The gameplay verbs that write shared state (combat, social, gear, get/put, use,
reload, movement, flee) were migrated to `grant_accept()` at the top of the
verb; narrowing each to bracket only the privileged span is future refinement,
and the MooScript `with priv` block will make that ergonomic. The smoke suite
proves the full model with a non-admin player: a non-setuid verb is refused on a
non-owned object, a setuid verb is refused before `grant_accept`, succeeds after
it, and is refused again after `grant_release`, `grant_accept` cannot elevate a
player's own non-setuid verb, and a non-admin fights an NPC end to end through
the setuid combat verbs.

Note: `sys_move` does not yet carry an authority check. A builder-move guard
belongs with OLC-1's `@move` / `@teleport`, so it is folded into that
milestone rather than P2.

Still deferred: a generic `@clone` (copy an arbitrary object's properties)
cannot be written purely in the VM, because `getprop` is by-name and `sys_next`
walks containment, not property keys. A type-aware clone that copies a known
property list is fully VM-doable today and covers the common cases (clone an
item, clone a mob). A truly generic clone needs one new host primitive, either
`sys_nextprop(obj, idx) -> name` or `sys_clone(src) -> id`. Add this only if a
generic clone is wanted; the type-aware form needs nothing.

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
