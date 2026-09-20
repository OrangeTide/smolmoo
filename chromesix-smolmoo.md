# ChromeSix on smolmoo (Milestone 25 mapping)

How the ChromeSix design in `chromesix.md` maps onto smolmoo's real
primitives. This is an implementation plan for Milestone 25 (Combat
and Character System), not a second rules document. The engine primitives
it builds on (the verb VM, syscalls, the data model, cached rollups) are
described in `smolmoo.md`; this doc covers only the ChromeSix mapping.

## 1. Language reality

ChromeSix ships as C verbs compiled to RV32 ELFs (see `smolmoo.md` for the verb
VM and syscalls). MooScript is available too for world builders, but the combat
and social code stays in C. The pseudo-code in earlier drafts targets LambdaMOO;
treat those snippets as expressions of intent, not as code to port line by line.

## 2. Data model

smolmoo properties are flat strings or object references, so store numbers as
string properties and parse them with `atoi` in the verb (`smolmoo.md` covers
the data model and prototype registration). The ChromeSix creature sheet uses
these properties.

Property naming, all on the creature object, values are decimal strings of
points unless noted:

    agi mig wit cha           attribute points (3 pts = 1D)
    sk_firearms sk_hacking .. skill points, one per skill
    hook                      "cyber" | "awakened" | "street"
    grade                     NPC grade: "mook" | "tough" | "elite" (players tough)
    armor                     soak bonus, derived from worn armor
    bp bp_max                 Body Points
    grit grit_max             Grit pool (grit persists between scenes)
    resolve resolve_max       social Resolve track (rules Section 16)
    wounds                    0..3 wound ladder index
    downed dying_round        dying state (round 0..3 of the bleed-out window)
    dead death_tick           death state and time of death (Section 11)
    cp                        Character Points, the growth currency (XP)
    maneuvers                 comma-separated ids of 5 CP unlocks
                              (maneuvers, cyberware, spells)
    standing                  comma-separated faction:step pairs
                              (e.g. "ns:-2,harshaw:1")
    size                      size step, default 0 (standard human)
    count                     for a mob entity, number of members (else absent)
    money                     funds in creds, a decimal-string integer
    anatomy                   comma-separated worn-slot tags (see Section 9)
    rest_tick                 time short rest began, for BP recovery (Section 7)
    stim_load crash           stim doses this rest, and stim-crash stacks (Section 7)

ChromeSix registers its prototypes on the system object like the built-in ones:
`#0.creature` is the base for players and NPCs, and `#0.combat` owns the combat
verbs. Derived values
(Passive Defense, Passive Perception, Passive Resolve, Soak, Composure, Max BP,
Max Grit, Max Resolve) are computed by a `recalc` verb on creation and after any change, never
hand-stored, so progression cannot desync the sheet.

Persistent versus temporary: attributes, skills, hook, wounds, and current
BP and Grit live on the persistent creature object. Per-fight flags
(initiative, cover tier, temporary defense and attack modifiers, smartlink
target, mana shield active, and timed conditions such as stunned or suppressed,
rules Section 7) are reset at combat end or kept on an ephemeral combat record,
each condition carrying an expiry tick. Cover is the 0 / 2 / 4 integer from Section 6 of the rules,
set by commands like kneel or hide and added into the effective Passive
Defense at hit time.

Unified unlocks: every 5 CP option (maneuver, cyberware mod, spell) is one id
in `maneuvers`, dispatched through a single table keyed by id that holds the
Grit or other cost, the requirements, and the effect handler. One code path
then covers the user command, the cost-and-refuse check, and the mechanical
effect, so adding an option is a table entry, not new plumbing. Installing a
cyberware id also checks the graft-slot cap (2 for the Cyber-Augmented hook),
and spell ids require the Awakened hook.

Equipment and money: weapons, armor, and stims are ordinary smolmoo objects
held in the creature's contents. A weapon object carries its damage rating, an
armor object its soak bonus, a stim its Grit restore, and each item that is
worn or held names its body slot (Section 9). `recalc` sets `armor` from the
best worn armor piece, and the attack verb reads the weapon in the acting hold
or dual slot, defaulting to unarmed 2D when it is empty. Money is a plain
integer counter, spent and refunded like the Grit gauge.

## 3. Randomness

Combat needs dice. The host provides `sys_random(max)`, a uniform integer in
`[0, max)`, so a single d6 is `1 + sys_random(6)` (see `smolmoo.md` for the
syscall). This was the one host primitive combat strictly required.

## 4. Dice engine as a C verb

Implement the ChromeSix pool in the combat verb:

    roll_pool(points) -> { total, wild_state }
      dice = points / 3;  pips = points % 3
      wild = 1 + sys_random(6)
      wild_state = (wild == 6) ? +1 : (wild == 1) ? -1 : 0
      total = wild + pips
      for i in 1 .. dice-1:  total += 1 + sys_random(6)

