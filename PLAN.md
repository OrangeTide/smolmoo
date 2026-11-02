# Completed Milestones

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

## Milestone 59: MooScript character-creation wizard

`makechar` is a character-creation wizard written in MooScript (`verb_makechar.moo`), a front end to
the ChromeSix sheet that exercises the language and the object model rather than the C verb path. It
walks the two opening creation steps, attributes (four values, each 3 to 9, totalling 24) then the hook
(street, cyber, or awakened), validates each, writes them to the player's sheet, and hands off to
`chargen` for skills and the finalize (kit and derived stats). Creation state lives in the sheet's
`cg_step` property, the same one `chargen` uses, so the two verbs share one flow and interoperate step
for step. The parsing is pure MooScript: a `token` helper over `length`, `substr`, and `index`, with
`toint`/`tostr` for the numbers and property read/write for the sheet.

The feature needed one host-bridge addition and no skjegg change. MooScript has no int-to-object cast,
and smolmoo stores a player's `charid` as a bare integer, so `host_vm.c` gained `obj_of(id)`, a six-line
identity extern that hands the verb the sheet's object handle. The language itself proved sufficient:
property writes, `toint`/`substr`/`index`, arithmetic, and `extern func ... -> obj` all compile and run.
The remaining MooScript limits found during the work (verb calls are fire-and-forget with no return, prop
writes are string-only, there is no suspend/interactive model) are in smolmoo's own bridge, not skjegg,
and none blocked this wizard, which uses the re-entrant `cg_step` pattern instead of a suspend loop.
`verbs.conf` registers `makechar` as #480; the suite grew from 496 to 503 checks, all green.

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
