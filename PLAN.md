# Completed Milestones

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

## Milestone 65: Fixed objects

The first step toward a per-room map: scenery that cannot be carried off. An item with a `fixed` prop is
refused by the floor path of the `get` verb (verb_getput.c), so a room feature like the wall-mounted
autodoc stays put instead of being pocketed. The change is one guard in the verb and one prop on the
autodoc seed object; no core-server change. This also closes a small gap where a heavy fixture could be
picked up despite its weight.

Fixed objects are the groundwork for the room map that follows: the map draws the room's immovable
features at their positions, so the world first needs a way to mark an object as a feature rather than
loose loot. The suite grew by one check, confirming `get` refuses the fixed autodoc.

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
