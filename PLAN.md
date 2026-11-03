# Completed Milestones

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

## Milestone 68: Character overlay

The map now shows who is in the room. Each character is placed at a random free cell and drawn in the
box, the viewer as `@` and everyone else as `&`, with a Here legend naming each and its cell. A placement
is stored on the character and tagged with the room (`mapx`/`mapy`/`maproom`), so a redraw keeps everyone
where they were, and it is assigned lazily the first time the room is drawn after a character arrives.
Tagging the position with the room is what lets movement need no hook: a position left over from another
room is simply replaced on the next draw.

Lazy placement is a write to a shared object, so `verb_map.c` becomes setuid and brackets its work with
`grant_accept`, the first write the map verb has needed. A character is told apart from a loose item by
having a sheet: a `charid`, or attributes inherited from the character prototype, which a scenery item
does not have. Placement runs after the fixed features are drawn, so a character never lands on a feature,
and two passes keep stored positions stable: the first honours a still-free stored cell, the second
places whoever is left. The suite grew from 574 to 577 checks, repopulating the Gnaw with its vermin pack
and reading the viewer and a vermin out of the Here legend.

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
