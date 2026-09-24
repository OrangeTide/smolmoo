# Completed Milestones

## Milestone 1: Minimal Viable Implementation (738fad9)

HTTP+SSE MUD server, basic command dispatch, room navigation.

## Milestone 2: Output Buffering (bc9dcfa)

Output buffering with backpressure for SSE streams.

## Milestone 3: Object System (fd58b19)

Prototype-based object system with property inheritance, world.data
persistence, `@examine` / `@set` / `@create` commands.

## Milestone 4: Ephemeral Objects (28ae9c9)

Persistent (`#N`) vs ephemeral (`&N`) object categories. Player
connections use ephemeral IDs (`>= OBJ_EPH_BASE`). Rolling allocator
replaces `next_obj_id`. `OBJ_NONE` sentinel throughout.

## Milestone 5: Communication Commands (28ae9c9, 4afc5a8)

- `say` / `"` — room broadcast
- `emote` / `:` / `::` — third-person actions (possessive form via `::`)
- `whisper` — room-scoped private message
- `page` — global private message
- `@gripe` / `@typo` / `@bug` / `@idea` / `@suggest` / `@comment` — feedback logging

`whisper` and `page` share a `cmd_tell` helper parameterized by verb,
room scope, and not-found message. Prefix-based dispatch splits `"`,
`:`, `::`, `@` commands at the punct boundary.

