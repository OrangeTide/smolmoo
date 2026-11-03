# Roadmap

Planned and deferred work, gathered in one place. Completed milestones
are logged in `PLAN.md`. The federation and storage directions have their
own rationale in `FUTURE.md`.

Nothing here is committed to a schedule. Each item is a known extension
with a sketch of what it touches, to be picked up when a world calls for
it.

## ChromeSix game system

- **Nested containers.** Support a container inside a container. Three
  places assume a single level and must change together: `put` refuses a
  container as its item (`verb_getput.c`); name resolution reaches only one
  level into a container (`obj_match_name` and the inventory flatten); and
  `cs_count` treats a container's whole containment subtree as its `cap`
  count, correct only while nothing nests. When nesting lands, `cap` must
  count direct contents (a bounded `sys_rollup` depth or a direct-child
  count), resolution must recurse to a bounded depth, and `put` must allow
  a container while guarding against cycles and runaway depth. The weight
  rollup (`cs_load`) already sums the full subtree and needs no change.

- **Combat gear from body slots.** The sheet's `wielded`, `worn`, and
  `armor` props are still the combat source of truth. Derive the active
  weapon and Soak from the items in the hand and worn slots instead, so the
  slot model and combat agree without a separate copy.

- **NPC disposition and aggression.** Disposition (a faction step plus a
  personal modifier) and hostility-driven aggression are not wired. A mob's
  standing toward a player would steer whether it opens a fight on sight.

- **Death and recovery extensions.** A station requirement for revival,
  revival penalties, and body weight while a body is carried are deferred.
  The first recovery slice revives at a fixed point or in place with no
  cost beyond the clinic fee.

- **Room occupant capacity.** A hard cap on how many combatants or bodies a
  room holds, alongside the existing range-band span.

- **Out-of-combat travel cost.** An exit `cost` feeding the halved-travel
  rule for an encumbered character moving between rooms. In a fight,
  encumbrance already slows movement; out of a fight it does not yet.

- **Social Resolve recovery.** A social scene drains Resolve and handles
  yield only; there is no recovery path yet, the counterpart to short-rest
  BP.

- **Multi-attacker resolution.** A character-to-session resolver so a foe
  can prompt a player who did not open the fight. It would make the elite
  NPC's second reaction observable (it is wired but latent in the current
  one-action-per-round model) and enable fights with several attackers.

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

- **Builder world-branching.** Working off a fork of the live world and
  merging when ready. It pairs naturally with OLC but is not a prerequisite.
  See `FUTURE.md`.

## Federation and storage

The federation stack (SHOAL, multi-server peering, the backup follower) and
packfile compaction are deferred to a distant future by decision, gated by
server maturity. They are documented with full rationale in `FUTURE.md`;
revisit when a concrete use case justifies the code.