The Glitch and Edge tables are small and fixed, so hardcode them as C arrays
in the combat verb rather than inventing a serialized table format. This
keeps verb source small, which the project's size constraint favors.

## 5. Combat resolution

A C attack verb using the object syscalls:

* Read attacker and defender fields with `sys_getprop`.
* Roll the attack pool and the damage pool with `roll_pool` (needs
  `sys_random`).
* Apply the Edge or Glitch from the Wild Die.
* Write BP, wounds, and downed state back with `sys_setprop`.
* Resolve names to objects with `sys_objfind`.

Output: build the multi-line combat log into one buffer and send it to the
room with `sys_broadcast` (syscall 6). Private lines to the
acting player go to stdout with `write`. There is no `player:tell` or
`announce_except`, so the log is one broadcast plus optional private writes.
Reuse the existing bar renderer approach for the HP and Grit meters.

Scale and mobs (rules Section 6): a creature's scale tier is its `size` step
(Section 8), and the gap is the difference between attacker and target. The
attack verb checks the gap first: at two or more tiers it skips the normal
damage roll. Striking up deals zero to a larger target unless the weapon is
heavy or area. Striking down calls an Agility + Athletics reflex save on each
smaller creature caught (rules Section 6), applying full maximum damage on a
fail, half on a success, and none when the save's Wild Die shows a Tactical
Edge, so one call resolves a giant sweeping a group. A Mob is one combat-record
entity with a member profile and a `count`; its effective scale is the member
`size` plus a step per number band (pack, swarm, horde, rules Section 6), which
sets its bonus dice and marks its attack as area, hitting all engaged past the
frontage cap. Single-target hits decrement `count`; area and explosive hits
drop it a whole band. When `count` falls below pack size the mob dissolves into
individual creatures or is removed.

NPC grades (rules Section 7) ride the same attack verb. A `mook` skips the BP
and wound writes: the first hit dealing Net Damage sets its downed state, so
fodder resolves in one line. A `tough` runs the full damage path like a player.
An `elite` is a tough whose reaction check allows two reactions a round, and
often a higher `size`. NPCs need not carry attributes: a quick-profile creature
stores its Passive Defense, Soak, attack pool, and damage directly and sets a
flag so `recalc` leaves them, while a fully built NPC recalcs from attributes
like a player. A supernatural creature's fear attack (rules Section 7) reuses
the save path, spending the target's Grit instead of BP and applying a
condition, as a once-per-encounter bonus action flagged on the combat record.

Opposed checks (rules Section 5) stay static: the acting verb rolls the actor's
pool against the target's recalc'd `Passive Perception` or `Passive Resolve`, or
against an object's `security` property, so only one side rolls and no separate
contested-roll path is needed.

Zones and range (rules Section 6) map onto rooms, so position needs no new
spatial model. A combatant's zone is its room (`location`), and zone adjacency
is the room's exits. The ephemeral combat record holds the rest: an engagement
set of who is in melee with whom, and a moved-this-turn flag per combatant.
Bands read off these: Engaged is a pair in the engagement set, Short is the
same room un-engaged, Long is an adjacent visible room. A Move action engages
or disengages within a room or steps to an adjacent one. The attack verb checks
the weapon's range band against the target's band before rolling, applies the
long-shot -1D or refuses an out-of-reach shot, and the moved-this-turn flag
grants the +2 ranged Passive Defense until the combatant's next turn.

A room's **span** bounds how far a fight can spread inside it, in bands rather
than measured size (M25h). `cs_room_span` reads a `span` override, else an
`outdoor` room reaches Long (2) and an indoor room only Short (1). `retreat`
caps at that span, so a tight indoor room holds a fight at Short and a span-0
box gives no ground at all, while an open outdoor room lets a fighter fall back
to Long without leaving. Larger spaces are still modelled as several linked
rooms, not one room with a big number.

## 6. Turn scheduling

Map the turn queue onto smolmoo's task and timer systems rather than a
LambdaMOO handler:

* `sys_spawn` a combat task when a fight starts. It owns the round loop.
* Between rounds the task calls `sys_suspend(delay_ms)` for pacing, or the
  host timer system (`pq.h`, `timer_add`) drives ticks.
* Initiative is a Wit-pool roll per combatant, sorted in the task. No
  `sort_initiative` verb is needed, the task holds the order.
* Orphan cleanup already exists: tasks are reaped when their session
  disconnects.

The in-game clock. A round is about five in-game seconds (rules Section 15),
resolved turn by turn at whatever real pace the task sets, so it never draws on
the wall clock. Outside combat the world clock advances with real time at a
tunable **time scale**, four in-game hours to the real hour by default, so a
two-hour short rest is about thirty real minutes and a wound's downtime days
span real sessions. Rest, Grit trickle, wound healing, and the 24-hour death
timer all measure the in-game clock and accrue whether the player is active,
idle, or logged off, since the world keeps time regardless.

