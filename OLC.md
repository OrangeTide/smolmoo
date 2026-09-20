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
- `sys_getobj` (23): read an objref property's value (location, dest, ...)
- `sys_post` (24), `sys_taskid` (25): wake a task by id / read the current id
  (resume a blocked task, such as the combat turn loop)
- `sys_notify` (26): deliver an `EV_USER` (with a string arg) to the agent
  listening on an object, so a control verb routes a request to its engine
- `sys_move` (13), `sys_next` (14): containment move and iteration
- `sys_objfind` (9): find an object by name
- `sys_spawn` (10), `sys_suspend` (11): timed and background tasks
- `sys_call` (18), `sys_hasverb` (19): verb-to-verb dispatch
- `sys_random` (12), `sys_rollup` (15), `sys_broadcast` (6)
- `sys_listen` (22), `sys_getmsg` (21): the OLC-5 event mailbox

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

The gameplay verbs that write shared state were migrated to `grant_accept()`.
How far the elevation reaches is a per-verb call, not a pending refinement.
Where the privileged span is small (a single transfer surrounded by validation,
messages and reads) the verb brackets only that span with
`grant_accept()`/`grant_release()`, so the rest runs at the caller's authority:
`buy` (the stock move), `board`/`disembark` (the relocate), and `get`/`put`/
`drop` (the item move, plus the slot clear on a looted item) do this. The
combat and social verbs instead run their whole body elevated by design: they
write room state and combatant sheets throughout (roster, `cb_*`, band, wounds,
BP, free strikes), so there is no meaningful unprivileged span to hold back.
The elevation always ends when the verb task exits, so a missed
`grant_release()` cannot leak past the verb. The smoke suite
proves the full model with a non-admin player: a non-setuid verb is refused on a
non-owned object, a setuid verb is refused before `grant_accept`, succeeds after
it, and is refused again after `grant_release`, `grant_accept` cannot elevate a
player's own non-setuid verb, and a non-admin fights an NPC end to end through
the setuid combat verbs.

`sys_move` carries the same authority check as `sys_setprop`: the effective
account must own the object (or be a wizard, or a `#0`-owned system verb). A
verb that relocates objects it does not own (buy, get/put, combat, movement) is
setuid and elevates for the move; a player's own `0755` verb can move only what
they own.

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

Deferred within OLC-1: `@clone` still skips list-valued properties, which world
objects do not use.

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

Two further deferrals came out of the OLC-2 review, both now shipped:

- **Reaping the dead (shipped).** A defeated NPC is left `downed`, which the
  reconcile counts as not-live, so it spawns a replacement while the body
  lingers and repeated resets grow the world. A host sweep (`corpse_sweep_cb`,
  `CORPSE_SWEEP_MS`) now reaps fallen bodies: it arms a `decay_tick` clock on a
  fresh body, counts it down (`corpse_decay_ms`, the lootable window), and at
  zero frees the body and any gear still on it. Reaping is scoped to instances
  the reset spawned (a `reset_spawn` marker), so a builder's hand-placed NPC is
  left alone, and it skips a room that is still in a fight (`cb_active`). Player
  bodies are ephemeral and handled by the M25i death sweep, so they are never
  touched. This is the decay-timer approach the review preferred over reaping
  inside `@reset`.
- **Clean spawn state (shipped).** A reset clone inherits its proto's
  properties through the parent chain, so a proto that has itself been in combat
  would yield an instance that starts downed, wounded, or dead. `spawn_clean`
  now restores full BP and clears the whole combat/death block (`downed`,
  `dead`, `dying`, `wounds`, `death_tick`, `decay_tick`, `band`) on every reset
  spawn, so an instance is clean regardless of the proto's current state.

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
- `list` is not setuid, and `buy` writes the buyer's own sheet at caller
  authority. Because the stock is store-owned, `buy` is setuid and elevates only
  for the `sys_move` transfer (a narrow `grant_accept`/`grant_release` bracket),
  then charges the buyer's own creds unelevated. A non-admin purchase is covered
  in the tests, since admin status would otherwise mask a permission regression.

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

