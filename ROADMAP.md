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

- **Generic `@clone`.** The current clone copies a known property list,
  which covers the common cases. A clone that copies arbitrary properties,
  including list-valued ones, cannot be written purely in the VM today
  (`getprop` is by-name and `sys_next` walks containment, not property
  keys). World objects do not use list-valued properties, so this is low
  priority.

- **Consumable-counter item type.** Generalize the stim's counter model (a
  dose that credits a sheet counter rather than carrying an object) into a
  reusable item type, so a world can add other counter consumables without
  C changes. The `buy`/`sell` verbs already special-case the stim counter.

- **Vehicle mid-transit state.** Vehicles hop stop to stop with no in-between
  state. A mid-transit position is not needed for working trains and lifts.

By decision, timed `@reset` restocking is not planned: resets are
on-demand, and a live world is topped up by running `@reset`.

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