## 7. Grit on the fuel gauge

Grit is mechanically the existing fuel gauge. Milestone 7 already provides a
per-session integer (`cc[sid].fuel`), a maximum (`FUEL_MAX`), tick-based
regeneration through the timer queue (`fuel_schedule_regen`), a
cost-and-refuse check in dispatch, and a status-bar push to the client.

Two options:

1. Add a parallel gauge, `cc[sid].grit`, following the same pattern
   (`grit_schedule_regen` and a second value in the status string next to
   Fuel). Unlike Fuel's fixed `FUEL_MAX`, the cap is the creature's recalc'd
   `grit_max` (3 + Wit + Charm dice, minus cyberware like Sub-Dermal Plating),
   so clamp and regen use that per-character value. Cleanest separation.
2. Repurpose fuel as Grit inside combat rooms only.

Recommend option 1. Maneuvers and the push-a-roll action decrement Grit and
refuse when insufficient, exactly like a costly command today. The Command
support action restores an ally's Grit by adding to `cc[sid].grit`. Medicine
does not restore Grit; it heals BP and Wounds (rules Section 7).

Grit persists between scenes (rules Section 8). Do not refill it at fight
start. The creature's persistent `grit` property is the store of record;
`cc[sid].grit` is a live mirror, loaded from the property on connect or spawn
and written back on change and disconnect, so a reconnect or a new scene does
not reset the pool. NPC Grit lives on the creature object for the fight.
Combat Grit recovery comes from the Command support action and from consumable
items that cost money (rules Section 8), which is the game's primary money
sink. The slow free `grit_schedule_regen` tick models only the downtime rest
and meditation restore, and clamps at the per-character `grit_max`.

The stim crash (rules Section 8) rides two counters. Using a stim adds its Grit
and bumps `stim_load`; while `stim_load` exceeds the creature's Might dice the
extra doses set `crash` to the excess, and the attack and skill verbs subtract
`crash` dice from every pool. Both `stim_load` and `crash` reset when the short
rest completes, the same `rest_tick` cycle that restores BP, so a rest clears
the crash and the safe limit together. The Adrenal Regulator cyberware raises
the effective Might-dice limit by one and absorbs the first `crash` stack.

Body Point short rest (rules Section 7) rides the same timer machinery. A room
carries a `safe` flag, a string property where absent or "0" means unsafe.
Stamp `rest_tick` when a creature settles: the rest is running while it stays
in safe rooms and does nothing strenuous. Any attack, damage taken, or step
into an unsafe room clears `rest_tick` and the rest restarts. A periodic timer
credits BP pro-rated toward Max BP over the short rest period (rules Section
13), so recovery accrues while players idle, chat, or roleplay, with no rest
command. Moving between safe rooms does not break it.

## 8. Factions, size, and descriptive output

Standing (rules Section 14) is a small signed integer per faction, stored in
the `standing` property as `id:step` pairs and parsed with the same split-and-
`atoi` approach as the rest of the sheet. A price or hostility check reads the
step for the relevant faction id and defaults to 0 when the id is absent. An
individual NPC's disposition is that faction step plus a personal modifier held
on the NPC. Charm skills adjust a disposition for a scene on the ephemeral
interaction record, never on the stored standing.

Size (rules Section 15) is one `size` step per creature, default 0 for a
standard human. `recalc` folds the size row into the derived Passive Defense,
Max BP, and Soak, so a large creature is harder to bring down and easier to
hit like any other modifier, and nothing size-related is hand-stored.

The descriptive scales (rules Section 15) are pure lookup. Each scale is a
small static C array mapping a value or range to a word list, hardcoded in the
verb the way the Edge and Glitch tables are, and the verb picks a word,
optionally at random from the overlapping set, when it builds prose. This is
where an admin who hides the mechanics turns a Target Number, a Body-Point
fraction, or a duration into text without ever printing the number.

## 9. Body slots and inventory

A creature's body is a set of fixed worn slots, and its inventory is whatever
hangs off them in the object tree. This uses smolmoo's existing containment,
`location` and `contents`, rather than a bespoke structure.

**Anatomy.** The slots a creature has are its anatomy, stored as the `anatomy`
property, a comma-separated list of slot tags. A tag is a part name plus an
index from 0, so `hand0` through `hand31`, up to 32 of any one part. Each part
type has a role:

* **hold** parts, the hands, wield weapons and tools.
* **wear** parts, head, torso, back, waist, arm, leg, and foot, wear armor,
  clothing, and cyber shells.