The hook fires on every way a player enters a room: the `go`/`flee` verbs
(through `sys_move`) and the host `@go` and login paths. A player who walks into
a fight already in progress is not drawn in, since `__combat` tracks one player;
they `attack` to join. Reactions never fire for a non-player entrant, so future
wandering NPCs will not set each other off.

Deferred (proactive, needs an autonomous driver):

- Wander, patrol, and idle chatter all require a tick with no player present.
  That needs a system session (a reserved connection slot the task loop never
  frees) plus a heartbeat timer that spawns behavior tasks. This is the
  host-timer driver deferred since OLC-2; add it when proactive NPCs are worth
  that change. `sys_move`, `sys_next`, `sys_random`, and `sys_spawn` are the
  primitives a wander/patrol verb would use once the driver exists.

Depends on: OLC-2 (spawned mobs need somewhere to come from and return to).

### OLC-5: Event system and runtime message loop (shipped)

The dependency the rest of the arc was waiting on. OLC-4's reactive mobs are
player-triggered because a VM task is bound to a session and reaped when it
ends, so nothing runs with no player present. Timed vehicles (old OLC-5) and
proactive mobs (wander, patrol) both need an autonomous tick. Rather than a
one-off timer, build a holistic host-to-VM event channel so a timer is just one
kind of event.

Locked design:

- Events are delivered cooperatively, never as an interrupt. The VM observes an
  event only at a yield point (`sys_suspend`), so there is no signal
  re-entrancy; events queue in the host while a verb runs and are handed over on
  the next block. An earlier embryonic seam (`sys_open` `O_VERB`, `sys_wait`,
  `sys_read`, `verb_deliver`) explored this for player commands; it was
  superseded by the mailbox path below and removed.
