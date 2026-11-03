# Completed Milestones

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

## Milestone 63: The Warrens combat area

The Warrens is a combat area reached through a breach in the Underdeck sump (#129), the first zone built
for free-fighting rather than contracts. There are no job givers; the draw is Character Points, which the
combat engine already awards for any win (`award_cp`, each foe worth its `cp_award`, default one). The
zone proves out multi-mob spawn points and a mob grade ladder as pure seed content on the existing reset
and combat verbs, with no core-server change.

Three spawn points populate the zone on the area's autoreset: the Gnaw keeps a pack of three `vermin`
(mook grade, downed by any hit, one CP each), the Flooded Run a `lurker` (tough, two CP), and the Brood
Nest the `broodmother` (tough, mig 9, four CP), the per-mob `cp_award` tuning the reward to the grade. A
reset rule keeps up to `count` live clones of its proto in its room and skips any room with a fight in
progress, so `count=3` is what makes the Gnaw a pack. The zone is 19 seed objects: four rooms wired by
eight exits, three mob prototypes, the area, and its three reset rules.

The suite grew from 540 to 546 checks. The block reaches the zone from the sump, fires the spawn points
and confirms each room populated its mob by name through `@contents`, then clears the pack and the lurker
and asserts the player's CP rose, proving kills pay out. This is also the first content added under the
standing preference to build new behavior as a sandbox verb before the core server: it needed neither,
being content over mechanics the engine already has.

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