* **dual** parts, the tentacles, do either but only one at a time. A tentacle
  wearing something cannot also wield, and a tentacle wielding something cannot
  also wear. A tentacle is arm and hand in one, so it needs no separate arm or
  hand slot.

A standard human is
`head0,torso0,arm0,arm1,hand0,hand1,leg0,leg1,foot0,foot1,back0,waist0`.
Anatomy is editable: a cyber-arm graft adds `arm2,hand2`, a lost hand drops its
tag. Two extremes bound the range:

* A tentacled alien: `torso0` and `tentacle0` through `tentacle11`, with
  `head0` optional.
* A six-limbed frame: `head0,head1,torso0`, `arm0` through `arm5`, `hand0`
  through `hand5`, and `leg0` through `leg5`.

**Worn and held items.** A weapon, armor piece, or tool is an ordinary object
in the creature's contents. An item that occupies a slot carries a `slot`
property naming its tag (`slot=hand0`), and one item fills a slot. Its role
must match the slot: a weapon takes a hold or dual slot, armor takes a wear or
dual slot. An item in contents with no `slot` is loose, in transit or being
handled.

**Containers.** Any worn or held item may be a container, declaring a capacity
(`cap`, an item count). Its contents are objects located inside it and count
against `cap`. A satchel on `back0` holds stims, magazines, and a credchip.
The design allows containers to nest, though depth is kept shallow. Nesting is
not yet supported: the current slices refuse it and assume a single container
level (see the deferral note below), so this stays a planned extension.

**Handling loose items.** `get`, `drop`, and `put` move loose items, those with
no body slot. `drop <item>` sets it on the room floor and `get <item>` lifts it
back; `put <item> in <container>` stows it, refused once the container is at
`cap`. Name resolution reaches one level past the room and the hands: into an
open container and into a fallen body, so `get <item> from <container>` pulls
from a satchel and `get <item> from <body>` strips a casualty's gear, clearing
the looted slot as the item changes hands. Worn or wielded gear is not loose;
`drop` and `put` refuse it with a reminder to `remove` it first, keeping item
handling and the gear verbs cleanly separated. To keep the one-level view sound,
a container will not nest inside another.

**Deferred: nested containers.** Three places assume a single container level
and must change together to support the nesting the design calls for: `put`
refuses a container as its item (`verb_getput.c`); name resolution reaches only
one level into a container (`obj_match_name`, and the M25g inventory flatten);
and `cs_count` treats a container's whole containment subtree as its `cap` count
(correct only while nothing nests, where subtree equals direct contents). The
weight rollup (`cs_load`) already sums the full subtree and needs no change.
When nesting lands, `cap` must count direct contents (a `sys_rollup` depth
bound, or a direct-child count), resolution must recurse to a bounded depth, and
`put` must allow a container while guarding against cycles and runaway depth.

**Inventory as a flatten.** The player's inventory is the object subtree
reachable through the worn slots: the slotted items plus everything nested in
container items, walked and shown as one flat list for display and for "do I
have X" checks. The worn slots are the fixed frame; the inventory is the
consolidated contents hanging from them.

**The flatten view.** The `inventory` command (aliases `inv` and `i`)
collapses the individual slots into a per-part summary of occupied over total,
where total is that part's count in the anatomy, then lists each occupied slot
and, for a container, its contents against its capacity:

    slots: head 0/1 torso 1/1 hand 2/2 leg 0/2 foot 0/2 back 1/1 waist 0/1
    torso0 vest
    hand0 pistol
    hand1 knife
    back0 satchel (2/6): stim, credchip

The first slice prints one occupied slot per line with plain part names. The
richer per-line annotations, weapon dice, armor rating, and stacked counts, ride
along with the ammunition and derived-display work in a later slice. The two
extremes stay compact:

    slots: torso 1/1 tentacle 12/12 head 0/1
    slots: head 2/2 torso 1/1 arm 6/6 hand 6/6 leg 6/6

**Ammunition.** A weapon object either holds ammo directly or takes a clip,
pack, or module object that does; either carrier can be examined for its
current count. A carrier that loads by hand unloads by hand, while others need
a coded process, charging a battery pack for instance. Each shot depletes one
unit or charge, at random from a plain container or from the next occupied
named slot when the carrier uses them. The range of ammo, bullets, cells,
flechettes, grenades, is open and not enumerated here.

**Weight.** Items carry a weight, and their sum against the creature's carry
rating ((Might dice x 2) + pips, rules Section 6) sets encumbrance. Over the
rating applies Slowed in combat and halves out-of-combat travel; past twice it
locks movement until weight is shed. The carry rating comes from the Might
attribute (`cs_carry`) and the current load is the weight rollup of the
inventory (`cs_load`, a cached containment rollup, see `smolmoo.md`).

