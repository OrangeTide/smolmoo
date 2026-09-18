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

### OLC-1: Builder toolkit (shipped)

The convenience commands that make building practical. They are host commands
in `smolmoo.c` beside `@create`/`@set`, not verbs: they are permission
sensitive and must act with the caller's authority, which the host permission
helpers (`is_wizard`, `obj_owner_match`) give directly, and generic `@clone`
becomes trivial in the host (walk `o->props`) with no new syscall. P1's
`@`-to-verb routing still stands for the verb-based world simulation in
OLC-2..5; it just is not what these particular tools needed.

- `@recycle #N`: destroy an object you own (a wizard may destroy any). Closes
  the gap where builders could create but not delete.
- `@clone #N`: copy an object's properties into a new object owned by the
  cloner. Generic (all properties), done in the host; list-valued properties,
  which world objects do not use, are skipped.
- `@move #N to <dest>` (alias `@teleport`): relocate an object you own into a
  room or container.
- `@dig <exit> to <name|#N>`: create a room (or link an existing one) and a
  matching exit pair, the forward exit named as given and the return named
  `back`. Gated to the current room's owner or a wizard.
- `@go #N`: teleport your own avatar to a room (wizard-only builder aid).
- `@find <name>`, `@contents [#N]`: locate objects by name and list a
  container's contents.

Object arguments accept `#N`, `&N`, or a name near the caller (`olc_ref`).
Documented in `help.md`; covered by the smoke suite, including the
ownership gates via the non-admin player.

`@recycle` (and `sys_recycle`) refuse to destroy an object that any other
object still names as its `parent` or `location`, so recycling a prototype or a
non-empty room/container is rejected rather than orphaning children or
stranding contents; empty it first. `@clone` preserves each source property's
permission flags, so a non-world-readable property is not silently exposed on
the copy.

Deferred within OLC-1: a `sys_move` authority check for the verb path is still
open (the host `@move` is gated directly and does not need it). `@clone` still
skips list-valued properties, which world objects do not use.

### OLC-2: reset system (shipped, on-demand)

Nothing used to respawn a killed mob or restock a room, so a world died after
one sweep. This adds a living world through reset rules.

The model is idempotent reconcile rather than death-triggered repop, driven by
data objects rather than a text mini-language:

- A **reset rule** is an object under the Reset Prototype (`#910`, seeded in
  world.data and registered as `#0.reset`) with properties `room`, `proto`,
  `count`, and optional `area`. An **Area Prototype** (`#900`, `#0.area`) is
  seeded for grouping rules, and `@reset #<area>` filters to rules whose `area`
  matches.
- `@reset` (wizard-only, host command) walks every rule, counts the live
  (non-downed) direct children of `proto` already in `room`, and clones the
  shortfall (`obj_create` a fresh child, set its location, clear `downed`,
  owner inherited from the proto). It is idempotent, so it both stocks a fresh
  room and repopulates a cleared one, and a second pass spawns nothing until an
  instance dies. Restocking a container (OLC-3) is the same rule with a
  container as the `room`.

Builders create rules with the OLC-1 tools (`@create #910`, `@set`), so no new
building command was needed. The reconcile is host code, reusing OLC-1's object
iteration and creation; it stays a host command because a background driver
would need a session context a verb does not have.

Deferred by decision (chosen scope: on-demand only): there is no automatic or
periodic trigger yet, so a live world is topped up by running `@reset`. Adding
a driver is the open question for OLC-2..5 as a group, either a host timer (like
autosave) or a reserved "system" session so a timer can invoke sim verbs. The
Area object is only a grouping handle so far; shared-ownership and
export-as-a-unit are not wired.

Two further deferrals came out of the OLC-2 review:

- **Reaping the dead.** The reconcile only adds live instances; it never
  reclaims downed ones, so a room accumulates corpses as persistent objects and
  repeated resets grow the world. This is bounded today (wizard-paced, no auto
  trigger). Reaping belongs with the periodic driver, and it must respect the
  M25i window where a downed body is still lootable, so a decay timer fits
  better than reaping inside `@reset`.
