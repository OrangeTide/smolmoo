# Completed Milestones

## Milestone 69: Automap on room entry

The last step of the map series makes it opt-in automatic. `map on` and `map off` toggle a per-player
`automap` flag, and when it is set the `go` verb draws the destination map on arrival, right after the
room description. The flag lives on the player object and defaults off, so nothing changes for a player
who does not ask for it.

The `go` verb fires the map through `sys_call`, which runs the map verb as its own task. That task is
handed the room the player just left, so the map verb was changed to read the room from the player's
current location rather than its call context; it now always draws where the player actually is, which is
correct for a typed `map` too. This is the one wrinkle the trigger needed. The suite grew from 577 to 580
checks: the flag toggles on, a real `go` through an exit (not the `@go` teleport, which does not run the
verb) draws the room just entered, and the flag toggles back off.

With the map series complete, the deferred pieces remain: authored per-room ASCII art with a floor mask,
and the global map that reflects room sizes. The `mapw`/`maph` and `dir` data authored here are the
groundwork for that world map.

## Milestone 70: MooScript host bridges, step one

The first step toward MooScript as a first-class verb language, alongside C. A MooScript verb reaches the
host only through the bridge in `sdk/runtime/host_vm.c`, which exposed property access, object moves, and
verb calls but none of the gameplay syscalls. Two of the most-used are added here: `broadcast` (room
output) and `random` (dice). `random` binds straight to the `sys_random` stub the runtime already
carries; `broadcast` needs a small wrapper (`host_broadcast`) because MooScript hands strings as a
length-prefixed value and `sys_broadcast` wants a C string. A verb binds them with the alias form, for
example `extern host_broadcast as func broadcast(room: obj, msg: str) -> int`.

The proof is a real verb: `roll`, written in MooScript (`verb_roll.moo`), rolls a die and announces it to
the room. `roll` alone is a d6, `roll <sides>` a d<sides>, clamped to a sane range, the result broadcast
to everyone in the room including the roller. It is the first MooScript verb that is a gameplay command
rather than a wizard flow, and it needs no C. The suite grew from 580 to 582 checks, rolling a d20 and a
default d6 and reading the announcement out of the roller's own stream.

This sets the pattern for the rest: a syscall becomes available to MooScript by binding its stub (or a
small converting wrapper) in the bridge. Still to come are `setpriv` for setuid writes, the `rollup`,
`now`, and `getobj` reads, and a decision on the ChromeSix `cs_*` helper library, which gameplay verbs
lean on and which is still C-only. The agents and the combat turn-loop, built on the suspend and mailbox
model, stay in C until the runtime grows a way to express them.

---

# Future Milestones

A broader menu of directions the CAS and signing foundation opens up (SHOAL,
peering, backup followers, branching, asset storage) lives in FUTURE.md.

The federation stack (SHOAL, multi-server peering, and the backup follower) is
deferred to a distant future by decision. It needs the unvendored have/want
sync protocol plus the `cas-topic` and `cas-tree` ref layers, and amounts to a
new networked subsystem. The local half already shipped as versioned history
(M29-M31), which covers what a single-server world needs. Federation is gated
by server maturity: it is not worth exploring until a server carries hundreds
of active players, since that growth will decide the design. Packfile
compaction is likewise
deferred. Both are documented with full rationale in FUTURE.md; revisit either
when a concrete use case justifies the code.