**Derived from slots.** `recalc` reads the slots for combat. Soak armor is the
best single worn armor piece, not the sum (rules Section 6), plus cyberware.
Passive Defense folds in the best single defensive-gear piece, again not the
sum (rules Section 6), plus any Passive-Defense cyberware such as Kinetic
Absorbers, all clamped under the cap of 20. The attack verb takes the weapon
from the acting hold or dual slot, defaulting to unarmed 2D when it is empty.
Extra limbs expand carrying and holding, never the action economy, so more
hands is not more attacks.

## 10. Player commands

These are the game-specific commands. The general MOO commands (chat, movement,
building, feedback, the web tools) are documented in `help.md` and reachable
in-game with `help`.

Combat is turn-based under the spawned combat task (Section 6), not real-time
twitch. On your turn the task shows the tactical picture and waits for one
command, your single action, with a short timeout that passes the turn if you
say nothing. Reactions fire between turns, from a readied stance or as the
default free Strike on a foe that leaves your melee. Everything outside the
action economy, looking, checking the sheet, setting a target, is free and
does not end your turn.

**Seeing the field.** Free, usable any time.

    look                 the zone, its exits, who is here and their band
    fight | status       your HP, Grit, wounds, and the initiative order
    consider <foe>       a read on a foe, in adjectives when mechanics are hidden
    sheet                the full character sheet
    inventory | i        the slot-and-container flatten (Section 9)

**Your action, once per turn.** Exactly one of:

    attack <foe> [push N]        strike or shoot with the weapon in your acting
                                 hold or dual slot; push spends N Grit for +ND
    cast <spell> <foe> [push N]  an Awakened spell as the action
    use <unlock> [foe]           any other maneuver, cyberware, or spell
    cover <ally>                 the Cover support action
    command <ally>               the Command support action, refund an ally Grit
    close [foe]                  move one band toward a foe
    retreat                      move one band away
    engage <foe>                 Short to Engaged, into melee
    disengage                    leave melee without drawing a free Strike
    go <exit> | n s e w ...      step to an adjacent zone (an action in a fight)
    use stim [on <ally>]         spend a stim, restore Grit
    hold                         do nothing, pass the turn

**Between turns.** A reaction is armed as a stance, not typed mid-resolution:

    ready <reaction>             arm a reaction (for example ready riposte); it
                                 fires when its trigger occurs, once per round,
                                 and stays set until you change or clear it
    guard                        ready your default defensive reaction

Passive reactions such as Emergency Defibrillator fire on their own, nothing to
ready.

**Gear, between fights, off the action economy.**

    wield <weapon> [in <slot>]   hold a weapon in a hold or dual slot
    wear <armor> [on <slot>]     wear armor on a body slot
    remove <item>                free a slot
    reload                       refill the wielded weapon's magazine
    get <item>                   pick a loose item up off the floor
    drop <item>                  set a loose item on the floor
    put <item> in <container>    stow a loose item (refused once at cap)
    get <item> from <container>  pull an item out of an open container
    get <item> from <body>       strip gear off a fallen body
    buy <item> | sell <item>     trade at a vendor, priced by standing (rules Section 14)
    standing                     your faction standing
    recover                      pay a clinic to revive your body (Section 11)
    carry <player> | release     lift a fallen ally's body and set it down
    revive <player>              patch a body back to life (Medicine or Cybertech)

**Targeting shorthand.** `target <foe>` (or `t <foe>`) sets a default so bare
`attack`, `cast`, and `close` act on it. Ranged verbs refuse or warn when the
target is out of the weapon's band, naming the long-shot penalty first.

Most of these are ordinary verbs on smolmoo's existing dispatch, with
`sys_objfind` resolving names. Only the turn prompt and the readied-reaction
check live in the combat task. A turn reads roughly:

    -- round 3, your turn --  HP 18/21  GRIT 4/6
    raider   short     unhurt
    thug     engaged   bloodied
    > ready riposte          (free, arms your reaction, no action spent)
    > attack thug push 1
    You spend 1 Grit and cut the thug down. It drops.
    -- the raider fires from short range and misses; riposte does not
       trigger on a ranged attack --
    -- round 4, your turn --  HP 18/21  GRIT 3/6
    > close raider
    You close the last band to melee with the raider. (+2 Passive Defense vs ranged)

## 11. Death and recovery

On the MUD death is a setback, not permadeath (rules Section 7). A creature
whose three-round dying window (rules Section 7) closes uncured enters the
`dead` state and stops acting, but
its body persists in the room as an inert object still holding its inventory.
`death_tick` stamps the time of death. Three paths bring it back:

* **Allies.** Another player can carry the body, an ordinary object in their
  contents or dragged room to room, to a recovery point, or revive it in place
  with a Medicine or Cybertech action at a suitable station. The body is also
  lootable, so a hostile can strip its gear before help arrives.
