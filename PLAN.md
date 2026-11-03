# Completed Milestones

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

## Milestone 72: the ChromeSix library reaches MooScript

The gameplay verbs lean on the 77-function ChromeSix helper library in `chromesix_verb.h`, which was
C-only: it is static C inlined into each C verb, and a MooScript verb cannot include a C header. The
chosen path is a bridge, not a port. A new `cs_moo.c` compiles the library once into the MooScript
runtime and exposes thin, non-static wrappers a verb binds with `extern`: `geti` (a property read with a
fallback), `pd`/`soak`/`maxbp`/`maxgrit` (the four derived stats, one accessor each, since MooScript has
no C struct for `cs_recalc`'s result), and `rest` (short-rest BP payout). Each wrapper converts the
MooScript ABI to the plain C the helpers want. Output helpers (`cs_out`) are not bridged; a MooScript
verb builds its lines with native strings.

`skj-cc` does no dead-code elimination, so `cs_moo.o` is all-or-nothing: it drags the whole library
(~56 KB) into any verb it links. Linking it into every MooScript verb tripled their size, so the
installer and the `@program` path now link it only when the source binds a `cs_moo` symbol
(`moo_src_uses_cs`). Verbs that do not use the library stay at their ~23 KB baseline; only a ChromeSix
verb pays the weight. One syscall the library references but the MooScript runtime lacked, `sys_post`
(24), was stubbed so the whole-header compile links.

The proof is a real port: `status` (and its `st` alias) now runs in MooScript (`verb_status.moo`),
replacing `verb_status.c`. It is the first MooScript verb to use the gameplay library. The existing M43a,
M43c, and short-rest tests already exercise it and still pass, so the port is faithful; the suite grew
from 587 to 588 checks, adding a Held-condition readout the earlier tests never produced.

Still deferred: the agents and the combat turn-loop, built on the suspend and mailbox model, stay in C
until the runtime grows a way to express them. A broader port of gameplay verbs (sheet, check) can now
follow the `status` pattern, each adding only the `cs_moo` wrappers it needs.

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
