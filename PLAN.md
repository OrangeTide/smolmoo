# Completed Milestones

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

## Milestone 69: Automap on room entry

The last step of the map series makes it opt-in automatic. `map on` and `map off` toggle a per-player
`automap` flag, and when it is set the `go` verb draws the destination map on arrival, right after the
room description. The flag lives on the player object and defaults off, so nothing changes for a player
who does not ask for it.

The `go` verb fires the map through `sys_call`, which runs the map verb as its own task. That task is
handed the room the player just left, so the map verb was changed to read the room from the player's
current location rather than its call context; it now always draws where the player actually is, which is
correct for a typed `map` too. This is the one wrinkle the trigger needed. The suite grew from 577 to 580
checks: the flag toggles on, a real `go` through an exit (not the `@go` teleport, which does not run the
verb) draws the room just entered, and the flag toggles back off.

With the map series complete, the deferred pieces remain: authored per-room ASCII art with a floor mask,
and the global map that reflects room sizes. The `mapw`/`maph` and `dir` data authored here are the
groundwork for that world map.

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
