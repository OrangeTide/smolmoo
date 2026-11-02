# Completed Milestones

## Milestone 57: Re-vendor skjegg v0.7.0 and runaway watchdog (6af81fe, c56f9a4, 7780bd3, d26908d)

The verb toolchain and RV32 core were re-vendored from skjegg v0.5.0 to v0.7.0, which retired the RV32
stack calling convention: every verb now builds with the ILP32 psABI. `skj-cc-rv-psabi` merged into
`skj-cc-rv`, the RV backend split `rv_emit.c` into `rv_select.c` and `rv_mc_text.c`, and `update-sdk.sh`
was regenerated from the new `vendor.sh` with the local post-vendor steps re-applied (the
`SKJ_SKIP_M68K_RUNTIME` guard, the smolmoo `sdk/LICENSE`, the `skjegg.mk` SPDX tag). The assembler
immediate-range checks and the backend large frame-offset lowering smolmoo once carried as local patches
had been upstreamed by v0.7.0 and were dropped.

The ABI change reached three smolmoo-owned pieces. `moo_syscall_rv.S` was rewritten from the stack
convention to the register convention, matching the C-verb stubs. `__moo_verb_call` in `host_vm.c` was
rewritten to take fixed psABI parameters rather than the old `&typemask + 1` stack walk, since the RV
backend has no variadic-callee support. And smolmoo's local `moo/lower.c` typemask patch, which the
re-vendor wiped, was re-applied: it is what tells `host_vm.c` how to route object arguments to dobj and
iobj and a string argument to argstr, and its loss was the root cause of five MooScript marshalling
failures seen mid-migration. The emulator's `rv_run` now returns a reason enum plus a retired count;
`task_step` reaps a task on HALT or TRAP, since a trap no longer sets `halted` and the old halted-only
check would have respun a trapping verb every slice forever. The suite stayed at 496 checks, all green.

Slice b adds a runaway watchdog on the new reason contract. A verb that never yields held its task slot
and burned a scheduler slice every tick forever, since `vm_exec` caps only the initial synchronous slice.
`task_step` now sums the retired instruction count into `vm_task.insn_run` and reaps a task that runs
`VM_INSN_BUDGET` (5M) instructions without yielding, logging a `[vm] task N killed` line. The counter
resets on every yield (a finish or `sys_suspend`), so a long-lived agent that ticks and suspends is never
charged for its lifetime; only continuous execution counts, and the budget sits well above `VM_MAX_INSN`
so a heavy but bounded verb still finishes. Verified with a `while(1)` MooScript verb reaped at about 5M
instructions while the server kept running, and a clean full suite with no legitimate verb tripping the
budget. `FUTURE.md` records the remaining skjegg process-sandbox features (the memory-pool cap, guard
pages, the access-check probe, spawn handles, and the per-CPU decoder cache) as evaluated and set aside,
each redundant with or a poor fit for smolmoo's fixed-RAM, many-short-task model.

Slice c trims the unused ColdFire toolchain from the vendored SDK. The server never runs a ColdFire
guest and builds every verb with the RISC-V psABI toolchain, so `COMPONENTS` in `update-sdk.sh` drops to
`riscv moo cc as-rv ld-rv cpp emu-rv` and the re-vendor removes 14 files (the ColdFire backend, the `as/`
and `ld/` ColdFire front-ends and tools, the ColdFire emulator, and the m68k crt), a net cut of about
8,980 lines. The shared assembler and linker skeleton (`asm_lex`, `asm_obj`, `link`, `script`, `mapfile`)
stays, carried by the RISC-V tools; `skj-run` now compiles RV32-only. The `SKJ_SKIP_M68K_RUNTIME` guard,
which had wrapped the m68k runtime rules, now wraps the sole remaining GNU-cross rule (`start_rv.o`), which
smolmoo does not use. Suite still 496 green.

