# Completed Milestones

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
small converting wrapper) in the bridge.

## Milestone 71: MooScript host bridges, step two

The next four syscalls cross the bridge, which lets a MooScript verb do more than read and talk: it can
now write shared state under its own authority. `setpriv` (grant and release of the setuid elevation),
`now` (wall-clock seconds), `getobj` (an object-valued property read), and `rollup` (a cached sum of one
numeric field over a containment subtree) are added. `setpriv` and `now` take only ints or nothing, so a
verb binds their stubs directly; `getobj` and `rollup` take a string, so each gets a small wrapper that
converts the length-prefixed MooScript string to the C string the syscall wants, with `getobj` handing
back an object handle rather than a bare id.

Two verbs prove them. `heft` (id 484) reports carried weight through `rollup(player, "weight")`, a pure
read. `inscribe` (id 483) is setuid: it carves a timestamped, authored message into the current room and
reads it back. It resolves the room by objref (`getobj`), brackets the write to the shared room object
with `setpriv(1)`/`setpriv(0)`, and stamps the time with `now()`. The read-back showing the stored text
is the proof the elevated write landed, since a player does not own the room. This is the first MooScript
verb that writes shared state. The suite grew from 582 to 587 checks.

Still to come is a decision on the ChromeSix `cs_*` helper library, which gameplay verbs lean on and
which is still C-only. The agents and the combat turn-loop, built on the suspend and mailbox model, stay
in C until the runtime grows a way to express them.

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
