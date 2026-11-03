# Completed Milestones

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

## Milestone 64: Item catalogue and consumable buffs

This fills out the item world with a wide spread of gear on the verbs the engine already carries, plus one
new consumable mechanic. The seed grows by 41 objects (ids #329-#369): weapons across every combat skill
and range band (brawl knuckles, melee blades and a reach spear, firearms from a taser to a long rifle, a
heavy autocannon, a spellcasting focus), torso and head armour spanning soak 1 to 4, worn and floor
containers of varied capacity, a set of craft materials and pure junk for loot and encumbrance, and six
buff consumables. Weapons, armour, containers, and junk are pure content on the existing wield, wear,
get, put, and buy verbs, so they needed no code.

The one gap was a consumable that applies an effect, which the core had only as the counter-based stim and
learned maneuvers. That went into `verb_use.c`, a sandbox C verb, not the core server, honoring the
verb-over-core preference. A buff item carries a `buff` tag and an `amount`; using it spends the item with
`sys_recycle` and applies an effect that reuses sheet fields the engine already reads: `grit` restore,
`bp` heal, and the timed `shield` and `mesh` self-buffs. The tag and amount are item data, so a new
consumable is a seed object, not a new branch. In a fight the use costs the turn like a stim; out of
combat it is free.

The suite grew from 546 to 561 checks. The block buys a weapon and wields it, wears a vest and reads the
soak onto the sheet, stows an item in a container, and drives each buff item: a shield gel and a dermal
spray set their timed sheet buffs, a medkit restores body points from one, and a second use of a spent
item confirms it was consumed. Armour soak stays single-valued on the sheet (the last-worn piece wins), a
known engine limit left untouched here.

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