Slice d turns the `moo/lower.c` typemask edit into `patches/moo-lower-typemask.patch`, a git patch against
pristine upstream, that `update-sdk.sh` applies automatically after the vendor copy. A failed apply aborts
the vendor (`set -eu` plus an explicit `die`), so upstream moving under the patch is caught loudly rather
than silently dropping the typemask, the way slice a's hand-re-application could. This removes the one
post-vendor step that still needed manual source editing; the remaining steps are restores of
smolmoo-authored files and are done with `git checkout`.

## Milestone 58: Re-vendor skjegg v0.39.0

The verb toolchain and RV32 core were re-vendored from skjegg v0.7.0 to v0.39.0. Upstream had grown far
beyond the verb pipeline (x86-64 and AArch64 backends, a hardware-virtualization emulator, a JIT,
DWARF/addr2line, a hosted libc, a capability filesystem sandbox, a cross driver, and the Kobold-Pascal AOT
toolchain), and `vendor.sh` was rewritten around selection axes (`--arch`, `--frontend`, `--with`).
smolmoo stays on its two verb languages, C and MooScript, over the RISC-V RV32 backend. `update-sdk.sh`
was regenerated from the new `vendor.sh` with the local post-vendor steps folded back in: the restore of
the smolmoo-authored verb runtime, the restore of smolmoo's `sdk/LICENSE`, the `moo/lower.c` typemask
patch, and the copy of the embedded core.

Three upstream ABI changes in the embedded core reached smolmoo. The ECALL handler contract gained
`RV_ECALL_RETRY` (-1), which parks the hart and re-executes the ECALL; `vm_ecall` had returned -1 for an
unknown syscall to fall through to the trap, which under the new core would loop that ECALL forever, so it
now returns 1. The float register file widened to FLEN=64 (`uint64_t f[32]`) for the F and D extensions;
smolmoo never serializes CPU state nor reads `f[]`, so this only enlarges the task struct. The new `dp`
flag enables double precision, which the core computes in the host FPU and which upstream documents as not
bit-reproducible across machines; smolmoo clears it after `rv_init` so a double instruction traps rather
than threatening deterministic world replay. Verb math is integer-only.

The build integration was simplified. smolmoo's Makefile now depends on the five tool binaries it drives
(`skj-cc-rv`, `skj-mooc-rv`, `skj-as-rv`, `skj-ld-rv`, `skj-cpp`) rather than skjegg.mk's `skjegg`
aggregate. That keeps the build off the targets the new vendor emits but smolmoo never uses, the `skjcc`
cross driver and the GNU-cross-assembler runtime rules (`start_rv.o`, `va_rv.o`), so the generated
`skjegg.mk` is used verbatim with no post-vendor edit and the old `SKJ_SKIP_M68K_RUNTIME` guard is retired.
The assembler and linker now compile in `dwarf/dwarf.c`, and `skj-cc-rv` bakes in an include path for its
own `<stdarg.h>`; both are carried by the vendor automatically.

The `emu-rv` component was dropped. smolmoo embeds `rv32.c` as the verb engine and never builds `skj-run`,
so vendoring the emulator pulled in an unused `emu/` and `fs/` tree, a gdb stub, and a 9p server.
`COMPONENTS` is now `riscv moo cc as-rv ld-rv cpp`, and `update-sdk.sh` copies the two core files
(`rv32.c`, `rv32.h`) straight from the fetched tree in its post-vendor step. The typemask patch still
applies cleanly (a five-line offset). The suite stayed at 496 checks, all green, plus the rv32 and
chromesix C unit tests.

The hosted libc was evaluated and set aside. skjegg's `libc/` is a POSIX-style hosted library (malloc over
`sbrk`, stdio over `fdio`, dirent, stat, coroutine threads) whose I/O routes through
`open`/`read`/`write`/`lseek`/`sbrk` syscalls, built for `skj-run` or a Linux-like host. smolmoo's verbs
run under the world hypercall ABI instead (`sys_broadcast`, `sys_getprop`, `sys_spawn`, and the rest), so
adopting it would mean implementing a POSIX syscall backend the server deliberately does not carry,
against the minimize-source-size constraint. The 200-line `mulibc.h` is correctly sized and stays.

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