* **Recovery services.** A hospital or clinic revives for a fee in creds,
  priced by standing (rules Section 14) like any vendor. The fast, paid path,
  and another draw on the money sink.
* **Auto-recovery.** If no one intervenes, a timer revives the character at the
  default recovery point once `death_tick` is 24 in-game hours old.

On recovery the character returns alive at the recovery point with BP restored
and Wounds cleared; any gear looted while it lay dead stays lost. A revival
penalty (reduced Grit, a lingering condition, or a service debt) is a tuning
knob, so death stings without deleting the character. The tabletop permadeath
mode simply skips all of this: a dead creature is retired.

The first slice implements the paid `recover` service and the auto-recovery
timer, both host-side, with a dead player gated to passive commands and revived
in place of a bespoke corpse (the player object is the body). The second slice
adds the ally path: `carry <player>` and `release` link a bearer to a body, `go`
drags it room to room, and `revive <player>` (Medicine or Cybertech) patches it
where it lies, with the death clock paused while it is carried. The loose-item
slice (Section 9) then makes a body lootable: `get <item> from <body>` strips a
casualty's worn gear one level deep. A station requirement and revival penalties
come later.

## 12. Build and install

Combat verbs are ordinary verbs (`verb_attack.c`, `verb_reload.c`, and so on),
registered in `verbs.conf` and compiled by `make install` like any other. See
`smolmoo.md` for how verbs are built and installed.

## 13. Suggested milestone slicing

Land it in small, testable steps rather than one drop:

* **M25a** (_DONE_) Creature sheet and character creation: properties including `cp`,
  `standing`, `size`, `money`, and `anatomy` with a basic set of worn slots;
  `recalc` (folding size and the Hook bonus into the derived values); a
  `@sheet` display verb; a `@chargen` verb that validates the creation budgets
  (attributes sum 24 and each 3 to 9; skills sum 12 and each within +2D of its
  attribute; one valid Hook; graft slots at most 2; Spellcasting points only
  with the Awakened Hook), applies the Hook effects,
  grants the starting kit into hand and body slots, 300 creds, and 10 CP of
  unlocks, and sets current BP and Grit to their maxima; and skill-check
  resolution against the TN ladder. No combat yet.
* **M25b** (_DONE_) Add the `sys_random` syscall and the `roll_pool` engine, with a
  smoke test that the distribution is sane.
* **M25c** (_DONE_) The attack verb, damage, Soak, BP, and the wound and dying
  tracks, driven by a spawned combat task. Cover as the 0 / 2 / 4 tier on the
  combat record, plus the Cover support action, which grants an ally a
  temporary +2 Passive Defense (the Light Cover value) as a per-fight
  modifier, distinct from the positional tier that kneel or hide set. The
  condition and difficulty descriptive scales (Section 8) land here as the
  shared output helper the combat log uses. The attack pulls its damage rating
  from the weapon in the acting hold or dual slot and Soak from the best worn
  armor (Sections 2 and 9). Zone and range tracking rides here too (Section 5):
  engagement on the combat record, Move actions to close or change room, the
  long-shot -1D, and the +2 ranged defense after moving. The turn prompt and
  the player command set (Section 10) are the interface to all of it. NPC
  grades (rules Section 7) branch here: a mook Downs on the first damaging hit,
  an elite gets a second reaction.
  Two pieces of the command set are held back by design. The Grit-gated
  actions (push, use, command, and the stim item) belong to the Grit gauge in
  M25d. The elite's second reaction is wired but stays latent in the current
  single-player model, where a foe faces at most one player action per round
  and so is only ever triggered for one reaction; it becomes observable with
  multi-attacker fights, which need the character-to-session resolver deferred
  for prompting a non-spawner player. Everything else has both an HTTP smoke
  test in `test.sh` and native unit coverage of the resolution helpers in
  `test/test_chromesix.c`.
* **M25d** (_DONE_) The Grit gauge on the fuel-gauge machinery, wiring maneuvers and
  push-a-roll to it. The Command support action refunds an ally's Grit, and
  Grit persists between scenes, refilled by paid consumables rather than a
  scene reset. Stims are inventory objects that spend an action to restore
  Grit, and buying them draws down `money`.
