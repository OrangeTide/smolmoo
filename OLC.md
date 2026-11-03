<!-- SPDX-License-Identifier: 0BSD OR CC0-1.0 -->
# Online Creation (OLC)

Builders log into the running game and create content: areas, rooms, exits,
items, NPCs and mobs, stores and vending machines, and vehicles such as
carriages, trains, and elevators.

Almost all of OLC runs as verbs on the RV32 VM rather than as host C in
`smolmoo.c`. This keeps the core server small (the project's central
constraint), lets builders revise the tools in-game through `@program`
without a server rebuild, and reuses the pattern the ChromeSix combat and
social systems use. The convenience builder commands are the exception:
they are host C because they are permission sensitive and act with the
caller's authority directly.

Planned extensions live in `ROADMAP.md`.

## Verbs and privilege

The verb syscall surface covers what OLC needs:

- `sys_create` (16), `sys_recycle` (17): object lifecycle
- `sys_setprop` (8), `sys_getprop` (7): property read and write
- `sys_getobj` (23): read an objref property's value (location, dest, ...)
- `sys_post` (24), `sys_taskid` (25): wake a task by id, read the current id
- `sys_notify` (26): deliver an `EV_USER` (with a string arg) to the agent
  listening on an object, so a control verb routes a request to its engine
- `sys_move` (13), `sys_next` (14): containment move and iteration
- `sys_objfind` (9): find an object by name
- `sys_spawn` (10), `sys_suspend` (11): timed and background tasks
- `sys_call` (18), `sys_hasverb` (19): verb-to-verb dispatch
- `sys_random` (12), `sys_rollup` (15), `sys_broadcast` (6)
- `sys_listen` (22), `sys_getmsg` (21): the event mailbox

Permissions are enforced host-side inside these calls, so a builder verb
cannot escalate. `sys_recycle` checks `is_wizard || obj_owner_match` before
freeing, `sys_setprop` checks `perm_can_write`, `sys_move` requires the
effective account to own the object, and `sys_create` stamps the caller's
account as the new object's `owner`.

Privilege is explicit bracketing, the setuid/seteuid pattern. A verb runs
with the caller's authority by default. `grant_accept()` raises the
effective account to the verb owner, but only if the verb carries the
setuid capability (the `04000` bit in its `elf` mode); `grant_release()`
drops back. Elevation also ends when the verb task exits, so a missed
release cannot leak past the verb. A verb elevates only to its own owner,
so setuid on a `#0`- or admin-owned verb is reachable only through the
wizard-only `@chown`. Verbs installed from `verbs.conf` that write shared
state carry `04755` and call `grant_accept()`; a verb written in-game with
`@program` is mode `0755`, so `grant_accept()` fails for it and a player
cannot escalate through a verb they own. A spawned task inherits the
spawner's privilege state. The VM tracks this per task (`vm.caller_acct`,
`vm.verb_owner`, `vm.can_elevate`, `vm.elevated`).

How far the elevation reaches is a per-verb choice. Where the privileged
span is small, a verb brackets only that span: `buy` (the stock move),
`board`/`disembark` (the relocate), and `get`/`put`/`drop` (the item move)
bracket the transfer and run the rest at the caller's authority. The combat
and social verbs run their whole body elevated, since they write room and
combatant state throughout.

## Builder commands

Host commands beside `@create`, `@set`, `@examine`, `@chmod`, `@chown`,
`@chgrp`, and `@group`:

- `@recycle #N`: destroy an object you own (a wizard may destroy any). It
  refuses an object that any other object still names as its `parent` or
  `location`, so a prototype or a non-empty room is rejected rather than
  orphaning children; empty it first.
- `@clone #N`: copy an object's properties into a new object owned by the
  cloner, preserving each property's permission flags.
- `@move #N to <dest>` (alias `@teleport`): relocate an object you own into
  a room or container.
- `@dig <exit> to <name|#N>`: create a room (or link an existing one) and a
  matching exit pair, the forward exit named as given and the return named
  `back`. Gated to the current room's owner or a wizard.
- `@go #N`: teleport your own avatar to a room (a wizard-only builder aid).
- `@find <name>`, `@contents [#N]`: locate objects by name, list a
  container's contents.