- The runtime owns the loop. The CRT provides `_start`, always the ELF entry,
  and always calls `main()`. A simple verb defines its own `main()`, runs, and
  returns (fire-and-forget, today's model). An agent does not define `main()`;
  it links `-lverbmain`, whose `main()` registers as its object's handler
  (`sys_listen`) and then blocks for events, draining the mailbox with
  `sys_getmsg` and calling `on_event` on each. When the dwell elapses with an
  empty mailbox the loop synthesizes an `EV_TIMER`, so the agent supplies just
  `on_event` and `verb_dwell` (the tick period). Opt-in is a link choice, so an
  executable is exactly one kind. (Stage 5a, done: CRT owns `_start`, C verbs
  migrated `_start` -> `int main(void)`, no behavior change.)
- Events are typed. `vm_event` gains an int `type` (`EV_USER`, `EV_TIMER`,
  `EV_ENTER`, ...) kept in sync host/VM like the syscall numbers, plus an int
  `tag` cookie. `EV_USER` is the string-verb path: `verb`/`dobj`/`iobj`/`argstr`
  stay meaningful only for it, so a new player command is still just a new verb
  string with no C change, while the engine's own events cost no `strcmp`.
- Each handler task gets a small bounded FIFO mailbox (a ring, drop-oldest with
  a count on overflow) so a second event cannot clobber an unread one. The host
  timer stays the `pq` heap; the mailbox is a per-task ring.
- Autonomous agents run under a reserved system session the reaper never frees;
  player-scoped handlers still live with their session.
- Two syscalls back the mailbox: `sys_listen` (22) routes an object's events to
  the calling task, and `sys_getmsg` (21) pops one event (or reports the box is
  empty). The periodic tick reuses the existing `sys_suspend`; the runtime turns
  an elapsed dwell into an `EV_TIMER` rather than the host scheduling one. The
  entry hook (OLC-4) delivers `EV_ENTER` to a live handler's mailbox instead of
  spawning a fresh `on_enter` task.
- Build plumbing: the `agent_` source prefix tells the installer (and
  `program_compile`) to link `verbmain.o`. Repo convention: `verb_*.c` for
  one-shot verbs, `agent_*.c` for event-loop agents. An `@program ... agent`
  token for in-game agent authoring is a follow-up.
- Launch: `@wake #N` (wizard only) starts object `#N` as an agent under the
  reserved system session; `#N.brain` names the agent verb. `@wake` also marks
  `#N.awake=1` in the persistent world, and a boot-time scan re-wakes every
  object so marked, so the living world (wandering mobs, running vehicles) comes
  back on its own after a restart. `@sleep #N` stops a running agent and clears
  the flag, so it stays stopped. The scan also runs after `@rewind` reloads the
  world in place: the old world's agents are stopped and the restored world's
  awake objects are re-woken.

Shipped: the event bus, the `@wake`/`@sleep` launcher with boot-scan
persistence, and a demo agent (`__ticker`, `#452`) that ticks with no player
present and announces a newcomer on `EV_ENTER`. Follow-up: `@program ... agent`
for in-game agent authoring.

`__combat` was not converted into a `verbmain` agent: it is a sequential,
self-terminating, per-fight task (bound to the player's session for reaping),
which the persistent `for(;;)` agent model fits poorly. Instead it became an
event-bus consumer the right way: it blocks the player's turn on one
`sys_suspend` (no polling of `cb_acted`), and a combatant's action verb wakes it
at once through `cs_end_turn`. The turn task publishes its own id in the room's
`cb_task` prop (`sys_taskid`, syscall 25) and the action verb wakes it by that
id (`sys_post(task_id)`, syscall 24, a bare `EV_WAKE`). It deliberately does not
`sys_listen` on the room, so combat never intercepts other commands: an
unrecognized command typed mid-fight still reports as unknown. Combat commands
stay ordinary verbs that route their result to the engine.

Depends on: the `pq` timer (already present) and the OLC-4 entry hook.

### OLC-6: Proactive mob behavior (shipped)

The deferred half of OLC-4. A mob woken as an agent (`agent_mob.c`, `__rover`
`#453`, named by a mob's `brain` and started with `@wake`) acts on its own,
driven by the same comma-separated `behavior` prop as the reactive verbs:

- `wander`: on each `EV_TIMER` tick, step through a random exit of the current
  room (an exit is any resident object with a `dest` prop, as the `go` verb
  reads it), announcing the departure and arrival.
- `patrol`: on each tick, advance along the `route` prop (a comma-separated list
  of room ids), wrapping at the end; `patrol_idx` tracks the current stop.
  `patrol` takes precedence over `wander` when both are set.
- The tick period is the `dwell` prop in ms (default 5000). A downed or dead
  mob, or one in an active fight (`cb_active`), holds position.
- Reactive `greet`/`aggro` still fire: because a woken mob receives `EV_ENTER`
  in its mailbox instead of the host spawning `on_enter`, the agent handles it
  through the same `mob_react` (shared in `mob_behavior.h`) that `verb_mob.c`
  uses, so waking a mob does not cost it its OLC-4 reactivity.

New host primitive: `sys_getobj(obj, name)` (syscall 23) returns an objref
property's value as an int (or -1), the companion to `sys_getprop`, which only
returns strings. A wander agent needs it to read its own live `location`; it
also serves OLC-7 (a vehicle reading its position).

Deferred: idle chatter (a timer that only broadcasts a line) is a trivial
`behavior` token to add when a world wants it. Aggro opened by a woken agent
runs its `__combat` task under the system session rather than the intruder's,
so it is not reaped on that player's disconnect; it still ends on the normal
fight teardown. Revisit if that proves to leak.

Depends on: OLC-5 (the tick and the event mailbox), OLC-2 (spawned mobs need
somewhere to come from and return to).

### OLC-7: Vehicles (shipped)

Carriages, trains, and elevators are moving rooms: a room object (child of #100)
marked `vehicle`=1, whose own `location` is its current stop and whose riders
have `location` = the vehicle, so relocating the vehicle carries them with it.
`player_room` returns the location directly, so a vehicle simply is the room its
riders are in.

- `board <vehicle>` / `disembark` (`verb_vehicle.c`, `#454`/`#455`, setuid like
  `go`): board moves the player into a vehicle present at their stop; disembark
  moves them out to the vehicle's current stop.
- Route data model: `route` is a comma-separated list of stop room ids (the same
  shape patrol uses); `stop_idx` tracks the current stop; `dwell` is the tick
  period in ms for a timed vehicle.
- The vehicle agent (`agent_vehicle.c`, `__transit` `#456`, named by the
  vehicle's `brain` and started with `@wake`) advances the vehicle and announces
  each move to the riders and both platforms. A train is self-paced: `dwell` > 0
  makes each `EV_TIMER` advance one stop. An elevator is on-command: with no
  `dwell` it never ticks and moves only on a `floor <n>` request.
- `floor <n>` (`verb_vehicle.c`, `#457`, a plain `0755` verb) is the elevator
  control. It reads the vehicle the caller is aboard, then routes the requested
  stop to the vehicle's agent through `sys_notify`; the agent owns the move and
  rejects an out-of-range floor. The verb steers nothing itself, so a rider on a
  self-paced train that refuses the request cannot force it off route.
- `call` (`verb_vehicle.c`, `#458`, a plain `0755` verb) is the platform-side
  summon. A platform names its vehicle in a `line` objref prop; `call` reads it,
  finds the platform's index in the vehicle's `route`, and routes that stop to
  the agent through `sys_notify`, so the vehicle comes to the caller. An
  on-command elevator answers; a scheduled train refuses and stays on its route.
  There is no global "which vehicle serves this room" lookup (a vehicle's
  `location` is its current stop, so `sys_next` cannot enumerate vehicles), so
  the platform-to-vehicle link is the explicit `line` prop.

New host primitive: `sys_notify(obj, arg)` (syscall 26) delivers an `EV_USER`
event carrying `arg` to the agent listening on `obj`. A control verb routes its
request to the engine this way, explicitly, the same shape as combat's action
verbs waking `__combat`. This replaces an earlier catch-all that routed every
unrecognized command to whatever agent a room happened to run: that hijacked the
unknown-command stream (a genuinely unknown command aboard was swallowed) and
coupled command dispatch to room listeners. With `floor` an ordinary verb,
`verb_dispatch` handles it and an unrecognized command aboard is just unknown.

Deferred: mid-transit state (the model hops stop to stop with no in-between).
Not needed for working trains and lifts.

Depends on: OLC-5 (the agent/tick and the event mailbox), OLC-6 (`sys_getobj`,
which the agent uses to read its own position), OLC-1 (build the vehicle and its
stops), OLC-2 (a vehicle can be a reset-managed object).

## Cross-cutting notes

- Builder sandbox: the deferred world-branching idea in FUTURE.md would let
  builders work off a fork of the live world and merge when ready. It pairs
  naturally with OLC but is not a prerequisite.
- Ownership quotas: as builders gain `@create` and `@clone`, a per-account cap
  on object count would prevent id exhaustion. A host-side check, small, worth
  considering with OLC-1.
- Prototype discovery: `@create` needs the builder to know parent ids. A small
  `@proto list` (host or verb) that lists the base prototypes would help.
- Per-agent VM cost: every woken agent (OLC-5/6/7) is a full task. It holds a
  128 KiB VM (`VM_MEMSZ` 0x20000), one of the `MAX_TASK` (1024) task slots, and
  its own dwell timer. The `tasks` array is one `calloc`, so idle pages are not
  faulted in and a handful of agents costs almost nothing (60 agents is about
  8 MiB resident, which a Pi handles easily). The cost is linear in the number
  of awake entities, though, so waking hundreds of mobs or vehicles is real
  memory and real slots. A densely populated world should prefer one shared
  "world tick" agent that iterates the mobs on each tick (one VM, one timer)
  over waking each mob as its own agent. Reserve per-mob agents for a handful of
  distinctive actors. This is a guideline for builders, not a code change.
</content>
