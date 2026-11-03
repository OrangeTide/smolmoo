# Completed Milestones

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

## Milestone 67: Fixed-object overlay

The map now draws a room's fixed features inside the box. A feature with an authored `mapx`/`mapy` sits
at that cell (the new Warrens sump pump at 8,1); a feature without coordinates is first-fit into the
first free interior cell in a stable scan order (the Med Bay autodoc lands at 0,0). Each placement marks
its cell so two features never overlap, and a feature that finds no free cell is recorded unplaced and
only named. A Features legend under the box lists each feature with its cell, or notes that it did not
fit.

Placement stays deterministic and pure-compute, so `verb_map.c` remains a read-only verb: the same room
always draws the same feature layout without storing anything. This is the right model for fixed scenery,
which never moves; the random, persisted placement comes with characters next, where the map will need to
write.

This step also fixes the exit rendering from the first cut. An exit used to blank its wall cell, which
left a side exit's row starting with a gap (the edges no longer lined up) and made every exit read as
empty space. An exit is now a labelled doorway: its direction letter sits in the wall (`n`/`s`/`e`/`w`,
the diagonals a corner slash), so the border keeps a character in every position and each opening says
which way it leads.

Every exit in the world now carries a `dir` heading, not just the Warrens zone, so no room maps with a
`?` opening. The headings were authored per area with reciprocal exits facing opposite ways (a north exit
returns south), the concourse hub spreading its ten exits across all eight compass points plus up and
down. The suite grew from 569 to 574 checks, drawing the authored pump position, the first-fit autodoc,
an exit rendered as a door in the wall, and a fully-headed exit legend.

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
