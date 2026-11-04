# Roadmap

Planned and deferred work, gathered in one place. `PLAN.md` keeps a short
lookback of the most recent completed milestones in detail; the full
milestone history is the git log, and each finished item here is rewritten
as a done-note in place. The federation and storage directions have their
own rationale in `FUTURE.md`.

Nothing here is committed to a schedule. Each item is a known extension
with a sketch of what it touches, to be picked up when a world calls for
it.

## ChromeSix game system

Nested containers are implemented. A container may be stowed inside another:
`cap` now counts direct contents only (`cs_count`), name resolution recurses
into nested containers to a bounded depth (`obj_match_name` and `child_named`),
and `put` allows a container while refusing a containment cycle or nesting past
five levels (`verb_getput.c`, `MAX_NEST`). The weight rollup (`cs_load`) already
summed the full subtree and was unchanged.

Combat gear from body slots is implemented. The worn Soak and active weapon
are a projection of the body's slotted items, not a separately kept tally. A
slot change raises a `gear_dirty` flag on the sheet and the gear verb rebuilds
the projection (`cs_regear`): Soak is the single best worn piece, since armor
does not stack, and the active weapon re-derives from the remaining hand
weapons when one is removed. The dirty flag is the seam for moving the rebuild
to a lazy read path later. NPCs set `armor`/`wielded` directly in the world and
carry no slotted gear, so they are unaffected.

NPC disposition and aggression are implemented. `cs_disposition` is an NPC's
effective standing toward a player, the faction step plus the NPC's personal
`disp` offset, clamped to [-3, 3], so a specific mob can be warmer or colder
than its faction (a grudge where the faction is neutral, or a fixer who still
deals with you). It is the one seam the store, job gates, and aggro share. On
a player entering its room, `mob_react` opens a fight when that disposition is
Hostile (-2) or worse, or when the mob carries the `aggro` behavior token.

- **Death and recovery extensions.** Revival penalties are now wired:
  `player_revive` restores BP but no longer clears Wounds, so a revived
  character comes back alive but injured and pays the `treat` clinic verb to
  mend each wound (rules Section 7, slow and expensive). Still deferred: a
  station requirement for the paid revive (it works anywhere now), and body
  weight while a body is carried (carrying is weightless today).

Room occupant capacity is implemented. A room's `occupancy` prop (unset or 0
means uncapped) caps how many combatants a fight holds, the floor-space sibling
to span. It limits the combat roster, which carries the living fighters and the
downed bodies alike (`cs_room_full`), so once the room is at the cap `attack`
cannot draw another foe in and an aggressive mob will not join. Enforced at the
foe-draw (`verb_attack`) and the mob aggro-join (`mob_react`).

Out-of-combat travel cost is implemented. An exit with a `cost` prop is a long
traverse, and an encumbered mover's travel is halved: `go` crosses it in two
steps, the first banking a half step (keyed to the exit) and the second
completing the crossing (`verb_go`), the twin of the in-fight Slowed rule where
a band costs two Moves. An unencumbered mover and a cost-free exit cross in one
step; overloaded movers are still locked in place.

Social Resolve recovery is implemented. Resolve now persists between scenes
(verb_persuade seeds it once, not every scene) and recovers on the same short
rest as Body Points (`cs_resolve_rest`): it climbs toward max Resolve in a safe
room out of a scene, on the `rest_secs` cadence, and a full recovery clears a
`yielded` concession so a character is not conceded forever. Banked at the same
lazy points as BP: a move, a sheet readout, and the start of a new push.

Multi-attacker resolution is implemented. A character-to-session resolver
(`sys_tell`) lets the turn task prompt any player-driven combatant in their own
session, not just the fight's opener, so more than one player can fight in a
single scene. The combat loop keys turns, targeting, and the win and loss
counts off session resolution (`is_pc`) rather than a single tracked player:
foes are the non-player side, the player side holds the humans, an NPC turns on
the first player still standing, and each downed player runs its own dying
track. A player who attacks a fight they are not in joins it as reinforcement
(`verb_attack`) and acts on their own turn next round. Still a rules call left
for later: sides and teams (combat is free-for-all, a player can target
anyone), and splitting the Character Point award among several players (the
opener books it today).

Pricing in `chromesix.md` is first-pass and tuned by play; that is a rules
note, not an engineering task.

## Online creation (OLC)

By decision, timed `@reset` restocking is not planned: resets are
on-demand, and a live world is topped up by running `@reset`.

Vehicle mid-transit state is implemented. A line sets a `transit` travel time
and either one `track` room (the simple case) or a full `path` of the rooms it
traverses; while under way it occupies that room, doors shut and absent from
both platforms, and pulls in to the destination on arrival. The maglev line
off the cargo bay (vehicle `#940`) is a worked example in the shipped world.

Generic `@clone` is complete. It copies every property with its permission
flags, and the only value type it skips, a list, cannot occur on a world
object: verb `setprop` stores strings only, object serialization persists only
strings and object references, and no host path sets a list property. An
object therefore holds string and object-reference properties alone, both of
which `@clone` already copies. Nothing is left to generalize unless
list-valued properties first become a persisted, first-class type.

## Engine

- **Parallel VM execution.** Not pursued. The workload splits into I/O-bound
  (the HTTP server) and CPU-bound (the RV32 VM) halves, and the `rv_run`
  model would make preemption natural, but bounded quanta on the single
  loop have been enough. Revisit only if VM work starves the loop.

Builder world-branching is implemented. A wizard works off a fork of the live
world and merges back when ready. The architecture and usage are documented in
`smolmoo.md` (Object namespaces); `FUTURE.md` holds the content-addressed
foundation it leans on. In outline: one server process hosts the live world and
up to a few branch worlds at once, each a namespace with a closed object graph
addressed by `(namespace, id)`; the live world is namespace 0 with an unchanged
on-disk layout. A branch is forked (`smolmoo branch` offline, or `@fork`
in-server) at the live root, which doubles as the immutable merge ancestor. A
wizard enters a branch with `@branch <name>` and leaves with `@branch live`.
`smolmoo mergeback <name>` runs the 3-way merge back to live: a per-id hash diff
of ancestor, branch, and live applies a branch-only change, keeps the live
version on a clash and reports it, adds a branch-new object, renumbers one whose
id the live world also took (rewriting references), and applies a branch
deletion the live world left untouched.

Remaining extensions, none yet called for: unloading a branch namespace while
the server runs (today a branch stays resident until shutdown); namespacing the
account, invite, reset, and group scans (they stay live-world, which is correct
for building but blocks those subsystems inside a branch); and raising the
shared `MAX_OBJ` slot pool, which a resident branch roughly doubles the draw on.

## Federation and storage

The federation stack (SHOAL, multi-server peering, the backup follower) and
packfile compaction are deferred to a distant future by decision, gated by
server maturity. They are documented with full rationale in `FUTURE.md`;
revisit when a concrete use case justifies the code.
