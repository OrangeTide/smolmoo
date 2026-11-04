# ChromeSix on smolmoo

How the ChromeSix combat and character system in `chromesix.md` maps onto
smolmoo's real primitives. This is a mapping document, not a second rules
document. The engine primitives it builds on (the verb VM, syscalls, the
data model, cached rollups) are described in `smolmoo.md`; this doc covers
only the ChromeSix mapping.

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
and spell ids require the Awakened hook. The acquire side is the `cs_unlocks`
catalog and the `learn` verb, which price and gate an id;
each unlock's effect still lives with its use rather than in one dispatch table.

Equipment and money: weapons, armor, and stims are ordinary smolmoo objects
held in the creature's contents. A weapon object carries its damage rating, an
armor object its soak bonus, a stim its Grit restore, and each item that is
worn or held names its body slot (Section 9). Soak and the active weapon are a
projection of those slots the gear verb rebuilds on a slot change: `armor` is
the single best worn armor piece, and `wielded` is the weapon in the acting
hold or dual slot, defaulting to unarmed 2D when the hands are empty. A
`gear_dirty` flag on the sheet marks the projection stale, the seam for a lazy
rebuild later. Money is a plain integer counter, spent and refunded like the
Grit gauge.

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
than measured size. `cs_room_span` reads a `span` override, else an
`outdoor` room reaches Long (2) and an indoor room only Short (1). `retreat`
caps at that span, so a tight indoor room holds a fight at Short and a span-0
box gives no ground at all, while an open outdoor room lets a fighter fall back
to Long without leaving. Larger spaces are still modelled as several linked
rooms, not one room with a big number.

A room's **occupancy** caps how many combatants a fight there can hold, the
floor-space sibling to span. An `occupancy` prop (unset or 0 means uncapped)
limits the combat roster, which carries the living fighters and the downed
bodies alike, since a fallen combatant stays on the roster taking up the floor
(`cs_room_full`). Once a fight is at the cap, `attack` cannot draw another foe
in and an aggressive mob will not pile on, so a cramped room forces the fight
to stay small.

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

Grit is mechanically the existing fuel gauge. The engine already provides a
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
A container may itself be stowed in another container. `cap` counts only direct
contents, so a nested container costs one slot in its holder no matter how full
it is. Nesting is bounded to five levels deep, a guard against a cycle or a hand
edit sending a walk runaway, not a play limit worth reaching.

**Handling loose items.** `get`, `drop`, and `put` move loose items, those with
no body slot. `drop <item>` sets it on the room floor and `get <item>` lifts it
back; `put <item> in <container>` stows it, refused once the container is at
`cap`, when it would nest past the depth bound, or when it would form a
containment cycle. Name resolution reaches past the room and the hands into open
containers, following the nesting to the depth bound, and into a fallen body, so
`get <item> from <container>` pulls from a satchel or a pouch inside it and
`get <item> from <body>` strips a casualty's gear, clearing the looted slot as
the item changes hands. Worn or wielded gear is not loose; `drop` and `put`
refuse it with a reminder to `remove` it first, keeping item handling and the
gear verbs cleanly separated.

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

Each occupied slot prints on its own line with plain part names, and an ammo
weapon shows its `[ammo/clip]`. The two extremes stay compact:

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
    treat                        mend one wound at a clinic (Medicine or Cybertech)
    train <skill>                spend Character Points to raise a skill one pip
    learn [unlock]               list the unlock catalog, or acquire one by id
    jobs                         list contracts offered here and your active one
    accept <giver> | abandon     take an offered job, or drop your active one
    turnin | report              claim the reward at the giver once the job is done

Winning a scene, a fight where the foes are downed or a social scene where the
target concedes, awards Character Points, summed over the defeated foes. `train`
spends them on skills: raising a skill one pip costs CP equal to its current
rating in dice, and a skill cannot be trained past attribute + 6 skill points,
the same ceiling character creation uses. `learn` with no id lists the catalog,
each unlock's cost, hook, and whether it is owned, available, or out of reach.
`learn <id>` spends CP on that unlock: each maneuver, cyberware mod, and spell in
the catalog costs 5 CP, and is gated by the
hook it needs (cyberware needs the cyber hook, a spell the awakened hook) and,
for cyberware, the two graft slots. A learned id joins the `maneuvers` list; its
effect then applies wherever that unlock is checked.

A job giver (a builder object marked `job`=1) offers a contract with a goal and a
reward. Two goal types exist: a bounty, defeating a named target creature,
completed at the fight's end, and a courier, reaching a named destination room,
completed on arrival. `accept` takes the job, and once it is done `turnin` at the
giver pays its creds, CP, and an optional faction standing step. You hold one
contract at a time.

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

On recovery the character returns alive with BP restored but its Wounds intact:
revival patches you breathing, it does not heal injuries. Wounds are slow and
expensive (rules Section 7), so a revived character limps to a clinic and pays
the `treat` verb (a Medicine or Cybertech roll, a fee per wound) to mend each
one. That retained damage is death's penalty beyond the revive fee, so death
stings without deleting the character; any gear looted while it lay dead stays
lost too. The tabletop permadeath mode simply skips all of this: a dead
creature is retired.

The paid `recover` service and the auto-recovery timer are host-side; a dead
player is gated to passive commands and revived in place, the player object
itself serving as the body rather than a bespoke corpse. An ally path links a
bearer to a body: `carry <player>` and `release` set and clear the link, `go`
drags it room to room, and `revive <player>` (Medicine or Cybertech) patches it
where it lies, with the death clock paused while it is carried. A body is
lootable: `get <item> from <body>` strips a casualty's worn gear one level deep.
A station requirement and revival penalties are a planned extension
(`ROADMAP.md`).

## 12. Build and install

Combat verbs are ordinary verbs (`verb_attack.c`, `verb_reload.c`, and so on),
registered in `verbs.conf` and compiled by `make install` like any other. See
`smolmoo.md` for how verbs are built and installed.
