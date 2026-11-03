# Completed Milestones

## Milestone 65: Fixed objects

The first step toward a per-room map: scenery that cannot be carried off. An item with a `fixed` prop is
refused by the floor path of the `get` verb (verb_getput.c), so a room feature like the wall-mounted
autodoc stays put instead of being pocketed. The change is one guard in the verb and one prop on the
autodoc seed object; no core-server change. This also closes a small gap where a heavy fixture could be
picked up despite its weight.

Fixed objects are the groundwork for the room map that follows: the map draws the room's immovable
features at their positions, so the world first needs a way to mark an object as a feature rather than
loose loot. The suite grew by one check, confirming `get` refuses the fixed autodoc.

## Milestone 66: The room map verb

The `map` command draws a small ASCII map of the current room. It is a read-only sandbox verb
(`verb_map.c`), not a core-server change: it reads the room's `mapw`/`maph` to size a bordered box (a
default when unset, so every room maps) and the new `dir` tag on each exit to cut an opening in the wall
at that exit's heading. The four cardinal and four diagonal headings become wall gaps; `u` and `d` drop
the roguelike `<` and `>` inside. A legend under the box ties each direction to its exit name, so the
shape shows where an exit lies and the legend says which exit it is. Exits are told apart from items and
characters in the room by carrying a `dest`.

The exit model gained one field. Exits were named children with only a `dest`; a `dir` tag now gives each
a heading, authored here across the Warrens zone (the hub at #174 sized 11x5, with west, south, and up
exits) and reusable later for directional movement and a world map. The suite grew from 562 to 569
checks, drawing the authored hub and a default-sized room and reading back the box width and the exit
legend. Fixed-object and character overlays, plus an opt-in automap on room entry, build on this next.

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