Room Prototype (#100) carries `owner=#0` for feedback reporting.

## Milestone 6: Timer System (db83d03)

Priority queue (`pq.h`) plus slim timer layer in smolmoo.c. Main loop
`select()` now uses a computed timeout from the nearest timer deadline.
`timer_add` / `timer_remove` / `timer_dispatch` using `CLOCK_MONOTONIC`
nanosecond deadlines.

## Milestone 7: Fuel Gauge (fe85946, f58042b)

Per-player fuel system with command costs (say/emote/whisper = 1,
page = 2, look/help/quit/@ = 0). Players start with 10 fuel, regenerate
2 per 5-second tick. Per-player regen timers via priority queue —
only active when fuel is below max. Costly commands refused with
"You're too busy to do that!" when fuel is insufficient.

## Milestone 8: ColdFire VM Host Integration (02e1fab)

Memory bus, ELF loader (from CAS), hypercall handler (exit, write,
getprop). `verb_look.c` as first VM verb. `bootstrap.sh` cross-compiles
verb sources, hashes with BLAKE2b, populates depot/ CAS.

## Milestone 9: NLP Prototype — Verb Dispatch (bfd19d6)

`verb_dispatch()` as fallback from the hardcoded command table.
Splits input into verb + argstr. Searches for a matching verb ELF,
passes argstr to the VM via `vm_args` at address 0x380. `look` is
now a real verb — removed from the dispatch table, runs entirely
in the ColdFire VM.

## Milestone 10: Vendor smolvfs, Replace CAS Layer (e8aa595)

Vendored smolvfs into the tree. VFS with CAS backend replaces the
original depot/ CAS and `bootstrap.sh` hashing. Verb ELFs, world
snapshots, and other blobs go through the VFS. Hashes hardened
against length-extension attacks.

## Milestone 11: World Save (c1e9694)

`world_save()` serializes objects back to `world.data`. `@save` admin
command for on-demand saves. Periodic auto-save via timer system.
Atomic write: save to temp file, then `rename()` into place.

## Milestone 12: Error Values at the Boundary (c1e9694)

First-class error codes (`E_PROPNF`, `E_INVARG`, `E_PERM`, `E_TYPE`,
etc.) returned through the property type system instead of bare -1.

## Milestone 13: Object Display & Property Expansion (34976f5)

`obj_fmt()` — formats `&N` for ephemeral IDs, `#N` for persistent.
Property access syntax `#123.name`, parent chain walking.

## Milestone 14: Accounts & Login (7f32595, 20c13fc, 769e731)

Account objects (parent=#500) with BLAKE2b password hashing. Invite
code system (parent=#600): XXXX-XXXX-XXXX format, ~58.9 bits entropy,
per-code usage limits, 24h refresh timer. `--bootstrap` CLI flag
generates admin invite on first boot. Pre-login gate in `dispatch()`
restricts unauthenticated sessions to `connect`/`create`. Retro CRT
login screen with scanlines, flicker, scan beam, and ASCII banner.

## Milestone 15: Object Inheritance — Verb Lookup (26b9189)

Verb dispatch walks the parent chain of each search target
(player → location → dobj → iobj → `#0`). Verb objects matched by
`verb` property with parent-chain walking. Also included M22 browser
UI improvements (command history, scrollback cap).

## Milestone 16: Verb Argument Matching (da6ae18)

LambdaMOO-style verb argument patterns: `this none this`,
`any to any`, `this in front of any`, etc. Each verb declares its
dobj/prep/iobj spec via `args` property. Parser matches input against
specs to resolve which verb to call. Extracts `dobj`, `dobjstr`,
`prepstr`, `iobj`, `iobjstr` into `struct vm_args`.

## Milestone 17: Permissions (80c12eb, 626257f)

Owner/group/world permission model. Per-property `r`/`w` flags in
triplets (`rw,r,r` syntax), default `rw,r,r`. Execute permission
lives in the `elf` property value as Unix 12-bit octal mode:
`elf=[0755, b2:hash]`.

Group objects (parent #700) with `members` property. Commands:
`@chmod`, `@chown`, `@chgrp`, `@group create|add|remove|list`.
Permission check order: wizard → owner → group → world.

## Milestone 18: Task Scheduling

Heap-allocated VM tasks (up to 1024, ~128 MB). `vm_exec` refactored
to allocate a task, run one quantum, and leave long-running verbs in
the task queue for async continuation. Main loop steps READY tasks
each iteration. Orphaned tasks (disconnected session) reaped
automatically.

New hypercalls:
- `sys_spawn` (10): create task from ELF hash with optional delay
- `sys_suspend` (11): suspend current task (timed, yield, or indefinite)

`sys_fork` (full VM clone) deferred — `sys_spawn` covers fork+exec.

## Milestone 20: VM Yield & Wait Hypercalls

Depends on M18 (Task Scheduling). `sys_suspend` from M18 already provides
basic yield and sleep. M20 adds the fd-based event hypercalls, `sys_open`,
`sys_close`, `sys_read`, and the `sys_wait` multiplexer (fd ready, timer, or
signal), so a verb can run as a persistent, long-lived handler that waits on
events instead of running to completion.

## Milestone 23: Web Text Editor (26ad076)

In-browser property editor. `@edit #N.prop` opens an editor tab,
`@view #N.prop` opens read-only. HTTP endpoints: `GET/POST /prop`
for reading/writing property values, `GET /edit` and `/view` serve
the editor page. SSE `E` and `V` commands trigger tab opens.
Permission checks via M17 owner/group/world model.

## Milestone 22: Browser UI (88f382c, cb60ebc, 0382440)

All original targets complete: command history (up/down), ANSI-to-HTML
color rendering (inline styles, 256-color, truecolor), server-pushed
status bar (room, fuel, HP), scrollback cap. Additional: wiki view
with markdown rendering, live markdown highlighting in editor,
invite code paste-to-split across input boxes.

## Milestone 19: Remaining VM Hypercalls

`sys_setprop` (8) and `sys_objfind` (9) hypercalls. Typed slot system
replacing M16's none/any/this + preposition table: `<obj>`, `<text>`,
`<player>`, `<exit>`, `<atom=X|Y|Z>`, bareword sugar.

## Milestone 24: Server Database Commands

`dump` became `export`, taking optional object selectors (`N` and
`N-M` ranges); status output moved to stderr so object data can be
redirected cleanly. New `merge <file>` imports objects and renumbers
them into a fresh contiguous range above the highest existing id,
rewriting references among the merged set while leaving external
references intact. The `obj_deserialize` and `world_load_text` parsers
were unified into one buffer parser (`obj_load_buf`) with an optional
id-remap hook, which the merge path reuses. All subcommands now honor
`SMOLMOO_DEPOT`. README documents the command-line tools.

## Milestone 25: Combat System (ChromeSix)

A point-based OpenD6 variant (Mini Six: Bare Knuckle Edition) tuned for the
Liminal Frontiers setting, implemented as verbs on the VM in slices M25a
through M25j: character sheet and skill checks, core combat and the spawned
turn task, cover and NPC grades, range bands and movement, reactions,
multi-foe fights and frontage, surprise, cross-room movement and fleeing, the
Grit gauge with stims and push-a-roll, social conflict on the same engine,
faction standing and vendor pricing, body slots and inventory, room span,
death and recovery, and cached containment rollups. New host syscalls were
added only where a verb could not do the job: `sys_random`, `sys_move`,
`sys_next`, `sys_rollup`. Native TAP tests (`test/test_chromesix.c`) cover the
helpers. The rules live in `chromesix.md` and `chromesix-smolmoo.md`; the
balance decisions and deferred items are recorded in `history.md`.

## Milestone 27: RISC-V RV32 Verb Engine (replaces ColdFire)

Full cutover from the ColdFire (m68k) verb VM to the RV32 engine (`rv32.c`),
now that skjegg ships an in-tree RISC-V assembler and linker. RV32 is the sole
verb engine; `coldfire.c`, `coldfire.h`, `vm.ld`, and `test/test_coldfire.c`
were deleted and the GNU gcc verb-build fallback dropped. The verb runtime is a
single `sdk/runtime/verb_rt_rv.S` (ecall stubs plus the 64-bit integer helpers)
linked with each self-contained `.c` verb; unit tests moved to
`test/test_rv32.c`.

Two vendored-SDK bugs surfaced while bringing up `__combat` (a verb with a
2080-byte stack frame) and were fixed in-tree and upstreamed to skjegg:

- `sdk/backend/rv_emit.c`: stack frames larger than a 12-bit signed immediate
  emitted out-of-range `addi`/`lw`/`sw` for s0-relative access. Added
  large-offset lowering that materialises `base + offset` in `t6`.
- `sdk/as/rv_encode.c`: the immediate instructions and loads/stores silently
  truncated an out-of-range 12-bit immediate; they now `die` with a clear
  message instead of emitting a wrong instruction.

## Milestone 21: MooScript Verbs

World builders write verbs in MooScript, a statically typed LambdaMOO-inspired
language, and run them on the RV32 VM alongside the C verbs. The compiler was
already vendored in `sdk/moo` and already targets RV32 (`skj-mooc-rv`), so this
milestone was integration, not writing a compiler.

The pipeline (`skj-mooc-rv` -> `skj-as-rv` -> `skj-ld-rv`) mirrors the C verb
path. The one wrinkle is that the MooScript backend passes call arguments on
the stack (result in a0), not in the psABI registers the C verbs use, so the
MooScript verb runtime is compiled with `skj-cc-rv` and links stack-convention
syscall stubs (`sdk/runtime/moo_syscall_rv.S`) rather than the register stubs
in `verb_rt_rv.S`. The runtime is `sdk/runtime/moo_rt.c` (the `_start` entry, a
bump arena, and the map from the host `vm_args` block to the verb's `main`) plus
the host bridge `host_vm.c` and the `str.c`/`list.c` libraries. A verb's entry
is `verb main(player, room, this, dobj, iobj, arg)`; it declares only the
leading parameters it uses. `verb_look.c` was ported to `verb_look.moo` as the
first MooScript verb.

The host bridge (`host_vm.c`) was completed against the syscall ABI: properties
are typed from their text form (`#N` -> obj, digits -> int, else str), moves go
through `sys_move`, and contents walk with `sys_next`. Four host syscalls were
added where a verb genuinely needed one: `sys_create` (16) and `sys_recycle`
(17) for object lifecycle, `sys_call` (18) for verb-to-verb dispatch, and
`sys_hasverb` (19) for interface checks. `sys_call` resolves a verb on the
target and the global `#0.verb` prototype, checks execute permission, and runs
it fire-and-forget with `this` bound to the target.

Verb-call arguments are marshalled onto the classic MOO context by type: the
compiler tags each `obj:verb(...)` argument with a compile-time typemask (the
only change to the vendored compiler, in `sdk/moo/lower.c`), and the host routes
object arguments to `dobj`/`iobj` and the first string to `argstr`. Other types
are passed as strings via `tostr()`.

`@program #N` compiles an object's `src` property into its verb `elf` in the
running server, so verbs can be written and revised in-game through the web
editor without a rebuild. It shells out to the same toolchain, so a server that
hosts in-game programming ships the SDK and `vm_rv.ld` at runtime; a serve-only
deployment does not.

Each piece is covered by the HTTP smoke suite through small test verbs
(`verb_test_create.c`, `verb_test_call.c`, `verb_greet.c`, `verb_reflect.c`,
`verb_testref.moo`) and the ported `look`.

`@program` compiles either MooScript or C. The language is sniffed from the
source (a C verb has `#include`, MooScript never does) or named explicitly as
`@program #N c` / `@program #N moo`. The C path uses the register-psABI compiler
and links the C verb runtime; MooScript uses the stack-convention pipeline and
the MooScript runtime. The C-verb header travels with the SDK so a serve-only
deployment can compile C verbs without the repo tree.

Verb-to-verb calls convey up to two object arguments, routed to `dobj`/`iobj`,
and one string, routed to `argstr`. Passing more objects, non-string scalars, or
a second string would need a typed argument vector in `vm_args`, but the
host-reserved region below the guest code base (`0x0`-`0x3FF`) has no room for
one, so it would mean relocating the code base in the linker and rebuilding the
toolchain. That is deferred as an intentional limitation: the payoff is modest
against the project's size constraint, and `dobj`/`iobj`/`argstr` covers the
common cases.

---

## Milestone 26: Durable Async World Save

The original threading plan (an I/O thread plus VM threads with a shared object
lock) did not survive contact with the code. VM tasks already run in bounded
quanta on the single-threaded main loop, so parallelizing execution has little
payoff against real locking risk. Profiling the save path found the one
unbounded operation worth moving off the loop, but with a twist: `world_save`
is not a monolithic serialize. It is content-addressed and incremental, storing
only dirty objects into the CAS and rewriting a small root pointer, and it did
no `fsync` at all, so a crash could leave the root referencing world content
that never reached disk.

This milestone does two things. First, durability: `cas_put_object` and the
other CAS writers `fsync` the object data before the rename and `fsync` the
containing directory after it, and `root_write` does the same for the root
pointer. The CAS-side fsync originally shipped as a minimal local patch, then
landed properly when the vendored CAS layer was re-synced to upstream smolvfs
v0.4.1 (Milestone 28). The `root_write` fsync stays in `smolmoo.c` since the
root pointer is smolmoo's own file, not part of the CAS.

Second, the fsync cost stays off the main loop. `world_save` is split into
`save_snapshot`, which serializes each dirty object into a self-contained buffer
and clears its dirty flag on the main thread, and `save_flush`, which writes
those buffers into the CAS, updates the object map, and rewrites the root. A
single background writer thread runs `save_flush`, fed by a bounded queue of two
outstanding jobs; the main loop blocks only if both slots are full, which keeps
memory bounded without risking loss. During serve the object map and root have
exactly one accessor, the writer thread, so no object lock is needed. CLI
subcommands (migrate, merge, install) keep the synchronous path since no writer
thread runs. `@save` queues the save and waits for the writer to drain so its
confirmation stays truthful; shutdown queues a final snapshot, then drains and
joins the thread before freeing the world.

VM execution stays single-threaded. The smoke suite (202 checks) and unit tests
(33 checks) cover the save, autosave, `@save`, merge, install, and shutdown
paths.

---

## Milestone 28: Re-vendor smolvfs to Upstream v0.4.1

The vendored CAS layer had drifted from upstream smolvfs into a trimmed fork.
This re-syncs the four modules smolmoo uses (`cas`, `cas-omap`, `cas-pack`,
`cas-codec`) to upstream v0.4.1 and drops the local fsync patch, which upstream
now carries. The public API is unchanged, so `smolmoo.c` needed no edits, and
all 15 CAS entry points smolmoo calls kept their signatures.

The four modules are self-contained: BLAKE2b lives inside `cas.c`, the optional
logging subsystem compiles away when no callback is registered, and the base
codec has no external compression dependency. The larger upstream additions
(signing, a VFS layer, snapshots, topics, trees, the miniz codec) are left out
since smolmoo does not use them yet; they are the raw material for the ideas in
"Future Milestones" below. A `version.h` marker is vendored so the next re-sync
can tell what is present. Upstream ships these files under BSD-2-Clause-Patent
OR MIT; since the same author owns both projects, the vendored copies are
relicensed to the repo's own 0BSD OR CC0-1.0 to keep the tree uniform.

---

## Milestone 29: Versioned World History (a88cf38, 4ca03b6)

Each world save is now a link in a signed, verifiable chain of world roots, so
the world keeps a commit log: rewind to any prior save, audit what changed and
when, and recover after a bad edit.

The design uses `cas-sign` alone, not `cas-topic`/`cas-tree`. A version record
is a signed, self-addressed object that names a root, carries a sequence number,
and links to its predecessor; the chain is the audit trail. Walking and
verifying it needs only `cas_vchain_walk`, which takes a plain `struct cas`. The
topic and tree layers add a ref with an update log and crash rollback, which
matters for federation and mirroring but not for a single server keeping its own
history, so they were left out. smolmoo already had a durable atomic
pointer-write, so the chain head is kept the same way as the root. That held the
new vendored code to `cas-sign` plus its monocypher backend, about 850 lines,
rather than the 1946-line `cas-tree`. Federation stays available as future work
(see FUTURE.md).

The signing side vendored `cas-sign` and the monocypher backend, built with
`-DCAS_WITH_MONOCYPHER`, and generates or loads a server signing key at serve
start. The key is a 32-byte seed in a 0600 file outside the depot, since a
secret must never live in the served store. Each save, once its root is durable,
appends a signed record naming that root and advances a `depot/head` pointer.
Record creation runs on the save writer thread, so signing stays off the main
loop. The root pointer remains the load source of truth, so history is a
non-invasive overlay: a crash between the root write and the record write loses
only a history entry, never world content.

Two in-game commands expose the chain. `@history` lists it newest first (seq,
save time, root), marking the version whose root is currently live. The
wizard-only `@rewind <seq>` restores the world to that version's root. The
restore is first written as a new version record, so the chain only moves
forward and the rollback is itself auditable; then the live persistent world is
reloaded in place and every session is disconnected, since each player's
in-world avatar is ephemeral and belongs to the replaced state. Both commands
read the head from `depot/head` rather than the writer thread's in-memory state,
so they never race a save in flight. The smoke suite drives an isolated server
through build-two-versions, `@history`, and `@rewind`, and the signed chain
verifies end to end with `cas_vchain_walk` (seq drops by one, each prev links,
signatures check).

---

## Milestone 30: Depot Garbage Collection

The signed history keeps every save's root, and the content-addressed store
never overwrites, so each changed object and object-map page leaves its old
version behind. Left alone the depot grows without bound, which matters on the
Pi/SD target. `@gc` bounds it.

It is a mark-and-sweep keyed on the version chain. The mark phase retains the
newest `keep` version records (default 128) and the live root, and for each
retained root marks the root object, every object the map names
(`cas_omap_foreach`), and every directory page the map holds
(`cas_omap_foreach_page`, a small enumerator added to the vendored `cas-omap`).
The sweep walks the whole store with `cas_foreach` and removes any unmarked
object, but only of the four types whose reachability is fully enumerated here:
world objects, object-map pages and roots, and version records. Verb ELFs
(stored as `blob` and referenced by object properties, not by the map) and any
other type are never touched, so the collector never has to reason about
references it does not model. Pruning old history falls out of the same sweep,
since an unretained version record is simply left unmarked.

Correctness leans on two things. The mark set must be complete, so an allocation
failure while marking aborts the whole collection rather than risk deleting a
live object. And the sweep must not race a save, so the save writer is drained
first; with writes quiesced, an unmarked object of a swept type is provably
unreachable and removed regardless of age. The wizard-only `@gc [keep]` runs it
on demand and reports how many objects were kept and removed. The smoke suite
builds several versions, collects down to the newest, and confirms the depot
shrinks while the live world (including a MooScript verb, whose ELF survives)
still serves.

The one vendored change, `cas_omap_foreach_page`, is a small local addition
worth upstreaming.

---

## Milestone 31: Depot Integrity Check

`@fsck` is the integrity companion to M30's reclaim. It is read-only and runs
in three passes. The content pass walks every stored object with `cas_foreach`,
reopens it, and recomputes its address with `cas_hash_object`; anything that no
longer hashes to its own name is corruption or bit rot. The reachability pass
loads the live root into a scratch object map and confirms every object it
names, every directory page it holds, and every verb ELF its objects reference
(scanned as `b2:<hex>` in the serialized bytes) is present, catching dangling
references. The chain pass walks the signed history with `cas_vchain_walk`,
which verifies each record's signature and predecessor linkage as it goes; a
pruned tail reports as verified-with-older-records-gone rather than an error.

The wizard-only command reports counts of corrupt, unreadable, and missing
objects plus the chain status, and changes nothing. Verb ELFs and other
non-map object types are checked for content integrity in the first pass and for
presence in the second, so the check covers them without the collector's
type restrictions. The smoke suite runs it on a clean depot and again after
flipping a byte in a stored object, confirming the damage is detected.

---

## Milestone 32: Online Creation (OLC arc)

In-game world building, run as verbs on the VM rather than host commands, so a
running server can grow its world without a rebuild. The full design record and
per-milestone detail live in OLC.md; this is the summary. Two host prerequisites
came first. P1 routes `@`-prefixed commands through verb dispatch (4840007), so
builder commands are ordinary verbs. P2 adds privilege bracketing (78fd04b,
2a33216): a setuid verb elevates to its owner's authority for a bounded span via
`grant_accept`/`grant_release`, and `sys_move`/`sys_setprop` check that the
effective account owns the object, so a player-invoked verb can write shared
state without granting the player that power.

The arc then shipped in seven milestones. OLC-1 is the builder toolkit
(`@clone`, `@move`, `@dig`, `@find`, `@contents`, and the rest; f4e1a08).
OLC-2 is the on-demand reset system that keeps rooms stocked (8288311), later
extended to reap fallen bodies and spawn clean instances (0588ca1). OLC-3 is
generic stores and vending machines (fe1f2a2). OLC-4 is reactive mob behavior,
NPCs that greet or aggro when a player enters (ef0e5b3). OLC-5 is the event bus
and agent runtime: the CRT owns `_start` so agents define `main`, objects run as
persistent event-loop handlers under a reserved system session, and `@wake`/
`@sleep` plus a boot-time scan give agents that survive restart and `@rewind`
(8f87517, 61dbd74, 4010a85). OLC-6 is proactive mobs (wander and patrol on a
timer, `__rover`; 3d2db22). OLC-7 is vehicles: rooms that carry riders stop to
stop, as timed trains or on-command elevators, with `board`/`disembark`/`floor`/
`call` and the `__transit` agent (6c466db, ab8e139, 8e99b9f).

`__combat` was reworked onto the same event bus rather than converted to a
persistent agent: it stays a sequential per-fight task that blocks on
`sys_suspend` and is woken by task id through `sys_post`, and it does not
`sys_listen`, so combat never intercepts other commands (4a9bb22). The event
work added the mailbox syscalls `sys_post`, `sys_taskid`, and `sys_notify`, the
last routing an `EV_USER` request from a plain verb to an agent. A late pass
narrowed each verb's elevation to the privileged operation itself, leaving
validation and messages at caller authority (9cf2ed4). The HTTP smoke suite and
TAP unit tests cover the arc end to end (274 smoke checks pass).

---

## Milestone 33: Builder self-sufficiency (OLC-8)

Completes the OLC arc's promise: a builder runs a living, self-authored world
without touching the repo tree. The full plan is `M33.md`; this is the summary.
Three slices shipped.

M33a is in-game agent authoring (dcbd0a9). `@program #N agent` compiles a C
program that also links `verbmain.o`, the generic event-loop `main()`, so a
builder writes a mob or vehicle brain in the web editor and `@wake`s it with no
rebuild. It reuses the existing agent build path (the installer's `agent_`
prefix), exposed to `@program` through an explicit token.

M33b is a per-account object ownership quota (6ebe152). `obj_quota` reads
`#0.objquota` (default 256) so an operator tunes it live, and `acct_over_quota`
gates all three creation paths, `sys_create` (returning `-E_QUOTA`), `@create`,
and `@clone`, so open building cannot march the id space toward `OBJ_EPH_BASE`.
Wizards are exempt.

M33c is prototype discovery (393715e). `@proto list` walks `#0`'s properties and
prints each objref registration as `<key> #<id> <name>`, so a builder finds a
parent for `@create` without reading source. It is a host command, not a verb,
because a verb cannot enumerate `#0`'s property keys (there is no
`sys_nextprop`).

The one remaining slice, generic `@clone` of list-valued properties, is deferred
as optional: no world object uses list-valued props today. The smoke suite grew
to 282 checks, covering each slice, with the permission-sensitive quota and
listing checked as the non-admin `TestPlayer3`.

---

## Milestone 34: Vehicles in transit

Vehicles can take game time between stops instead of hopping instantly, and know
where they are while under way. The full plan is `M34.md`; this is the summary.
Slice 1 (timed transit) shipped (e12152e), and slice 2 (spatial awareness)
shipped in three parts (see below).

The event loop enabler is a per-cycle dwell: `verbmain` now re-reads
`verb_dwell()` before each block instead of once at startup, so an agent can vary
its tick with its state. This is backward compatible, a constant-returning agent
is unaffected.

A vehicle gains an optional `transit` property (travel time in ms). With a
positive value the agent (`agent_vehicle.c`) runs a two-phase cycle: it departs,
moves to the target stop with its doors shut (a `moving` flag set, `verb_dwell`
returning `transit`), and opens on the next tick, announcing arrival. While
`moving`, `board`/`disembark`/`floor`/`call` refuse ("doors are closed"), so a
rider is carried along and cannot get on or off mid-trip. With no `transit` the
trip is the old instant hop, so existing vehicles are unchanged. The suite grew
to 285 checks.

Slice 2 is spatial awareness: a vehicle knows where it is while under way, so
riders see what is outside and are seen in turn. It shipped in three parts
(14deef1, 9446672, 861afac). 2a adds an optional `path`, the full room-by-room
traversal (stops plus the pass-through rooms between): a train walks it one room
per tick and really occupies each, so a rider sees each pass-through go by and
anyone standing in it sees the train pass. 2b lets a pass-through room `observe`
rooms it can see into, so a rider glimpses a platform the train skips and its
people see it pass in the distance. 2c gives an on-command elevator with a `path`
a directional walk toward the requested stop, showing the rooms it passes instead
of hopping. The per-room move is factored into one `step_to` shared by the
train's looping advance and the elevator's targeted step. A vehicle with no
`path` is unchanged. The suite grew to 292 checks.

## Milestone 35: Character advancement

The ChromeSix growth loop that M25 designed but left inert now runs: play awards
Character Points and CP buys a better sheet. The full plan is `M35.md`; this is
the summary. It shipped in three slices (b098d44, 961d153, a8df506).

Slice a earns CP. The combat task (`verb_combat.c`) resolves both a fight and a
social scene, and it declares a win at one point, when no foe is left up. That
hook now grants the player CP, summed over the defeated foes (each foe's
`cp_award`, or a base of one), announced with `puts`. It fires once per settled
scene, so it cannot be farmed by re-opening one.

Slice b spends CP on skills. The `train <skill>` verb (`verb_train.c`, non-setuid
since it writes only the caller's own sheet) raises a skill one pip for CP equal
to its current rating in dice, capped at attribute + 6 skill points like
creation, refusing an unknown skill, a raise past the cap, and one the character
cannot afford.

Slice c spends CP on unlocks. The `learn <id>` verb (`verb_learn.c`) adds a
maneuver, cyberware mod, or spell from the new `cs_unlocks` catalog, which holds
each id's CP cost, required hook, and graft-slot use. It checks the id, prior
ownership, the hook, the graft-slot cap, and the cost. The catalog ships with the
`smartlink` maneuver and a new `dermal` cyberware that adds two Soak (read live
by `cs_recalc`), so a learned unlock has a real effect and the catalog grows one
row per future unlock. The suite grew to 302 checks.

## Milestone 36: The unlock roster

M35 shipped `learn` but left its catalog nearly empty and its effects hardcoded.
M36 fills the roster and finishes the machinery. The full plan is `M36.md`; this
is the summary. It shipped in three slices (c6fe839, abe1252, 4be0109).

Slice a makes the catalog visible: `learn` with no id lists each unlock's CP
cost, required hook, and status (owned, available, too dear, the wrong hook, or
full graft slots), closing the discoverability gap M35c left.

Slice b makes the use side table-driven. Each `cs_unlocks` row gained a Grit cost
and an effect tag, and `verb_use.c` now dispatches an active unlock through the
catalog (find, refuse if passive, check learned, gate to the turn, spend Grit,
switch on the tag) instead of a hardcoded branch. `smartlink` became the
`UEF_AIM` row and behaves unchanged. Adding an active unlock is now a row plus,
only for a new kind of effect, one case.

Slice c fills the roster. Beside `smartlink` (maneuver) and `dermal` (cyberware,
+2 Soak) it adds `reflex` (cyberware, +2 Passive Defense, filling the second
graft slot) and `vigor` (awakened spell, +3 Max Grit). Each new unlock is a
catalog row and one line in `cs_recalc`, no verb code. `vigor` newly exercises
the awakened hook `learn` gates on. The suite grew to 309 checks.

## Milestone 37: Jobs and contracts

The character systems could fight, talk, travel, and grow, but the world had no
reason to do any of it. M37 adds a directed loop: a job giver offers a contract,
completing it pays creds, CP, and standing. A job is data on a builder-authored
object, not compiled content. The full plan is `M37.md`; this is the summary. It
shipped in three slices (f882e8e, a6dca55, 57b68d9).

Slice a is the bookkeeping. A giver object marked `job=1` carries the contract in
props (`job_desc`, a goal, and the `job_cp`/`job_creds` reward). The player holds
one job at a time on the sheet (`job_giver`, `job_done`). `verb_jobs.c` (verbs
461-465, non-setuid since it writes only the caller's sheet) dispatches `jobs`
(offers here plus your active contract), `accept` (refused when you already hold
one), and `abandon`.

Slice b completes and pays a bounty. `mark_job_done` runs at the same scene-win
point as `award_cp`; if a defeated foe's name matches the giver's `job_target`,
it sets `job_done`. `turnin`/`report` at the giver checks the flag, pays `job_cp`
and `job_creds` into the sheet's `cp` and `money`, and clears the job. The three
refusals (no job, giver absent, not done) are covered.

Slice c adds the courier goal and the standing reward. A giver names a
destination room in `job_dest`; `verb_go.c` sets `job_done` on arrival, the
travel-side mirror of `mark_job_done`, so `turnin` is unchanged. `cs_standing_add`
is the verb-side writer that merges one `id:step` pair into the `standing` prop
clamped to the [-3, +3] band, and `turnin` parses the giver's optional
`job_standing` "faction:step" and pays it. The suite grew to 321 checks.

A follow-on chargen fix banks the documented 10 CP creation budget (chargen wrote
none, so a new character could not reach the M35-M37 growth loop until grinding
combat) and shows the CP balance on the sheet.

## Milestone 38: Standing with teeth

M37 lets a player earn faction standing, but the only consequence was vendor
pricing. `chromesix.md` Section 14 promises more, and M38 builds it: standing now
changes what the world lets a player do and how it treats them. The full plan is
`M38.md`; this is the summary. It shipped in three slices (2f380cf, 470ab1d,
470ccb2).

Slice a gates access. A job giver can carry an optional `job_min` "faction:step";
`accept` refuses a contract the caller does not rank for, and `jobs` marks a
gated offer locked with the band needed. The "faction:step" parse is factored
into a shared `job_fac_step` helper that the M37 standing reward now uses too.

Slice b adds hostility. `mob_react` (the shared reactive-NPC hook) now opens a
fight on entry when a mob with a `faction` meets a newcomer whose standing with
that faction is Hostile (-2) or worse, even without an `aggro` behavior token.
The vendor refusal at Hostile, already shipped, is the peaceful half of the same
rule. A mob with no faction is unchanged.

Slice c adds per-NPC disposition. `cs_disposition(npc, sheet, faction)` returns
the player's standing with a faction plus the NPC's optional `disp` offset,
clamped to the band, so a specific NPC can be warmer or colder than its faction.
The store, the aggro trigger, and the job gate all read through it. Because a
`disp` can be a negative grudge, which `cs_geti`/`cs_atoi` cannot read, the
helper parses the sign itself. The suite grew to 329 checks.

## Milestone 39: In-game time and recovery

M25i built recovery from death (the paid `recover`, a death clock, ally revive),
but a character who survived a fight hurt had no way to mend: the suite reset
`bp` by hand after every fight. M39 adds recovery for the living, on a new clock
the verbs had been missing. The full plan is `M39.md`; this is the summary. It
shipped in three slices (56e1ed3, 9ae2e79, 05cfca9).

Slice a adds the clock and the short rest. `sys_now` is host syscall 27, returning
wall-clock seconds so a stored stamp survives a restart. `cs_rest` recovers BP
lazily while a character sits in a `safe` room out of a fight: a `rest_since`
stamp and its `rest_room` anchor the clock, each read pays out the elapsed
whole-BP share toward `maxbp`, and the stamp advances by only what it paid, so a
remainder is not lost to frequent reads. The sheet applies it, and `go` banks it
before leaving a room. The period is `#0.rest_secs`.

Slice b adds wound treatment for the living. `verb_treat.c` (verb 466) scans the
room for a `clinic`=1 object, charges its `treat_fee`, and rolls the better of
Medicine or Cybertech against a TN that climbs with the wound, clearing one
`wounds` step on success. It is the living-character counterpart to M25i's
dead-only `recover`, and it gives those skills and creds a use.

Slice c pays off two clock-dependent items earlier milestones deferred. A job
giver's `job_cd` cooldown, stamped per giver on the sheet at `turnin`, blocks
re-accepting the same giver until it lapses (M37). `cs_decay` drifts every
faction's standing toward Neutral over idle `#0.decay_secs` intervals, applied on
a sheet read (M38). The Grit trickle on a rest is deferred as a small follow-on.
The suite grew to 342 checks.

## Milestone 40: Combat conditions

The engine tracked hit-point damage, wounds, the dying track, and the stim Crash,
but not the temporary conditions the combat rules lay out. M40 adds a condition
framework and the highest-value conditions, so a fight is more than trading
damage: a character can be knocked down, stunned, frightened, or set bleeding, and
must act around it. A condition is a prop on the sheet, in the idiom of `downed`
and `crash`, read where it bites: the turn grant for action-denial,
`cs_attack_resolve` for pool and Passive Defense shifts. The full plan is `M40.md`;
this is the summary. It shipped in three slices (6056e0c, ad3222b, 018dbd0).

Slice a builds the framework and Prone. `cs_cond_list` names the active conditions
and the sheet prints a `Conditions:` line. `cs_attack_resolve` folds the Prone
modifiers in from both sheets (a prone attacker at -1D; a prone defender +1D to
melee, -1D to ranged) and annotates the attack line. `trip` (verb 467) is the
inflictor, an opposed Might-plus-Brawl roll that knocks an engaged foe prone;
`stand` (verb 468) clears it, spending the turn in a fight.

Slice b adds Stunned and Shaken. `stun` (verb 469) spends a Grit on an opposed
Might-plus-Brawl blow that leaves a foe Stunned; the turn loop skips a stunned
combatant's turn and clears the flag, mirroring the flat-footed skip. `menace`
(verb 470) is a Charm-plus-Command intimidation that leaves a foe Shaken (-1D
until it rallies); `cs_rally` attempts a Wit save against 10 at the end of a
shaken combatant's turn to shake it off.

Slice c adds Ongoing damage and closes an M39 leftover. `cs_cond_tick`, called at
the turn grant, applies a sheet's `bleed` value as direct, un-soaked damage each
turn and downs a combatant that bleeds out. `rend` (verb 471) is the inflictor, a
Grit-fuelled opposed Might-plus-Brawl tear; `staunch` (verb 472) ends your own
bleed. `cs_rest` now clears the `crash` stack when a rest fills BP to maximum, so
a full short rest lifts the stim penalty (the deferred M39 item). Blinded,
Suppressed, Held, and a persistent Exposed follow the same framework as later
follow-ons. The suite grew to 360 checks.

## Milestone 41: The active roster

M36 built a table-driven unlock catalog so a maneuver could be a row, not a
branch, but only four unlocks filled it, leaving `learn` and the CP economy with
almost nothing to buy. M41 stocks the shelf, adding a representative unlock for
each active-effect family so every kind of option is proven end to end. It leans
on two seams: the `cs_unlocks` catalog and its `use` dispatch (M36), and the
condition framework (M40). The full plan is `M41.md`; this is the summary. It
shipped in three slices (5c1a808, 951f998, 1541db4).

Slice a adds timed self-buffs. A buff is one prop carrying rounds remaining, so
its magnitude is computed at the read site: `cs_recalc` grants the bonus while it
is live, `cs_cond_tick` counts it down at the turn grant, and `verb_use` sets the
duration on activation. It ships `shield` (Mana Shield, +3 Passive Defense) and
`mesh` (Dermal Wire Mesh, +Wit dice Soak).

Slice b adds offensive unlocks and lands the M40-deferred Suppressed condition.
`use` grew a target for an offensive unlock, so both dispatch through the catalog.
Suppressed is a `suppress` prop, folded into `cs_attack_resolve` as a -1D on the
attacker's next attack and then spent. `suppress` (Suppressive Fire) sets it on
every standing foe; `shock` (Static Shock) reuses `cs_attack_resolve` for a 4D
Wit-plus-Spellcasting attack via a world-seed focus object, and a new `out_edge`
out-parameter reports a Tactical Edge on the hit, which shock reads to also Stun.

Slice c adds the aid action. `inject` (Biomedical Injector) rolls Wit plus
Medicine against Moderate and heals the caller or a named ally in reach by the
margin, capped at the caller's Wit dice. It is the first ally-targeted effect, so
a later ally-facing unlock is a row, not new plumbing. The reaction-family
unlocks and the remaining catalog rows are deferred as later follow-ons. The
suite grew to 375 checks.

## Milestone 42: Reactions

M41 shipped the active roster but deferred the reaction unlocks, which fire out of
turn when a hit lands rather than on your own turn. M42 builds that one shared
mechanism, a defender-reaction check inside `cs_attack_resolve`, and lands the
four reaction unlocks, largely closing out the ChromeSix combat engine. Each
reaction is a `UEF_PASSIVE` catalog row read by id at the resolution point it
bites, gated on a spare `react_left` and its cost, and firing only when reactions
are allowed so a reaction's own strike cannot recurse. The full plan is `M42.md`;
this is the summary. It shipped in three slices (a8e0a45, 8333f08, ea0ed39).

Slice a builds the hook at the Net Damage point and ships Reflex Governor
(cyberware): the defender spends a reaction and 2 Grit to roll Wit plus Cybertech
against the attack total, cutting the hit by its Wit dice on a success. This
proves the read-a-reaction-unlock-and-adjust-the-hit seam the others reuse.

Slice b adds two hooks at the other points. Riposte (any hook) hooks the miss
path: on a miss by 4 or more, the defender spends a reaction and 1 Grit and
`cs_attack_resolve` recurses with reactions off to strike the attacker. Emergency
Defibrillator (cyberware) hooks the lethal drop: a hit that would take the
defender to 0 BP leaves them at 1 and sets a burnout flag, firing once until a
repair clears it.

Slice c adds the first third-party reaction and rounds out the catalog.
`cs_attack_resolve` gained a `room` parameter, threaded through its callers, so
Empathic Aegis (Awakened) can scan the room for a bystanding ally that spends a
reaction and 1 Grit to add its Charm dice to the defender's Soak. Kinetic
Absorbers (cyberware) is a passive read in `cs_recalc`, +1 Passive Defense for -1
Max Grit. The initiative and marking unlocks (Wired Reflexes, Tactical
Co-Processor, Threat-Assessment Optics) stay deferred as on-your-turn rows, as do
the M40 conditions Blinded, Held, and Exposed. The suite grew to 382 checks.

## Milestone 43: Player-facing polish

After eight milestones that deepened the ChromeSix systems, M43 adds no mechanics.
It makes the existing depth approachable from inside the game: a glanceable status
line, in-game help for the game commands, and a guided first few minutes for a new
character. It is deliberately the lowest-code milestone of the run, wiring
renderers and seeding content rather than building systems. The full plan is
`M43.md`; this is the summary. It shipped in three slices (5ee9adb, f4133ef,
832e40a).

Slice a adds a `status` verb (alias `st`) that draws the Section 12 vitals line,
the HP and Grit bars over the same `cs_meter` the combat prompt uses, a wound
ladder, and any active conditions, a glance without the full `sheet`.

Slice b seeds the ChromeSix verbs as help topics on `#0.help`, grouped into six
area topics (character, combat, maneuvers, gear, jobs, social) with a `commands`
index, so a player can look them up in-game rather than only in a markdown file.
`cmd_help` already serves any property by name, so this is content only.

Slice c guides a new character. `chargen` sets a `made` marker on completion (the
character prototype supplies default stats, so `made` is the reliable set-up
signal), a `login_welcome` helper points an unmade character at `chargen` and
`help start`, `status` guides an unmade character rather than showing a default
readout, and a `start` topic lays out the first steps. A scripted tutorial and a
persistent per-line prompt stay deferred. The suite grew to 393 checks.

With combat substantially complete and its depth now approachable, the near-term
ChromeSix leftovers are content and the trivial catalog rows; a playable starter
district or a campaign loop are the larger next directions.

## Milestone 44: A playable starter district

After the combat engine and its polish, the systems had nowhere to happen: jobs,
standing, the clinic, the vendor, and combat encounters were exercised only by
test setup, not by any world a player could walk into. M44 seeds one small
district that ties chargen, movement, jobs, standing, combat, and the services
into a loop a new character can actually play. It is mostly world content, which
is data rather than source, so it validates the engine as a game without growing
the codebase, and it shakes out integration the isolated tests never touch since
those build their own objects while this drives the seeded ones. The full plan is
`M44.md`; this is the summary. It shipped in three slices (93138a2, 6b9475f,
6e07d35).

Slice a seeds the hub and its services: a five-room waystation (concourse, bar,
med bay, supply, bunks) in a reserved id block, with a vendor stocking priced
gear, an autodoc clinic, and a safe bunk for a short rest, all wired to the lobby
with two-way exits. A smoke test walks in from the lobby and uses each service
against the fixed seed ids. Two integration notes: a world-seed line beginning
with `#` is parsed as an object header, so the seed carries no comments; and
movement is blocked when carry load exceeds the Might-derived capacity.

Slice b seeds the first playable contract loop. A fixer in the bar carries a real
bounty against a scavenger hand-placed in a new cargo bay, rewarding creds, CP,
and a dockers standing step. The bounty completes on the scene win and pays out at
turn-in, the M37 loop now from the seed. The foe is hand-placed rather than
stocked by a `#910` reset rule, because a seeded rule shifts the global `@reset`
counts the OLC tests assert exactly; reset-based respawn is deferred to keep those
tests green.

Slice c gives the district a faction that reacts to who the player is. A dockers
office off the concourse holds a handler whose courier contract is gated behind
dockers standing (`job_min`), so a fresh character is refused until the slice-b
bounty earns the band, and a guard that greets a newcomer, aggros a
dockers-Hostile one through `cs_disposition` with no aggro token, and roves its
route when woken as an agent (`brain`). The guard is left unwoken in the seed,
reactive through `on_enter`, so the district does not rove on its own until a
builder wakes it. Further districts, a branching job tree, and a campaign arc stay
deferred. The suite grew to 413 checks.

## Milestone 45: Close out the combat catalog

The M25c-M42 arc built the combat engine and its reaction family but left three
Section 11 catalog rows and three conditions as explicit follow-ons. M45 lands them,
so every family the rules lay out is proven end to end and `learn` and `use` have
the full shelf. No new mechanism: each item is a catalog row or a condition prop
hung on a seam that already exists (the `cs_unlocks` catalog and `use` dispatch, the
M40 condition framework, the `cs_attack_resolve` hooks, and the opposed-roll
inflictor pattern from `trip` and `stun`). The full plan is `M45.md`; this is the
summary. It shipped in three slices (55556d9, 3a36058, 3747a43).

Slice a adds the first unlock that touches turn order rather than a hit. Wired
Reflexes is a cyberware passive: a catalog row plus a `cs_has_unlock` read in the
initiative loop of `verb_combat.c`, adding a flat bonus to the rolled initiative
before the sort so its holder acts earlier. The bonus is a flat post-roll addition,
which keeps it deterministic to test.

Slice b adds two on-your-turn cyberware unlocks that apply a tracked condition to a
target, and lands the M40-deferred persistent Exposed as a real state. Tactical
Co-Processor sets `exposed` (a tracked -2 Passive Defense read in
`cs_attack_resolve`), and Threat-Assessment Optics sets `marked_by` to the marker's
sheet so `cs_attack_resolve` adds a die when the attacker is the marker. Both
dispatch through the catalog on the M41b offensive-target pattern and join
`cs_cond_list`. The transient ambush Exposed is left as is, and does not double
count since ambush does not set the prop.

Slice c lands the last two M40 conditions. Blinded (`blind`) docks the blinded
attacker 3D and wears off through `cs_cond_tick`; Held (`held`) docks the held
attacker 1D and blocks band changes and fleeing until `break` clears it. Two
inflictors join `verb_menace.c`: `grapple` (melee Might+Brawl) sets Held, and
`flash` (Agility+Firearms at any range) sets Blinded, both spending a Grit. The
initiative and marking unlocks and these conditions were the M40-M42 deferrals, so
the combat catalog is now substantially whole; the remaining Section 11 rows add no
new family and stay as content. The suite grew to 423 checks.

A note the milestone surfaced: the M29 `@rewind` tests copy the main depot and
generate their own signing key, so they can only rewind to roots they signed. If
total suite runtime crosses the five-minute `AUTOSAVE_MS`, the main server autosaves
one root under its own key, and the copied root then fails the isolated rewind. The
suite runs well under that in a normal pass, but its growth narrows the margin, so a
later milestone should make the isolated instances hermetic (start at their own
genesis).

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