* **M25e** (_DONE_) Social conflict on the same engine (rules Section 16). Talk runs on
  the combat machinery with different labels, so the whole turn loop, initiative,
  timeout, and roster code is reused rather than duplicated.

  The scene state is the existing `cb_*` room props plus one flag, `cb_mode`,
  which is `fight` (the default, unset reads as a fight for back-compat) or
  `social`. A room holds one scene at a time: the four push verbs refuse to open
  while a fight is active, and the combat action verbs refuse while a social
  scene is active, both through one `cs_wrong_mode` guard. This shared state is
  what makes the two mutually exclusive, matching the standalone slice.

  Three derived stats join Passive Defense, Soak, and Max BP, all computed from
  the sheet the same way (`chromesix_verb.h`): Max Resolve `= 12 + Charm dice x3`,
  Composure `= Wit dice x2 + Wit pips` (the Soak shape), and Passive Resolve
  `= 4 + (Wit + Charm dice) x2 + pips` (the Passive Perception shape). Current
  `resolve` is initialised on the sheet when a combatant enters the scene.

  Four verbs, one per approach, `command` / `negotiate` / `con` / `perform`
  (each `<verb> <target>`), share one resolution helper `cs_push_resolve`
  mirroring `cs_attack_resolve`: roll Charm + the approach skill against the
  target's Passive Resolve for the push, and on a hit roll the pressure pool
  against Composure, cutting the remainder from Resolve. The pressure pool is
  the target's `leverage` prop (points, converted to dice like any pool),
  defaulting to `9` (3D, the "ordinary" leverage) when unset, a single
  authorable difficulty knob per NPC. `hold` already passes a turn and is reused.

  The scene is bidirectional. On an NPC's turn the `__combat` task branches on
  `cb_mode`: instead of attacking, the NPC pushes the player, draining player
  Resolve, using its own `leverage` as the pressure pool. A combatant at 0
  Resolve yields. When an NPC yields the task calls the target's `on_yield` verb
  (its object id in an `on_yield` prop, spawned by ELF hash with the pusher and
  NPC ids passed as args), or falls back to a broadcast concession plus a
  `yielded` flag when the NPC scripts none. The scene ends when every NPC has
  yielded (the `foes_up` analog) or the player yields; a player yield just ends
  the scene, with no further penalty. `on_yield` is the seam where M25f later
  writes standing and disposition shifts.

  Resolve recovery is out of scope for this slice: a scene drains Resolve and
  handles yield only. Verified with `test.sh` HTTP smoke tests in the M25a-d
  style (a push-to-yield run against a scripted NPC, a player-yields run, and a
  mode-collision refusal) plus native coverage of `cs_push_resolve` and the
  three derived-stat helpers in `test/test_chromesix.c`.
* **M25f** (_DONE_) Factions and standing: the `standing` property and its
  price and service checks. `cs_standing` reads the signed step for a faction
  from the sheet's `standing` id:step list (Neutral 0 when absent), and
  `cs_standing_word` names the seven bands. The `buy` verb reads the room
  `vendor`'s `faction`, shifts the price 10 percent per step, and refuses at
  Hostile (-2) or worse. Standing is GM-adjudicated through the admin-only
  `@standing <player> <faction> <step>` command, which merges one faction into
  the sheet and leaves the rest, and the sheet lists the bands. NPC disposition
  (faction step plus a personal modifier) and hostility aggression are deferred
  to a later slice.
* **M25g** (_DONE, first slice_) Body slots and the inventory flatten view
  (Section 9). Anatomy lives on the creature object as the `anatomy` slot-tag
  list (defaulting to the standard human frame), and an item occupies a slot by
  naming its concrete tag in a `slot` property. New verb helpers carry the
  frame: `cs_anatomy`, `cs_part_role` (hand hold, tentacle dual, else wear),
  `cs_item_role` (a weapon deals damage and holds, everything else wears),
  `cs_slot_holder`, and `cs_find_free_slot` with an optional part preference.
  The `wield`/`wear` verbs validate the role and place the item in a free slot
  (an item's `part` prop steers it, so a satchel takes back and a vest torso),
  and `remove` clears the tag. The `inventory` verb (aliases `inv`, `i`) walks
  contents through the new `sys_next` syscall and prints the per-part
  occupied/total summary, the slotted items, and each container's contents
  against its `cap`. Tentacle dual slots and anatomy editing are supported by
  the model. The sheet's `wielded`/`worn`/`armor` stay the combat source of
  truth this slice; ammunition, weight and encumbrance, and moving combat
  soak/weapon onto the slots are deferred to a later slice.
* **M25g slice 2** (_DONE_) Ammunition and weight (Section 9, rules Section 6).
  A ranged weapon carries a magazine directly: `clip` capacity and `ammo`
  rounds; helpers `cs_uses_ammo`, `cs_weapon_empty`, `cs_fire`, and
  `cs_reload_weapon` drive it. A resolved shot depletes one round, an empty
  weapon clicks instead of firing, and the new `reload` verb refills to the
  clip (free out of a fight, an action in one). A weapon with no `clip` (a
  blade, bare hands) never runs dry, so the pre-seeded combat weapons stay
  unlimited. `inventory` shows an ammo weapon's `[ammo/clip]`. Weight: each
  item carries a `weight`, `cs_carry` gives the (Might dice x 2) + pips carry
  rating, and `cs_load` sums the inventory (slotted items plus one level of
  container contents). `cs_encumbrance` returns the tier: over the rating is
  encumbered, past twice it overloaded. The sheet shows `Load n/cap` with the
  tier. Overloaded locks all movement (`go`, `close`, `retreat`, `engage`
  refuse). Encumbered is Slowed in a fight: one Move arrives per turn and a
  band costs two, so a `move_bank` on the sheet banks the half step and the
  second Move completes the band. Out-of-combat halved travel across discrete
  rooms, ammo carriers as separate magazine objects, and deriving combat
  soak/weapon from the slots remain deferred.
* **M25g slice 3** (_DONE_) Loose-item handling (Section 9). `get`, `drop`, and
  `put` in `verb_getput.c` move items with no body slot: `drop` to the floor,
  `get` off the floor, `put <item> in <container>` (refused at `cap`, and no
  container nests inside another). `obj_match_name` gains a lowest-priority tier
  that reaches one level into an open container (`cap` > 0) or a dead body in
  reach, so `get <item> from <container>` pulls from a satchel and
  `get <item> from <body>` strips a casualty's gear (closing the M25i lootable-
  body deferral), clearing the looted `slot` as it changes hands. `drop` and
  `put` refuse worn or wielded gear ("remove it first"), leaving that to the
  `remove` verb.
