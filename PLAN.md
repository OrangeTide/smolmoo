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

---

# Future Milestones

## Milestone 21: MooScript Compiler (separate tool)

Statically typed MooScript variant targeting RISC-V RV32. ~5-6 kLoC,
built as a separate tool (not counted in smolmoo core). Can run as
a library linked into smolmoo for `@program`, as a standalone
compiler, or self-hosted inside the VM. The skjegg `skj-mooc-rv`
compiler is the vendored starting point.

The compiler is optional — any language with an RV32 backend works.
MooScript is the default for world builders.

Syntax follows LambdaMoo conventions (see `sdk/moo/lambdamoo-syntax.md`):
property access via `obj.prop`, verb calls via `obj:verb(args)`,
`$name` shorthand for `#0.name`.

## Milestone 25: Combat System

Trivial subset of OpenD6 RPG System, following Mini Six: Bare Knuckle
Edition. Character stats, skill checks (dice pools), basic combat loop.
BFRPG is the backup if OpenD6 is too complex. Likely
implemented as scripts on top of the VM.

## Milestone 27: RISC-V RV32 Verb Engine (replaces ColdFire)

Full replacement of the ColdFire verb VM with the RV32 engine (`rv32.c`),
now that skjegg v0.5.0 ships an in-tree RISC-V assembler and linker
(`skj-as-rv`, `skj-ld-rv`). Single-milestone cutover: RV32 becomes the sole
verb engine and ColdFire is removed from the server.

- Verbs compile with `skj-cc-rv-psabi` -> `skj-as-rv` -> `skj-ld-rv`
  (standard RISC-V ILP32: args in a0-a7, a7 = syscall number, a0 = return).
  The GNU gcc fallback is dropped; the in-tree toolchain is the only path.
- New smolmoo-local RV verb runtime: `start_rv_vm.S` (entry + MooScript
  bump arena + 64-bit backend helpers), `hypercall_rv.S` (ecall stubs),
  `vm_rv.ld` (little-endian, `elf32-littleriscv`, code at 0x400).
  `host_vm.c`, `str.c`, `list.c`, `mulibc_vm.h` recompile unchanged.
- Server (`smolmoo.c`): swap `coldfire.h`/`cf_cpu` for `rv32.h`/`rv_cpu`;
  flip the six memory-bus callbacks and the ELF parse helpers to
  little-endian; ELF checks become ELFDATA2LSB + EM_RISCV (243); entry is
  passed to `rv_reset(cpu, entry)` instead of ColdFire memory reset vectors;
  the LINE_A opword dispatcher becomes an `rv_set_ecall` handler reading a7
  for the number and a0-a5 for args. The 14 syscall bodies are unchanged
  apart from the register accessors.
- `Makefile`: server links `rv32.c`; the `sdk` target builds the RV runtime
  objects; `test_coldfire` becomes `test_rv32`.
- ColdFire removed from the server: delete top-level `coldfire.c`/`.h` and
  `vm.ld`. The ColdFire SDK components stay vendored for now; trimming them
  from the `update-sdk.sh` component set is a separate cleanup.

Implemented. The verb runtime landed as a single `sdk/runtime/verb_rt_rv.S`
(ecall stubs plus the 64-bit integer helpers) linked with each self-contained
`.c` verb; verbs supply their own `_start` and include `mulibc.h`. Unit tests
moved to `test/test_rv32.c`. `coldfire.c`, `coldfire.h`, `vm.ld`, and
`test/test_coldfire.c` are deleted.

Two vendored-SDK bugs surfaced while bringing up `__combat` (a verb with a
2080-byte stack frame) and were fixed in-tree, to be upstreamed to skjegg:

- `sdk/backend/rv_emit.c`: a stack frame larger than a 12-bit signed
  immediate (2047 bytes) made the prologue/epilogue and every s0-relative
  local access emit out-of-range `addi`/`lw`/`sw`. Added large-offset
  lowering (`emit_mem`, `emit_addbig`) that materialises `base + offset` in
  `t6` (never allocated, never otherwise used) when the offset is out of
  range, applied at the prologue frame adjust, the callee-save saves and
  reloads, spill and i64-spill access, slot load/store, address-of-local,
  and the continuation save.
- `sdk/as/rv_encode.c`: `addi`/`slti`/.../`andi`, the loads, and the stores
  silently truncated an out-of-range 12-bit immediate. They now `die` with a
  clear message instead of emitting a wrong instruction.

All three fixes were upstreamed to skjegg (branch off v0.5.0), verified against
its own RISC-V test suite (`make check-rv-emu`, 16/16), and delivered as a
`git format-patch` series for submission.

## Milestone 26: Threading

I/O thread (HTTP server, world save) + VM thread(s) (script
execution). Deferred until VM work stabilizes — the right
locking interface depends on VM design. Estimated ~100-150 lines:
pthreads, rwlock on objects, message ring between threads. Target
is Raspberry Pi multi-core.
