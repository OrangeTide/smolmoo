# Completed Milestones

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
until the runtime grows a way to express them.

## Milestone 73: sheet and check ported to MooScript

Two more gameplay verbs follow the `status` pattern, each binding only the `cs_moo` wrappers it needs.
`check` (`verb_check.moo`) rolls a skill check: it parses `check <skill> [tn] [push N]` in MooScript and
crosses the bridge for the gameplay math, with new wrappers for the skill index, the push Grit spend, the
dice pool (attribute plus rank plus push less Crash), and the roll with its wild die (returned through a
stashed flag, since MooScript has no out-parameter). `sheet` (`verb_sheet.moo`) renders the full readout:
the name, attributes, derived defence, resources, carry load, and conditions are built with native
MooScript strings, while the three data-dense lines that carry their own vocabulary, the skills list and
the faction-standing and clearance lines, are returned prebuilt by the C library through string-returning
wrappers rather than rebuilt in MooScript. New wrappers added: `carry`, `decay`, `skill_idx`,
`grit_push`, `skill_pool`, `roll`/`wild`, `parse_push`, and the three composite line builders.

Both replace their C sources (`verb_check.c`, `verb_sheet.c`). The port needed no new tests: the existing
M25a, chargen-render, worn-armor, push, stealth, and zero-pool checks already exercise both verbs and all
still pass, so the suite holds at 588. With `status`, the three read-only ChromeSix display and resolution
verbs now run in MooScript; the writers (gear, store, jobs) and the suspend-based combat loop and agents
remain in C.

---

Planned and deferred work lives in `ROADMAP.md`; the federation and storage
directions have their full rationale in `FUTURE.md`.
