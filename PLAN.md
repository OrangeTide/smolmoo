# Completed Milestones

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

## Milestone 60: The Underdeck starter sub-area

The Underdeck is a maintenance sub-level reached by a service stair off the waystation concourse (#120),
adding playable content to the starter district. It was authored entirely in-game through the OLC builder
rather than hand-written into the seed: `build_underdeck.sh` drives a throwaway server with `@create`,
`@set`, `@move`, and `@dig` to build the area, then `smolmoo export` captures the result in `world.data`
seed format. This dogfoods the builder toolkit and proves OLC can construct a full zone with no gaps.

The sub-area is 18 objects: three rooms (The Underdeck, a Salvage Market, and a flooded Sump) wired by six
descriptive exits; a salvage dealer vendor stocking a melee pipe and scrap plating armor; a dockers
scrapper offering an ungated clear-out contract (140 creds, 2 CP, and a +1 dockers standing reward); a
tough squatter an area reset spawns into the sump; and a few ambient items. The test suite grew from 503
to 512 checks, a nine-check block that walks the whole chain from the wizard session: reach each room,
accept the contract, list the vendor stock, spawn and defeat the squatter, and turn in for the reward.

Authoring surfaced a seed-format trap. `world.data` has no comment syntax: the migrate parser reads any
line beginning with `#` as an object header, so a `# note` line parses as object `#0` (the id atoi-reads
as zero) and overwrites the System Object, wiping its `verb=#400` pointer. That made every verb resolve as
"Unknown command" while C builtins still worked. Provenance notes stay in the commit message and here, not
in the seed.

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
