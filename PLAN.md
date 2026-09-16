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

Depends on M18 (Task Scheduling) — now complete.

    sys_yield   — voluntary preemption, suspend VM and return to host
    sys_wait    — suspend until event (fd ready, timer, signal)

`sys_suspend` from M18 already provides basic yield/sleep. M20 extends
this with fd-based event waiting (verb fds, stdin) and the full
`sys_wait` multiplexer for long-lived processes.

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

## Milestone 20: VM Yield & Wait Hypercalls

`sys_open`/`sys_close`/`sys_read`/`sys_wait` hypercalls, persistent
verb handlers with fd-based event waiting.

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
`verb_testref.moo`) and the ported `look`. Deferred: object arguments beyond
`dobj`/`iobj`, non-string scalar arguments to a verb call, and a C verb path for
`@program` (MooScript only).

---

# Future Milestones

## Milestone 26: Threading

I/O thread (HTTP server, world save) + VM thread(s) (script
execution). Deferred until VM work stabilizes — the right
locking interface depends on VM design. Estimated ~100-150 lines:
pthreads, rwlock on objects, message ring between threads. Target
is Raspberry Pi multi-core.