Object arguments accept `#N`, `&N`, or a name near the caller (`olc_ref`).
The `@` handler routes a verb named `@tag` through `verb_dispatch` after the
built-in tags, so a builder can add `@dig`-style commands as verbs; built-in
tags keep priority. These commands are documented in `help.md`.

## Resets

A living world is kept stocked by reset rules, an idempotent reconcile
rather than death-triggered repop. A reset rule is an object under the Reset
Prototype (`#910`, registered as `#0.reset`) with properties `room`,
`proto`, `count`, and optional `area`; an Area Prototype (`#900`, `#0.area`)
groups rules. `@reset` (wizard-only) walks every rule, counts the live
(non-downed) children of `proto` already in `room`, and clones the
shortfall, clearing `downed` and inheriting the proto's owner. It both
stocks a fresh room and repopulates a cleared one, and spawns nothing until
an instance dies. `@reset #<area>` filters to one area.

A host sweep reaps fallen bodies: it arms a `decay_tick` clock on a fresh
body, counts it down (the lootable window), and frees the body and its gear
at zero. Reaping is scoped to reset-spawned instances (a `reset_spawn`
marker), skips a room still in a fight, and leaves builder-placed NPCs
alone. Every reset spawn is cleaned (`spawn_clean` restores full BP and
clears the combat and death block) so an instance is never born downed or
wounded.

## Stores and vending machines

`verb_store.c` (bound to `buy` and `list`) drives stores from a data model
that needs no schema: a store is any object holding priced item objects
(`price > 0`) in its contents. A builder stocks it by moving priced items
in through the `@` commands or the web editor.

- `list [from <store>]` shows the stock at the standing-adjusted price.
- `buy <item> [from <store>]` sells one instance, moving the object to the
  buyer and drawing its price off their creds. With no `from` the store is
  the room's `vendor` NPC; a named `from` resolves an object in the room, so
  a vending machine is the same model without an NPC.
- Faction pricing (`cs_standing`) shifts the price ten percent per standing
  step from the store's `faction`, and Hostile or worse refuses the sale.
- A reset rule with the store as its `room` restocks it.
- The stim stays a sheet counter, not an object. A store sells it by setting
  a `stim` base price; only a store that opts in dispenses stims.

`buy` is setuid and elevates only for the stock move, then charges the
buyer's own creds unelevated; `list` is not setuid.

## Mobs

Mob behavior is data-driven by a `behavior` property on the mob (its own or
inherited), a comma-separated list of reactions.

Reactive behavior is player-triggered through the `on_enter` verb
(`verb_mob.c`). When a player enters a room, the host runs `on_enter` on
each resident object carrying a `behavior` prop, bound with `this` = the NPC
and `player` = the newcomer:

- `greet`: the NPC says a line to the room (its `greeting` prop or a
  default).
- `aggro`: the NPC opens a fight on the newcomer, seeding the `cb_*` state
  and spawning the `__combat` turn task. `on_enter` is setuid, so its writes
  to the room and both sheets elevate like the combat verbs.

