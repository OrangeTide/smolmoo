# Completed Milestones

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

## Milestone 61: The Transit Spur job arc

The Transit Spur is a new zone off the waystation concourse (#120) carrying a three-step job arc, the
first multi-contract storyline in the starter world. The arc chains through the engine's existing flag
machinery: each contract's `turnin` grants a campaign flag (`job_grant`) that the next contract requires
(`job_need`), so later steps show as locked until the earlier one is cleared. It also exercises all three
contract shapes in one line: an opening bounty, a courier run (`job_dest`, completed by reaching the
room), and a final bounty.

The warden on the platform offers the ungated opener, clearing a feral cur from the signal house for 120
creds and the `spur_safe` flag. That unlocks the signaler's courier run in the maintenance shed, carrying
a relay core to the junction for 150 creds, a dockers standing step, and the `relay_live` flag. Powering
the relay draws out a saboteur at the junction, the final bounty the inspector pays 300 creds and 4 CP
to put down. The zone is 21 seed objects: four rooms wired by eight exits, three job givers, two
reset-spawned combat mobs, a flavor relay-core item, and the area with its two reset rules.

The zone was authored directly in the `world.data` seed rather than through the OLC build-and-export
roundtrip used for M60, since the seed is the maintained artifact, direct authoring keeps object ids
organized by type, and OLC's zone-building completeness was already proven. The suite grew from 512 to
526 checks, a fourteen-check block that walks the whole arc from the wizard session: reachability, the
courier run showing locked before its clearance, and accept, objective, and reward for each of the three
steps.

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