* **M25h** (_DONE_) Room span: how far a fight can spread inside one room,
  expressed in the existing range bands rather than measured distance. A room
  carries an optional `outdoor` flag (flavor, and the spacing default) and an
  optional `span` override. `cs_room_span` returns it: an explicit `span` wins,
  else an outdoor room reaches Long (2) and an indoor room only Short (1),
  clamped to 0..2. `retreat` caps the band it opens at the room span, so an
  indoor fight cannot back off past Short and a span-0 box refuses the move
  outright; crossing to an adjacent zone stays `go`, not a band. Leaving
  out-of-combat travel cost (an exit `cost` feeding the halved-travel rule) and
  a hard occupant capacity for a later slice.
* **M25i** (_DONE, first slice_) Death and recovery (Section 11). When the
  cling-to-life track (Section 7) closes uncured the combat verb sets `dead`,
  and the host owns the rest. A dead character is an inert body: the dispatch
  gate refuses action verbs (only `look`, the sheet, and the passive host
  commands still work), while the player object stays in the room holding its
  inventory. Two recovery paths bring it back, both reviving at the recovery
  point (room #101) with BP and Wounds restored (`player_revive`): the paid
  `recover` command charges a clinic fee in creds, priced by standing with the
  clinic faction like any vendor (`standing_step`, `bp_max` host helpers); and
  a death clock (`death_sweep_cb`, tunable via `SMOLMOO_DEATH_MS`, default 24h)
  auto-revives a body once its countdown elapses. Two GM tools drive and fund
  the track: `@kill <player>` (the deterministic death entry, since a Might-3D
  fighter almost never fails all three cling rolls) and `@grant <player>
  <amount>`. Ally carry and in-place revival, a lootable-body/corpse object, a
  station gate, and revival penalties are deferred to a later slice.
* **M25i ally slice** (_DONE_) Carry a body between rooms and revive it in
  place (Section 11). A dead body carries a `carrier` link to its bearer and the
  bearer a `carrying` link back; `carry <player>` and `release` set and clear
  them, and the VM `go` drags the linked body along room to room. While carried
  the death clock pauses (an ally is handling it). `revive <player>` needs
  Medicine or Cybertech and calls `player_revive` at the reviver's own room, so
  a friend can be hauled somewhere and patched where they lie rather than
  waking at the recovery point. `@teach <player> <skill> <points>` is the GM
  knob behind the skill gate. Looting a fallen body landed later in M25g slice
  3 (`get <item> from <body>`). Still deferred: a station requirement, body
  weight while carried, and revival penalties.

* **M25j** (_DONE_) Cached containment rollups, a host primitive (`sys_rollup`,
  see `smolmoo.md`). `cs_load` becomes `sys_rollup(creature, "weight")` instead
  of walking the inventory each call, and `cs_count(container)` (an item count,
  `sys_rollup(container, "")`) backs both the `put` capacity check and the
  `inventory` `count/cap` display from one cached source that cannot disagree.
  Deriving combat soak/weapon from the slots is the same shape and can reuse it
  next.
* **Nested containers** (_planned_) Support the container-in-container nesting
  the design calls for (Section 9, "Deferred: nested containers"). The current
  slices refuse it and assume one container level, so three places change
  together: `put` allows a container as its item (guarding against cycles and
  runaway depth), name resolution recurses to a bounded depth, and `cap` counts
  direct contents rather than the whole subtree. The weight rollup already sums
  the full subtree and is unaffected.

Each step is verifiable with `test.sh` in the style of the existing verb
tests, which keeps regressions visible.