Proactive behavior runs in a woken agent (`agent_mob.c`, `__rover`, named by
a mob's `brain` and started with `@wake`), driven by the same `behavior`
prop on each `EV_TIMER` tick:

- `wander`: step through a random exit of the current room.
- `patrol`: advance along the `route` prop (a list of room ids), wrapping at
  the end; `patrol` takes precedence over `wander`. `patrol_idx` tracks the
  stop.
- `chatter`: say a random line from the `chatter` prop (lines separated by
  `|`) to the current room. It composes with `wander` or `patrol`, so a
  moving mob can also mutter.
- The tick period is `dwell` ms (default 5000). A downed, dead, or fighting
  mob holds position.

A woken mob still reacts: it receives `EV_ENTER` in its mailbox and handles
it through the same `mob_react` (in `mob_behavior.h`) the reactive verb uses.

## Agents and the event loop

Autonomous behavior with no player present runs on a host-to-VM event
channel. Events are delivered cooperatively: the VM observes an event only
at a yield point (`sys_suspend`), so there is no re-entrancy; events queue in
the host and are handed over on the next block. Each handler task has a
small bounded FIFO mailbox (drop-oldest on overflow).

The runtime owns the loop. The CRT provides `_start` and calls `main()`. A
simple verb defines its own `main()`, runs, and returns (fire-and-forget).
An agent does not define `main()`; it links `-lverbmain`, whose `main()`
registers as its object's handler (`sys_listen`) and blocks for events,
draining the mailbox with `sys_getmsg` and calling `on_event` on each. When
the dwell elapses with an empty mailbox the loop synthesizes an `EV_TIMER`,
so an agent supplies just `on_event` and `verb_dwell`. The `agent_` source
prefix tells the installer and `program_compile` to link `verbmain.o`; repo
convention is `verb_*.c` for one-shot verbs, `agent_*.c` for agents.
`@program ... agent` authors an agent in-game.

Events are typed (`EV_USER`, `EV_TIMER`, `EV_ENTER`, `EV_WAKE`), kept in
sync host and VM like the syscall numbers. `EV_USER` is the string-verb
path, so a new player command is still just a new verb string. Two syscalls
back the mailbox: `sys_listen` (22) routes an object's events to the calling
task, and `sys_getmsg` (21) pops one.

`@wake #N` (wizard only) starts object `#N` as an agent under a reserved
system session the reaper never frees; `#N.brain` names the agent verb.
`@wake` marks `#N.awake=1` in the persistent world, and a boot-time scan
re-wakes every object so marked, so wandering mobs and running vehicles come
back on their own after a restart. `@sleep #N` stops an agent and clears the
flag. The scan also runs after `@rewind` reloads the world in place.

`__combat` is not a `verbmain` agent: it is a sequential, self-terminating,
per-fight task bound to the player's session. It blocks the turn on one
`sys_suspend`, and a combatant's action verb wakes it through `cs_end_turn`:
the turn task publishes its id in the room's `cb_task` prop (`sys_taskid`)
and the action verb wakes it by that id (`sys_post`). It does not
`sys_listen` on the room, so an unrecognized command typed mid-fight still
reports as unknown.

## Vehicles

Carriages, trains, and elevators are moving rooms: a room object (child of
`#100`) marked `vehicle=1`, whose own `location` is its current stop and
whose riders have `location` = the vehicle, so relocating the vehicle
carries them with it.

- `board <vehicle>` / `disembark` (`verb_vehicle.c`, setuid like `go`):
  board moves the player into a vehicle present at their stop; disembark
  moves them out to the vehicle's current stop.
- `route` is a comma-separated list of stop room ids; `stop_idx` tracks the
  current stop; `dwell` is the tick period for a timed vehicle.
- The vehicle agent (`agent_vehicle.c`, `__transit`, named by the vehicle's
  `brain`, started with `@wake`) advances the vehicle and announces each
  move. A train is self-paced (`dwell` > 0 advances one stop per tick); an
  elevator is on-command (no `dwell`, moves only on a `floor <n>` request).
- `floor <n>` (a plain `0755` verb) reads the vehicle the caller is aboard
  and routes the requested stop to its agent through `sys_notify`; the agent
  owns the move and rejects an out-of-range floor.
- `call` (a plain `0755` verb) is the platform-side summon. A platform names
  its vehicle in a `line` objref prop; `call` finds the platform's index in
  the vehicle's `route` and routes that stop to the agent. An on-command
  elevator answers; a scheduled train stays on its route.

`sys_notify` (26) delivers an `EV_USER` carrying its arg to the agent
listening on an object, so a control verb routes its request to the engine
explicitly, the same shape as combat's action verbs waking `__combat`.

## Notes

- **Ownership quota.** A per-account cap on object count prevents id
  exhaustion as `@create` and `@clone` open up. It is a host check tunable
  through `#0.objquota`.
- **Prototype discovery.** `@proto list` lists the prototypes registered on
  `#0`, so a builder can pick a parent for `@create` without reading source.
- **Per-agent VM cost.** Every woken agent is a full task: a 128 KiB VM
  (`VM_MEMSZ`), one of the `MAX_TASK` (1024) slots, and its own dwell timer.
  A handful of agents costs almost nothing (about 60 agents is 8 MiB
  resident), but the cost is linear in awake entities. A densely populated
  world should prefer one shared "world tick" agent that iterates the mobs
  each tick over waking each mob as its own agent, reserving per-mob agents
  for a few distinctive actors.