- **Clean spawn state.** A spawn clears only `downed`; it still inherits
  `dead`, `hp`, and other combat state from its proto through the parent chain.
  Pristine templates are fine; a proto that has itself been in combat yields a
  broken instance. Revisit by clearing the full combat block on spawn, or by
  requiring reset protos to be untouched templates.

Depends on: OLC-1 (for building the rules in-game).

### OLC-3: Generic stores and vending machines (shipped)

The store was hardwired in `verb_buy.c`: a single `vendor` property on the room
and one product, the stim. `verb_store.c` (bound to both `buy` and `list`)
generalizes it, keeping the stim shop as a fallback.

- Stock is a data model that needs no schema: a store is any object that holds
  priced item objects (`price > 0`) in its contents. A builder stocks it by
  moving priced items in, all through existing `@` commands and the web editor,
  with no C changes.
- `list [from <store>]` shows the stock at the standing-adjusted price.
  `buy <item> [from <store>]` sells one instance, moving the object to the
  buyer (`sys_move`) and drawing its price off their creds. With no `from` the
  store is the room's `vendor` NPC; a named `from` resolves an object in the
  room, so a vending machine is the same model without an NPC.
- Faction pricing (`cs_standing`) applies uniformly from the store's `faction`:
  each step shifts the price ten percent and Hostile or worse refuses the sale.
- Selling an object instance decrements stock; a reset rule (OLC-2) with the
  store as its `room` restocks it.
- The stim stays a sheet counter, not an object. A store sells it by setting a
  `stim` base-price property; `buy stim` then dispenses a dose through the
  fallback and `list` shows the line. Only a store that opts in this way sells
  stims, so the ChromeSix economy is unchanged (the quartermaster carries
  `stim=75`) and an ordinary vending machine does not dispense them.
- `buy` and `list` are not setuid: they only write the buyer's own sheet and
  `sys_move` (which takes no authority check), so they run at caller authority.
  A non-admin purchase is covered in the tests, since admin status would
  otherwise mask a permission regression.

Deferred: a `sell` verb (players selling back to a store) and generalizing the
stim's counter model into a reusable "consumable that credits a sheet counter"
item type. Neither is needed for builder-run stores; add them if a world calls
for it.

Depends on: OLC-2 (restock is a reset rule).

### OLC-4: Mob behavior (reactive slice shipped)

Templates fight but do not act on their own. Behavior is data-driven by a
`behavior` property on the mob (its own or inherited from a prototype), a
comma-separated list of reactions dispatched to the `on_enter` verb
(`verb_mob.c`).

Shipped (reactive, player-triggered):

- A room-entry hook. When a player enters a room, the host (`mob_enter`, fired
  from `sys_move`) runs `on_enter` on each resident object that carries a
  `behavior` prop, bound with `this` = the NPC and `player` = the newcomer. No
  host timer and no system session: a reaction is a task under the entering
  player's session, so it lives exactly as long as a player is present. This is
  the on-demand analogue of OLC-2's `@reset`.
- `greet`: the NPC says a line to the room (its `greeting` prop, or a default).
- `aggro`: the NPC opens a fight on the newcomer, seeding the `cb_*` state the
  way `verb_attack` does and spawning the `__combat` turn task, which then
  drives the NPC's turns. A second aggressor in a running fight falls in via the
  roster instead of reopening it. `on_enter` is setuid, so aggro's writes to the
  room and both sheets elevate the same way the combat verbs do (proven for a
  non-admin by the P2 migration test).

Deferred (proactive, needs an autonomous driver):

- Wander, patrol, and idle chatter all require a tick with no player present.
  That needs a system session (a reserved connection slot the task loop never
  frees) plus a heartbeat timer that spawns behavior tasks. This is the
  host-timer driver deferred since OLC-2; add it when proactive NPCs are worth
  that change. `sys_move`, `sys_next`, `sys_random`, and `sys_spawn` are the
  primitives a wander/patrol verb would use once the driver exists.

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
