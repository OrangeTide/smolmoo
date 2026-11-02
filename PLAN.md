# Completed Milestones

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

## Milestone 62: The Greens job arc

The Greens is a hydroponics grow-dome off the waystation concourse (#120) carrying a second three-step job
arc. Where the Transit Spur chained purely on campaign flags, this arc is gated by faction standing it
earns itself, introducing a `growers` co-op faction and demonstrating the full gating spectrum in one
line: an ungated opener, a standing-gated middle step, and a flag-gated finale.

The co-op steward offers the ungated opener, clearing a blight-rat from the nursery for 110 creds and a
`growers` standing step. That standing opens the foreperson's courier run, a standing-gated (`job_min`
growers:1) haul to the cistern that pays more standing and grants the `greens_trusted` flag. The flag in
turn opens the overseer's finale, putting down a reaver in the cistern for 280 creds and 4 CP. The zone is
21 seed objects: four rooms wired by eight exits, three job givers, two reset-spawned combat mobs, a
flavor seed-crate, and the area with its two reset rules.

The standing gate confirms a mechanic the flag arc did not: it is a game-mechanic check read through the
giver's disposition, not a permission check, so the admin wizard does not mask it and must actually earn
the standing. The suite grew from 526 to 540 checks, a fourteen-check block that walks the whole arc from
the wizard session, including the standing-locked offer and the `accept` refusal below the gate before the
opener is cleared.

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
