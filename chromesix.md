# ChromeSix

A compact, tunable dice-pool RPG. ChromeSix is the rules engine for the
Liminal Frontiers setting, a point-based variant of the OpenD6 family adapted
for text-based, mostly-automated MUD combat.

Lineage: inspired by OpenD6 and Mini Six: Bare Knuckle Edition
(Ray Nolan, CC BY 4.0). ChromeSix diverges in several places and is not
Mini Six compatible.

All constants in this document are defaults. The tuning knobs are listed
in Section 13 so lethality and pace can be adjusted in one place.

---

## Introduction

These rules meet the Liminal Frontiers setting from the ground, at the level
of the people who work the galaxy and do not own it.

Picture the crew you play as a unit like Omega Team: blacklisted operators
running retrieval and protection jobs out of a welded-together junkyard on a
crowded station. Ex-corporate, ex-military, good at the work, and broke. The
lights are on and the shuttle is fueled because somebody paid, and nobody
pays out of kindness. You earn a living and you help where you can.

What you know is the shape of the world you live in. Two centuries ago the
megacorporations reached the stars first, and they still own the air inside
every dome. Most people carry cyberware, the good hardware goes to whoever
can pay, and privacy is a memory. You take contracts in the margins because
the legitimate doors are closed to you.

What you do not know is why some jobs feel wrong. The corps keep pulling
things out of deep space and locking them away behind security no artifact
should warrant. Survey crews come back with gaps in their reports and fewer
names on the roster. People you trained with go near certain projects and
come back hollow. The crew calls this "the weird." It is not a system, it is
scuttlebutt and instinct: don't take the job on that station, don't touch
anything that isn't on the manifest, walk away from cargo that is cold in a
way cold should not be. The veterans who last are the ones who heed that
instinct without being able to explain it.

That gap is deliberate. The full truth of Liminal Frontiers, why the weird
exists and what waits behind it, is the Game Master's to reveal in play.
These rules are written from the operator's chair. Your characters learn what
they learn, when they learn it, and not before.

## Design Philosophy

The design diverges from its OpenD6 roots to speed up play, remove
character-creation trap builds, and hold the math inside stable bounds. Four
pillars carry it.

* **Integer point system.** Every attribute and skill is one integer. Dice
  and pips derive from it, so caps are ceilings and costs are deltas.
* **Static resolution.** Attackers roll against a static Passive Defense, and
  damage checks against a static Soak. There are no active defense rolls,
  which keeps combat fast and easy to automate.
* **Horizontal growth.** Characters advance by gaining new maneuvers,
  cyberware, and spells, not by inflating dice pools past the ladder.
* **The three-currency rule.** No ability is free and repeatable. Every
  advantage costs at least one of an action, player time, or a managed
  resource such as Grit or Body Points. The resource economy is also what
  keeps a fighter dependent on the medic and the fixer between jobs, so no
  role in a crew is dead weight.

---

## 1. The Point System

Every attribute and skill is stored as a single integer number of points.

    3 points        = 1 die (1D)
    remainder       = pips
    roll            = nD6 + pips

Examples:

    6 pts  -> 2D
    7 pts  -> 2D+1
    10 pts -> 3D+1

Pips add a flat bonus to the pool total, exactly as in OpenD6. Storing one
integer keeps the sheet trivial to program: caps are point ceilings, costs
are point deltas, and the sheet derives `points / 3` dice and `points % 3`
pips for display.

---

## 2. Attributes

Four attributes, each governing an equal share of skills.

* **Agility** reflexes, ranged and light melee attacks, stealth, defense.
* **Might** physical power, heavy weapons, damage, soak, Body, survival.
* **Wit** initiative, tactics, hacking, medicine, perception.
* **Charm** social conflict, contacts, leadership, negotiation.

Each attribute has a combat role and an out-of-combat role, so no attribute
is a pure dump stat. Combat still favors Agility and Might in a fight. The
balance load is carried by non-combat play and by the resource economy in
Section 8, which routes recovery through Wit and Charm skills.

---

## 3. Skills

Skills are distributed evenly, five per attribute. A skill roll pools the
governing attribute plus the skill: `attribute dice + skill dice + pips`.

| Attribute | Skills |
| --- | --- |
| Agility | Firearms, Melee, Stealth, Athletics, Drive |
| Might | Heavy Weapons, Brawl, Endurance, Haul, Demolitions |
| Wit | Hacking, Cybertech, Medicine, Perception, Spellcasting |
| Charm | Command, Negotiate, Con, Streetwise, Performance |

The recovery and support skills (Cybertech, Medicine, Streetwise, Command)
sit under Wit and Charm on purpose. They are what make those attributes
matter once the shooting starts, because they refill the combat resources
between and during fights.

Most skills resolve as a check against the TN ladder or as an opposed check
(Section 5). How each group plays:

* **Agility** is physical action. Firearms and Melee attack; Stealth is opposed
  by Passive Perception; Athletics covers climbing, leaping, and the scale
  reflex save; Drive runs vehicles and chases as extended tasks.
* **Might** is force and endurance. Heavy Weapons and Brawl attack; Endurance
  resists exhaustion, poison, and hardship; Haul is lifting and dragging;
  Demolitions sets and defuses charges, often an extended task under a clock.
* **Wit** is the mind. Hacking cracks systems against their security; Cybertech
  installs, repairs, and stabilizes; Medicine heals and stabilizes; Perception
  spots and sets Passive Perception; Spellcasting channels, gated by the
  Awakened Hook.
* **Charm** is people. Command, Negotiate, Con, and Performance drive social
  conflict (Section 16) and shift standing; Streetwise finds gear, rumors, and
  a way in.

---

## 4. Character Creation

Points, at 3 points per die. A character is built in five steps: attributes,
skills, a Hook, starting unlocks, and starting gear.

**Attributes**

* Start each attribute at 1D (3 pts). That is 12 points of base.
* Spend 12 more points across the four attributes.
* Creation cap: 3D (9 pts) in any one attribute.

A typical result is 3D / 2D / 2D / 1D. Characters are mundane, competent in
one area, superhuman in none.

**Skills**

* Spend a pool of 12 points (about 4D) across the skill list.
* Creation cap: +2D over the governing attribute in any one skill.
* A skill left at 0 points is untrained. You may still attempt it, rolling
  the governing attribute alone. Spellcasting is the exception, gated by the
  Awakened Hook and never rolled untrained, so points spent there are wasted
  unless you took that Hook.

This creates the intended tension. You can start narrow and good, for
example Agility 3D plus Firearms 2D for a 5D shooting pool with little else,
or broad and shallow. You cannot do both.

**Hook**

Pick one Hook (Section 10). It sets your Passive Defense bonus and one
defining trait, and it gates the next step. Only the Awakened cast spells, and
only the Cyber-Augmented install cyberware, up to their graft-slot cap.

**Starting unlocks**

Spend 10 CP on unlocks from Section 11, the standard 5 CP each, so two picks.
Respect the Hook gates: an Awakened character takes a spell or two so it can
actually cast, and a Cyber-Augmented character fills its graft slots with
cyberware. Street Savvy's +2 CP is a general head start that carries into play
rather than a third pick. Without this budget a Hook's defining feature would
sit unused until sessions of earned CP accrued, so it is the step that makes a
character playable on day one.

**Starting gear and funds**

Every character begins with a basic kit and a small cash reserve.

* One weapon up to the SMG or Rifle tier (Section 6), plus a knife.
* Armor up to Medium. Armor sets the bonus in the Soak formula:

      Light +1 | Medium +2 | Heavy +3

* Three stims. A stim costs an action to use and restores 2 Grit, with a safe
  limit per rest and a crash beyond it (Section 8).
* Starting funds of 300 creds, spent on more gear, stims, and the ongoing
  cost of keeping Grit topped up.

Reference prices, all tunable, so starting funds mean something:

      Stim 75 | Knife 20 | Light armor 50 | Pistol 100 | Medium armor 150
      Katana 200 | SMG 250 | Rifle 300 | Heavy armor 400 | Heavy weapon 800

Defensive gear that raises Passive Defense is priced with the rest of that
kit in Section 6, at the expensive end so the choice costs a real score.

A new character sits at Neutral, 0, with every faction, and standing shifts
these prices by 10 percent per step (Section 14). Street Savvy additionally
starts knowing one contact, a named NPC at Friendly disposition.

---

## 5. Core Resolution

Roll a pool of d6 equal to the relevant `attribute dice + skill dice`, add
pips, and compare the total to a Target Number.

**Target Numbers**

    Easy 6 | Moderate 10 | Hard 14 | Formidable 18 | Heroic 22

Moderate is a coin flip for a typical 3D pool. Hard is a real test that
rewards a specialist. The ladder is tuned for the smaller pools above, not
for the inflated pools of earlier drafts.

### Opposed Checks

Some actions are resisted by another character rather than a fixed difficulty:
sneaking past a guard, conning a mark, hacking guarded ice. ChromeSix keeps
these static too, so only the acting side rolls. The resister contributes a
**Passive rating**, the static twin of the pool it would otherwise roll:

    Passive rating = 4 + (pool dice x 2) + pool pips

A 3D pool gives 10, exactly Moderate, so a contest between equals is the same
coin flip as an unopposed Moderate task, and every extra die of the resister
raises the bar by two. The actor rolls their own pool against that number;
equal or higher wins, ties to the actor, as with an attack.

Named passives, computed once and reused:

* **Passive Perception** = 4 + (Wit + Perception dice) x 2 + pips. Beaten by a
  Stealth or Con roll. Use the highest among several watchers.
* **Passive Resolve** = 4 + (Wit + Charm dice) x 2 + pips. Beaten by Command,
  Negotiate, or Con in a social push, and the basis for the social conflict
  track.

Objects and systems carry a Passive rating of their own, a lock's or a node's
**security**, set on the same scale. Passive Defense (Section 6) is the
combat-specific version with its own formula, not this one. When both sides are
genuinely acting at once, a chase or a shoving match, a Game Master may instead
have each roll and compare totals; the static passive is the default because it
is faster and needs no second roll.

### Extended Tasks

Some jobs are not one roll: slicing a hardened node, defusing a bomb, a vehicle
chase, a ritual. Run these as an extended task with a **clock** of a few
segments, four for a stiff job, six for a grim one. Each fitting action that
succeeds fills a segment, and a wide success may fill two; a Glitch or a failure
costs a segment or advances an opposing clock, the alarm rising, the pursuer
closing, the timer counting down. The task completes when the clock fills. This
is where Drive, Hacking, and Demolitions earn their length, and where the
descriptive scales (Section 15) narrate progress without naming the count.

### The Wild Die

One die in every pool is the Wild Die, ideally a distinct color at a table
and simply the first rolled die on the server. It does not explode and it
does not remove dice. It adds a narrative event instead.

* **2 to 5** normal. Add face value to the total.
* **6 Tactical Edge** add 6 to the total, then apply one random Edge.
* **1 Glitch** the die still contributes 1 to the total, so the action can
  still succeed, then apply one random Glitch.

This keeps totals bounded, which is why the defense cap and the TN ladder
stay meaningful. The Edge and Glitch tables carry the drama that exploding
dice would otherwise provide, so keep them rich.

**Glitch table (Wild Die shows 1)**

1. **Exposed Defense** your Passive Defense drops by 2 until your next turn.
2. **Thermal Spike** cyber or magic feedback deals 2 direct, un-soaked Body damage.
3. **Jammed / Channel Bleed** weapon or spell stalls, take -1D on your next attack.
4. **Tactical Slip** the target gains +1D on their next attack against you.

**Edge table (Wild Die shows 6)**

1. **Armor Gap** this hit ignores the target's Soak.
2. **Tactical Opening** the target's Passive Defense drops by 3 until their next turn.
3. **Overcharge** deal +1D extra damage on this hit.
4. **Adrenal Rush** recover 2 Body Points.

---

## 6. Combat

There are no active defense rolls. An attacker rolls a skill against a
static Passive Defense, then a separate damage roll is checked against a
static Soak.

### Initiative

Roll a Wit pool at the start of combat. Act from highest to lowest. Some
maneuvers override this.

### Surprise

A fight can open with one side unready. A combatant who reaches the enemy
undetected, its Stealth having beaten their Passive Perception (Section 5),
acts in a **surprise round** before initiative is rolled: one action against
foes still unaware. The surprised take no action and no reaction that round and
count as Exposed, -2 Passive Defense, to the ambushers. Once it resolves, roll
initiative and fight as normal. If neither side is caught out, skip straight to
initiative.

### Actions and Reactions

On your turn you take one action: an attack, a maneuver, a support action, or
a move. You also have one reaction, an out-of-turn response to another
combatant, used by abilities such as Riposte and Emergency Defibrillator. Your
reaction refreshes at the start of your turn, so you hold at most one between
turns.

Unspent, your reaction defaults to a free Strike against any foe that leaves
melee with you. You may instead ready a specific reaction, dedicating that one
reaction to its trigger: Riposte, Empathic Aegis, or a plain guard, the
baseline defensive brace that grants +2 Passive Defense against the next attack
before your next turn. Passive reactions such as Emergency Defibrillator need
no readying and fire on their own.

### Passive Defense

    Passive Defense TN = (Agility dice x 2) + Agility pips + Hook bonus + cover
    Maximum: 20

Agility is capped at a x2 contribution so it does not become the single
mandatory stat. The Hook and cover carry real weight, which lets a Might or
Charm build survive without maxing Agility.

Against a target in the open, an attacker's larger pool means most attacks
land, and that is deliberate. Combat pools run to five dice while a bare
Passive Defense sits near ten, so a firefight moves instead of stalling on
whiffed rolls. Passive Defense is not a dodge that makes you hard to hit; it is
the edge that position and equipment buy. A bare human is easy to hit. A Hook,
a piece of cover, a mod or gear an admin adds, and moving under fire each shave
the odds a little, and stacked toward the cap of 20 they turn a sure thing into
a real contest. Survival rests more on Soak, cover, and Body Points than on
avoiding the hit.

Cover is a strict three-tier integer, not a free-form number. This keeps it a
single value in the sheet that commands can toggle, and it feeds straight
into the hit calculation.

    0  Out in the open, no bonus
    2  Light cover or concealment, for example vector fields, smoke, neon
    4  Heavy cover, for example concrete barricades, armored bulkheads

Commands like kneel or hide set this value directly. The Cover support action
below is the round-by-round way to raise an ally's cover.

### Defensive Gear

Beyond the Hook and cover, worn gear can add a small, permanent bump to Passive
Defense. Each piece is expensive on purpose, so buying defense is a real money
sink and a real choice against offense and recovery. A bonus adds into the
Passive Defense formula and still sits under the cap of 20, so stacking gear,
cover, and a Hook has a ceiling. Cyberware that raises Passive Defense is in
Section 11; this is the gear anyone can buy and wear.

| Gear | Passive Defense | Price | Slot and note |
| --- | --- | --- | --- |
| Ablative Harness | +1 | 400 | torso, sheds a plate on a heavy hit and needs re-plating |
| Reflex Weave | +1 | 700 | a worn underlay, always on, no upkeep |
| Kinetic Deflectors | +1 | 900 | worn rig, also grants +1 Soak against a single hit once per scene |
| Vector Field Projector | +2 | 1800 | worn device, the field is the Light Cover from the table above made portable, and it needs a charged cell to run |

Armor still sets Soak, not Passive Defense; these are a separate layer. A
character can wear armor for Soak and one defensive piece for Passive Defense,
but two defensive pieces do not stack, only the best applies, the same rule as
armor. Passive Defense cyberware (Section 11) is internal and adds on top of the
best worn piece, the way Sub-Dermal Plating adds to Soak.

### Attack

Roll `governing attribute + skill + pips + modifiers`. Compare to the
target's Passive Defense TN. A total equal or higher is a hit.

The governing attribute is set by the skill, for example Firearms and Melee
use Agility, Heavy Weapons and Brawl use Might, Spellcasting uses Wit.

### Range and Movement

Position is tracked in zones and range bands, not measured distance, which
suits both the table and the room-based MUD.

A **zone** is one region of a fight: a room, a stretch of corridor, a named
piece of terrain. Each combatant is in exactly one zone, and zones are adjacent
or not, usually a line or a loop. Sharing a zone with a foe means you can close
to melee with it.

The gap to a given target is one of three **bands**:

    Engaged   in melee, in contact
    Short     the same zone, out of melee
    Long      an adjacent zone, in sight

Reach depends on the weapon (see the ratings below):

* A **melee** weapon or bare hand reaches an **Engaged** foe.
* A **short** weapon reaches **Engaged or Short**.
* A **long** weapon reaches **any band in sight**.
* A shot one band past a weapon's reach is a **long shot at -1D**, and two
  bands past is out of reach. Line of sight is the Game Master's call. A melee
  attacker sharing a zone ignores the cover that zone offers.
* An **area** effect catches everyone in one zone.

Moving is your action. One Move shifts you one band toward or away from a
target, or one zone along an adjacency. There is no Speed stat and no faster
charge: crossing from Long to Engaged is two Moves, two turns spent running
rather than attacking. That is what makes a firing line dangerous and a charge
a real gamble.

A combatant who spent its action to Move is harder to hit at range, +2 Passive
Defense against ranged attacks until its next turn. Closing under fire is often
safer than standing in the open.

Leaving an Engaged foe lets that foe spend its reaction on one free Strike
against you. Spend your action to **Disengage** instead and you pull back
untouched. Even then, a foe of equal or higher initiative can spend its own
turn to close again, so breaking contact cleanly means outpacing it or putting
a blocked zone between you.

### Frontage

A zone has no grid to pin bodies, but they still crowd. At most **two**
attackers can press one target in melee at once. A third must wait a turn, turn
on another foe, or shoot. The cap is per target, so a mob has to spread along
your line instead of ganging one character. A **Large** creature or bigger
(Section 15) can be ringed by up to **four**.

### Damage and Soak

Weapons carry a damage rating in dice and pips, rolled like any pool.

    Soak TN     = (Might dice x 2) + Might pips + armor bonus
    Net Damage  = max(0, damage roll - Soak TN)

Sample weapons, with damage rating and range band:

| Weapon | Damage | Range |
| --- | --- | --- |
| Unarmed | 2D | engaged |
| Knife | 3D | engaged, or thrown to short |
| Katana | 4D+1 | engaged |
| Pistol | 4D | short |
| SMG | 5D | short |
| Rifle | 5D+2 | long |
| Heavy | 6D | long |

Two rolls resolve an attack. The first, the attack pool against Passive
Defense, decides hit or miss. The second, the damage rating against Soak,
decides how much lands. Both use the same dice engine.

### Ammunition

A weapon is **loaded** or **depleted**, and the two play modes track that
differently.

At the table, do not count bullets. Play as loaded, and at the end of a scene
roll one d6 for each weapon fired; on a 1 or 2 it runs depleted and needs
reloading from supplies before it fires again. The Game Master shifts the odds
for a weapon barely used or emptied in a long firefight. That is the whole
economy, a pressure rather than a tally.

A MUD counts every shot instead, which it can do precisely. The shape is
simple: a weapon draws from ammo it holds directly or from a clip, pack, or
module loaded into it, and firing depletes one unit or charge. The mapping
carries the details.

### Anatomy, Weapons, and Armor

Bodies vary in this setting. A character may have any number of hands, arms,
legs, or heads, from a floating droid with none to a many-armed frame. That
changes what a character can hold and wear, not how many actions they take. A
turn is a turn no matter how many arms swing it, so extra limbs never grant
extra attacks. A weapon is used from the hand, or other manipulator, that
strikes with it, and an empty one is Unarmed 2D.

Armor is worn on the body and does not stack. Your armor bonus to Soak is the
single best worn piece, not the sum of everything layered on, plus any
cyberware such as Sub-Dermal Plating, which is internal and adds on top.

A creature's size (Section 15) further shifts its Passive Defense, Soak, and
Max BP. Standard humans are size 0, where the formulas above stand unmodified.

### Encumbrance

Body slots limit what you wear and hold; weight limits how much you can carry
and still move. Your **carry rating** is (Might dice x 2) + Might pips in load
units, and gear, loot, and a full pack count against it.

* Over your carry rating you are **encumbered**: travel out of combat is
  halved, and in a fight you are Slowed (Section 7), a band or zone costing two
  Moves.
* Past twice your carry rating you cannot move at all until you shed weight.
  You can still act, fight, and pick things up; you simply cannot leave the
  room.

Item weights, like prices, are set per item and tunable.

### Scale and Mobs

Each row of the Section 15 Size table is one **scale tier**. A gap of one tier
is just the size modifiers there: a Large mech is easier to hit and harder to
drop than a person, and normal combat handles it. A gap of two or more tiers
stops being a matter of numbers and turns categorical, so a person never grinds
down a Massive war-frame one pistol shot at a time, and the war-frame never
rolls to swat the person.

Across a gap of two tiers or more:

* **Striking up**, at something far larger, an ordinary attack deals no Net
  Damage; the bulk shrugs it off. Only a heavy or explosive weapon, an area
  effect, a called shot at a real weak point (Game Master's call, at -1D), or a
  Mob can hurt it.
* **Striking down**, at something far smaller, the blow lands like an area
  hazard, not a single attack. Every smaller creature caught makes a **reflex
  save**, Agility + Athletics vs Hard (14), or Formidable (18) across a
  three-tier gap. A failure takes the full, maximum damage, enough to Down a
  Standard target outright; a success takes half; a save that rolls a Tactical
  Edge dives clear for none. The save is a reflex and does not spend your one
  reaction (Section 6); it parallels the Downed dying roll (Section 7), the
  rare moment a defender rolls, and it resolves a giant sweeping a whole group
  in a single step.

A **Mob** is a crowd of similar creatures, a gang swarming, a drone flock, a
press of ghouls, fought as one combatant: one initiative, one action a turn,
one profile drawn from a single member. Numbers raise its **effective scale**:

    pack     about 6      +1 tier
    swarm    about 20     +2 tiers
    horde    about 60     +3 tiers

That is how the small threaten the large. Enough bodies climb the ladder until
they reach a target no single member could scratch. A Mob's attack gains **+1D
per tier it has climbed** and the **area** property: it strikes everyone
Engaged with it in its zone at once, ignoring the frontage cap and any cover
the zone offers. A mob does not duel, it swamps. Its climbed scale only reaches
upward: against foes of its members' own tier or smaller a Mob attacks
normally, rolling damage with the bonus dice and area rather than the
striking-down auto-maximum, so a swarm is deadly but does not instantly Down
everyone it touches.

Hurting a Mob runs the rule in reverse. A single-target attack thins it by a
sliver and never brings the whole down, so against a swarm one gun is nearly
useless. Area and explosive attacks are the answer, striking the whole Mob and
knocking it down a band as it thins, horde to swarm to pack to gone. A Mob that
falls below pack size breaks into individuals or routs.

### Support Actions

Two baseline actions give Wit and Charm builds a round-by-round combat role
without spamming a damage skill. Both cost one action and are available to
every character, with no maneuver unlock and no Grit cost.

* **Cover** roll Wit + Perception vs Moderate (10). On success an ally gains
  +2 Passive Defense until their next turn, the Light Cover value from the
  table above. It does not stack past the Heavy cover value.
* **Command** roll Charm + Command vs Moderate (10). On success restore 1
  Grit to an ally. This is the Rally action referenced in Section 8, stated
  here as a concrete roll and effect.

Both cost the actor's whole action, so the support role trades personal
damage for keeping an ally alive or funded.

---

## 7. Body Points and Wounds

Two tracks with distinct jobs.

**Body Points (BP)** are the fast, granular buffer. Stamina, near misses,
and superficial hits. They recover quickly.

    Max BP = 12 + (Might dice x 3)

**Wounds** are serious trauma on a short ladder. They recover slowly.

    Wounded -1D  ->  Badly Wounded -2D  ->  Critical -3D  ->  Downed

The dice penalty applies to all actions and stacks with the ladder.

### Resolving a hit

1. Compute Net Damage and subtract it from BP.
2. If that single hit's Net Damage is at least half of Max BP, mark one
   Wound. Big hits wound, chip damage does not.
3. If BP reaches 0, the character is Downed regardless of Wound count.
   Excess damage is ignored.
4. A defender's own Wild Die 1 can trigger a Glitch, and certain Edges
   (Armor Gap, Overcharge) make a hit far more likely to wound.

### Downed and Dying

A Downed character is unconscious and bleeding out, with three rounds before
the bleed turns fatal. Once on each of those rounds it may cling to life,
rolling a single die and adding its Might dice as a flat bonus, +2 at Might 2D,
rather than a full pool.

    Cling to life = 1d6 + Might dice, 6 or higher stabilizes

The flat bonus is the point. A whole extra pool would make a tough character
untouchable and a frail one all but certain to die; a flat +1 per Might die
keeps both in real jeopardy without either extreme. Stabilizing stops the
worsening and leaves the character unconscious; a natural 6 brings it to at 1
Body Point. If none of the three rolls reaches 6, the character dies. An ally
who reaches the body ends the dying outright, spending an action or making a
Medicine or Cybertech check, with no roll.

This three-round window is the main lethality control, and reaching a fallen
ally is the reliable save. A Downed character is rescuable, not deleted.

### Death and Recovery

At the table, Dead is permanent. A character whose three-round dying window
closes uncured is gone and the player rolls a new one. The dying window above is
the chance to prevent that, not to undo it, which is what keeps a rescuable
Downed state from making death toothless.

A persistent MUD cannot retire a player's character on one bad night, so it
plays death as a setback instead of an end: a fallen character comes back
through allies who reach the body, paid recovery services, or an automatic
respawn after a set span. Which model is in force is a tuning choice; a table
can adopt the recovery version, and a MUD can switch permadeath on.

### Recovery

* **BP** short rest, the Biomedical Injector, the Adrenal Rush Edge. Fast.
  Stims restore Grit, not BP (Section 8).
* **Wounds** downtime days plus a Medicine or Cybertech check plus supplies.
  Slow and expensive.

A **short rest** is the automatic Body Point recovery. Two hours of in-game
time in safe rooms without strenuous activity, no fighting and no step into an
unsafe room, restores Body Points in full. A room is flagged safe or not, so a
character can wander an inn, a residence, or a secured stretch of a dungeon and
still rest, but crossing into unsafe ground resets the clock. Recovery is
pro-rated across the period, so a partial rest returns a partial share. Because
a MUD already tracks time precisely and play is full of quiet stretches between
fights, this needs no rest command: idle recovery just happens, and a
character's Body Points climb while the player chats, trades, or plans. Grit
trickles back over the same rest more slowly (Section 8); Wounds do not, they
need downtime days.

### Conditions

Beyond Wounds, a hit or a hazard can impose a temporary condition. Each states
what it does and when it ends; unless noted, a condition ends at the start of
your next turn.

* **Stunned** you take no action on your next turn. A reaction is still
  allowed. Ends after that turn.
* **Exposed** your Passive Defense drops by 2. This is the Exposed Defense
  Glitch, named for reuse.
* **Suppressed** your attacks take -1D while you stay in the beaten area.
* **Prone** your attacks take -1D, melee attacks against you are +1D, and
  ranged attacks against you are -1D. Stand up with a Move.
* **Blinded** your attacks take -2D and you cannot make attacks that need
  sight; anyone you cannot see ignores your cover. Ends when sight returns.
* **Shaken** your attacks and saves take -1D and you cannot move toward the
  source of your fear. Ends when you rally, an ally's Command or your own Wit
  roll vs Moderate (10) at the end of your turn.
* **Slowed** every band or zone of movement costs two Moves instead of one.
* **Held** you cannot move, entangled or grappled. Break free with a Might or
  Agility roll vs Moderate (10) as your action.
* **Ongoing** bleeding, burning, or corroding deals a set amount of direct,
  un-soaked damage at the start of each of your turns until you spend an action
  or make a save to end it.
* **Crash** the stim crash (Section 8). A stacking -1D on all actions, one per
  dose taken past your safe stim limit. Unlike most conditions it does not lapse
  at your next turn; it clears only on a full short rest.

Wounds (above) and Downed are the heavier conditions, each on its own track.

### NPCs and Mooks

Non-player creatures use the same sheet as characters, but few deserve a full
one. Stat them by **grade**, filling only what the grade needs.

* **Mook.** Fodder: a street tough, a guard drone, one of many. A mook has no
  wound track, and the first hit that deals any Net Damage Downs it. Give it an
  attack pool, a Passive Defense, a Soak, and a weapon; skip BP, Wounds, and
  Grit. Mooks are most dangerous massed, where they form a Mob (Section 6).
* **Tough.** A real opponent: an enforcer, a veteran, a predator. A Tough runs
  the full creature rules, Max BP, the Wound ladder, Grit, and any maneuvers,
  built from attributes and skills or from set numbers.
* **Elite.** A named threat or a boss, something that should not fall in a
  round. An Elite is a Tough with the numbers turned up, a full Grit pool for
  signature maneuvers, often a larger scale tier (Section 15), and one extra
  reaction each round so a lone boss is not swamped by a party's action economy.

**Quick profile.** You need not derive an NPC from attributes. A profile is
enough to run a fight: an attack pool in dice, Passive Defense, Soak, damage,
BP or the Mook tag, plus size and any special. A Tough built this way plays
exactly like one built from a sheet, because the engine reads only the derived
numbers.

### Fear Attacks

The supernatural does not only wound the body. Once per encounter a
supernatural creature may loose a **fear attack** as a bonus action, on top of
its turn: a short-range dread that catches everyone nearby. Each target saves,
Wit + Charm against the creature's rating, Hard or Formidable by its potency,
the same shape as the scale reflex save (Section 6), a Tactical Edge for
nothing and a success for half.

Here the damage is **Grit**, the drain on nerve and focus. A failed save loses
the full rated Grit and inflicts the creature's fear condition; a success loses
half and no condition; an Edge shrugs it off. A target without the Grit to pay
takes the condition anyway, the dread getting through where composure has run
dry.

Every supernatural creature carries one fear condition as its signature,
Shaken by default. A given kind of horror always breaks minds the same way, so
a handful of conditions cover most of them, and the rarest and worst things
bring conditions of their own that a Game Master reveals in play.

---

## 8. Grit and the Three-Currency Rule

Design rule: every ability costs at least one of an action, player time, or
a resource. Nothing is free and repeatable.

**Grit** is the managed resource, one pool covering nerve, focus, and mana.

    Max Grit = 3 + Wit dice + Charm dice

Uses:

* **Push a roll** spend 1 Grit for +1D, to a maximum of +2D per check. This
  is what reins in an over-leveled character. The TN may be trivial but your
  Grit reserve is not.
* **Maneuvers** cost 1 to 3 Grit each, replacing per-encounter flags with
  one economy.
* **Spellcasting** draws Grit as mana.

Grit is retained between scenes. It does not refill on its own when a fight
starts, so keeping it topped up is an ongoing cost. A scene is a single
encounter or one continuous situation. Downtime is the longer gap between
scenes, where Wounds heal (Section 7).

Recovery routes through support play and the item economy:

* An ally's **Command / Rally** action refunds Grit mid-scene. This is the
  Command support action in Section 6.
* Stims and similar consumables restore Grit in combat. At 75 creds each
  (Section 4) they steadily drain a character's funds, the main Grit
  sink-and-source loop and the game's primary money sink.
* Rest and meditation in downtime restore Grit slowly and for free.

**Stims and the crash.** A stim restores 2 Grit but taxes the body, and the
body only takes so much before the payment comes due. Your **safe stim limit**
between rests is your **Might dice**, two doses at Might 2D, three at 3D. Doses
up to that limit are clean.

Each dose past the safe limit still restores its Grit, the desperate top-up in
a bad fight, but inflicts **Crash** (Section 7): a stacking -1D on all actions,
one more with each dose over the line, that clears only on a full short rest,
the same rest that resets the safe limit. So a character can push past the
limit when a fight demands it, at a mounting cost that makes chain-dosing a
losing trade rather than a routine. The limit and the crash together cap what
raw money can buy in a single stretch, which keeps the stim sink from
trivializing Grit management. Cyberware can raise the safe limit or soften the
crash (Section 11).

Grit persists and much of a character's recovery gear spends it. The
Biomedical Injector, Mana Ward, and the wound-clearing Biomantic Knit in
Section 11 all draw on Grit, so a character who burns their pool hard in one
fight walks into the next one short on both offense and healing. That is the
intended pressure. Grit is a managed reserve, not a per-scene allowance, and
topping it back up is what drains a character's money.

This is the structural answer to combat attributes being stronger. The
gunfighter needs the medic and the fixer between fights, and needs money to
keep the reserve full.

Grit is a spend-down pool. If you prefer a rising Stress meter that causes a
breakdown at maximum, it is the same integer inverted. Spend-down is
recommended for simpler refusal logic.

---

## 9. Progression

The Game Master awards 1 to 3 Character Points (CP) per session. Growth is
horizontal, wide and specialized, never by inflating a stat past the ladder.

**Attributes**

* Lifetime cap: 4D (12 pts).
* Cost to raise: current dice x 4 CP per point. Expensive on purpose.

**Skills**

* Lifetime cap: +3D over the governing attribute.
* Cost to raise: current dice in that skill, CP per point.
* A brand new skill's first point costs 1 CP, which keeps breadth cheap.

**Maneuvers, cyberware, and spells**

* 5 CP each, a permanent unlock. See Section 11.

Being the best hacker in the city is a large pile of skill points, not a 6D
Wit.

**Pay and Rewards**

Alongside CP, a session pays in creds. First-pass numbers, to be tuned by play
and simulation:

    odd job or favor     100 to 400 creds
    solid contract       400 to 1500 creds
    major score          2000+ creds, split across the crew

Set against the costs, a stim is 75 creds, so refilling Grit between fights
runs a few hundred over a session and more if the safe limit pushes a crew onto
downtime rest (Section 8). Weapons and armor run 50 to 800, and defensive gear
reaches into the low thousands (Section 6). A working crew clears its stim and
gear upkeep on ordinary contracts and saves toward the big buys, a Vector Field
or a Heavy weapon, on a score. Loot, recovered gear, creds off a foe, a
sellable artifact, is bonus on top, priced by standing when fenced (Section 14).

---

## 10. Hooks

Pick one at creation.

| Hook | Passive Defense | Traits |
| --- | --- | --- |
| Cyber-Augmented | +6 | up to 2 cyber-graft slots |
| Awakened | +4 | can cast spells and channel mana |
| Street Savvy | +2 | starts with +2 CP and a contact |

The Awakened hook steps outside the mundane operator default the setting is
written around. A crew like Omega Team has no Awakened member and does not
know the option exists. Whether an Awakened character fits your table is the
Game Master's call, and picking it means starting already touched by what the
rest of the setting only whispers about.

---

## 11. CP Maneuvers, Cyberware, and Spells

Each option below is a permanent 5 CP unlock (Section 9). The per-use cost is
in Grit unless noted, paid on top of the action. Cyberware needs a free
cyber-graft slot, so only the Cyber-Augmented hook installs it, up to its
slot cap. Spells need the Awakened hook. Anyone can learn a plain combat
maneuver.

### Combat Maneuvers

| Maneuver | Cost | Effect |
| --- | --- | --- |
| Wired Reflexes | 3 Grit | act first this round, bypass initiative |
| Smartlink | 2 Grit | spend an action to aim, next attack ignores cover and lowers target Passive Defense by 2 |
| Riposte | 1 Grit | reaction, when an attacker misses your Passive Defense by 4 or more, make an immediate free strike for base weapon damage |
| Suppressive Fire | 1 Grit + action | cover a choke point or a short arc, any enemy who enters or acts there before your next turn is Suppressed, -1D on attacks (Section 7) |

### Cyberware

| Mod | Cost | Effect |
| --- | --- | --- |
| Sub-Dermal Plating | none, -1 Max Grit | permanent +2 Soak TN, the neural load permanently lowers Max Grit by 1 |
| Biomedical Injector | 1 Grit + action | roll Wit + Medicine vs Moderate (10), restore Body Points equal to the margin, capped at your Wit dice, to yourself or an ally in reach |
| Tactical Co-Processor | 2 Grit + action | roll Wit + Hacking or Perception vs the target's Passive Defense, on a hit the target's Passive Defense drops by 3 against all allies until your next turn |
| Dermal Wire Mesh | 1 Grit + action | for 3 rounds add your Wit dice to your Soak TN |
| Sub-Dermal Capacitor | none | passive, once per encounter, when an ally restores your Grit with Command, gain +1D on your next Firearms or Melee attack |
| Neural Link Coordinator | none | passive, whenever you restore Grit to an ally with a Charm skill, regain 1 Body Point |
| Emergency Defibrillator | reaction | when a hit would drop you to 0 Body Points, drop to 1 instead, then the mod burns out until a downtime Hard (14) Wit + Cybertech repair |
| Kinetic Absorbers | none, -1 Max Grit | passive, permanent +1 Passive Defense, subdermal buffers that read a blow coming; the neural load lowers Max Grit by 1 |
| Reflex Governor | 2 Grit + reaction | when you are hit, roll Wit + Cybertech vs the attack total, on a success reduce that hit's Net Damage by your Wit dice |
| Adrenal Regulator | none, -1 Max Grit | passive, raise your safe stim limit by 1 and ignore the first stack of Crash each rest (Section 8); the governor runs hot, lowering Max Grit by 1 |
| Threat-Assessment Optics | 1 Grit + action | mark a foe until your next turn, your attacks against it ignore its cover and the moved-target bonus |

### Sorcery

| Spell | Cost | Effect |
| --- | --- | --- |
| Mana Shield | 1 Grit per round | +3 Passive Defense while active |
| Mana Ward | 1 Grit + action | +3 Soak TN against standard weapons until end of scene, and negates one incoming Armor Gap Edge |
| Spell-Weave | +1 Grit | take -1D on a Spellcasting check to hit two targets |
| Static Shock | 1 Grit + action | roll Wit + Spellcasting vs Passive Defense for 4D damage, a Tactical Edge also leaves the target Stunned (Section 7) |
| Cognitive Siphon | 1 Grit + action | roll Wit + Spellcasting vs Passive Defense, deal no damage but drain 2 Grit from the target and give 1 to an ally |
| Aura of Command | 1 Grit + action | roll Charm + Command vs Moderate (10), up to two allies clear the Exposed Defense glitch |
| Empathic Aegis | 1 Grit | reaction, when an ally in sight is hit, add your Charm dice to their Soak TN for that hit |
| Mind Flicker | 1 Grit + action | roll Charm + Con vs the highest enemy Wit pool, on success your whole party counts as Light Cover, +2 Passive Defense, for 2 rounds |
| Biomantic Knit | 2 Grit + 2 Body Points, once per scene | roll Wit + Spellcasting vs Hard (14), success clears one Wound level on a touched target. This is the only way to shed a Wound mid-fight, and the cost and scene limit keep Section 7's slow wound recovery intact |
| Necrotic Tap | none, once per scene | passive, when an enemy in reach is Downed, roll Charm + Streetwise or Wit + Perception vs Moderate (10), success restores 2 Grit |

### Street and Tech

| Option | Cost | Effect |
| --- | --- | --- |
| Overclock | 3 Grit | force a device or node to short-circuit without a roll |
| Underworld Contacts | none | once per downtime, source illegal gear or a safehouse |

---

## 12. Text Status Display

A single legible status line, reusing a bar renderer.

    HP [||||......] 9/21   GRIT 4/6   WOUNDS oo..  (Badly Wounded -2D)

While Downed:

    DOWNED - DYING   cling 6+   round 1 of 3

---

## 13. Tuning Knobs

Adjust these to change feel. Everything else follows.

| Knob | Default | Effect |
| --- | --- | --- |
| Points per die | 3 | granularity of pips |
| Creation attribute pool | 24 pts total | starting power |
| Creation attribute cap | 3D | starting spikiness |
| Skill pool | 12 pts | starting breadth |
| Creation unlock budget | 10 CP | starting maneuvers, cyberware, spells |
| Starting funds | 300 creds | starting wealth |
| TN ladder | 6 / 10 / 14 / 18 / 22 | difficulty |
| Task clock | 4 or 6 segments | length of an extended task |
| Passive Defense factor | Agility x2, cap 20 | hit rate |
| Frontage cap | 2, or 4 vs Large | melee attackers per target |
| Reactions per round | 1 | out-of-turn responses |
| Scale mismatch gap | 2 tiers | when scale turns categorical |
| Scale reflex save | Hard, Formidable at 3 tiers | dodging a much larger foe |
| Mob bands | 6 / 20 / 60 | pack / swarm / horde thresholds |
| Max BP base | 12 | bulk survivability |
| Soak factor | Might x2 | small-hit absorption |
| Carry rating | Might x2 | weight before encumbrance |
| Wound trigger | half Max BP | how easily hits wound |
| Dying bar and window | 6+, 3 rounds | size of the death window |
| Death | permadeath (table), recovery (MUD) | the stakes of dying |
| Session pay | 100 to 1500 typical | reward pacing |
| Stim price | 75 creds | strength of the Grit money sink |
| Stim Grit restore | 2 | how far one dose goes |
| Safe stim limit | Might dice per rest | clean doses before the crash |
| Short rest period | 2 in-game hours | Body Point recovery pace |
| Max Grit base | 3 | how many big plays before a refill |
| Max Resolve base | 12 | social stamina |
| Composure factor | Wit x2 | social pressure absorption |
| Attribute lifetime cap | 4D | long-term inflation ceiling |

---

## 14. Factions and Standing

ChromeSix has no alignment axis. What matters in Liminal Frontiers is who you
answer to and who wants you dead, so relationships are tracked as standing
with factions and disposition toward individuals.

### Faction Standing

Standing with a faction is a single signed integer on a seven-step band. It is
the persistent baseline for how that faction treats you.

    +3  Allied      trusted, given priority and protection
    +2  Friendly    welcomed, offered good work
    +1  Known       recognized and tolerated
     0  Neutral     a stranger, judged on the moment
    -1  Watched     distrusted and overcharged
    -2  Hostile     refused service, met with force when convenient
    -3  Hunted      killed or captured on sight

Standing moves in whole steps. Completing work for a faction, returning what
they lost, or spending real capital with them raises it. Robbing them, killing
their people, or helping a rival lowers it. A single job rarely moves standing
more than one step.

### What Standing Does

* **Prices.** Each step shifts prices by 10 percent. At +3 a vendor sells at
  70 percent, at -1 at 110 percent. At -2 or worse they will not deal.
* **Work.** Better jobs, safehouses, and introductions require a minimum
  standing. The best contracts are closed to strangers.
* **Hostility.** At -2 a faction's people move against you when the odds
  favor them. At -3 they move on sight.

### Individual Disposition

A specific NPC starts at their faction's standing and carries a personal
modifier from your own history with them, read on the same seven bands. A
corporate fixer whose faction hunts you may still deal with you because you
once saved his life. Charm skills (Command, Negotiate, Con) can shift a
disposition one or two bands for the length of a scene, but they do not change
the underlying standing. Standing is earned over time, not talked into.

---

## 15. Descriptive Scales

When the game describes a scene in prose, words read better than dice and
pips, and they are essential when a Game Master hides the mechanics from
players, the same mystery the Introduction protects. Each scale maps a value
to a set of words. Overlapping words are intentional, so one value can be
described more than one way for variety.

### Difficulty

| Target Number | Words |
| --- | --- |
| 6 or less | trivial, simple, routine |
| 7 to 10 | straightforward, fair, a real try |
| 11 to 14 | tricky, hard, demanding |
| 15 to 18 | formidable, daunting, severe |
| 19 to 22 | heroic, forbidding |
| 23 or more | legendary, all but impossible |

### Condition

Pick a word from the character's remaining Body Points, then add a wound
descriptor if any Wound is marked.

| Body Points remaining | Words |
| --- | --- |
| full | unhurt, fresh |
| 75 to 99 percent | scuffed, grazed |
| 50 to 74 percent | bloodied, winded |
| 25 to 49 percent | badly hurt, staggering |
| 1 to 24 percent | barely standing, failing |
| 0 | down, flatlined |

| Wound level | Added descriptor |
| --- | --- |
| Wounded | favoring an injury |
| Badly Wounded | visibly wounded, slowing |
| Critical | crippled, moving wrong |

### Time and Duration

A combat round is about five seconds. Vague durations map to ranges so the
server can resolve them.

| Words | Range |
| --- | --- |
| a moment, an instant | 1 round, about 5 seconds |
| briefly, a few rounds | 2 to 6 rounds, up to half a minute |
| a minute, a short spell | about 12 rounds, 1 to 2 minutes |
| several minutes | 2 to 10 minutes |
| a while, a short while | 10 to 60 minutes |
| hours | 2 to 8 hours |
| a shift, most of a day | 8 to 24 hours |
| days, downtime | 2 or more days |

### Relative Height

The game avoids imperial and metric units in prose and measures height against
the body instead. Height also sets cover when a barrier stands between attacker
and target, feeding the tiers in Section 6.

| Words | Cover |
| --- | --- |
| ankle-high, knee-high | none (0) |
| waist-high | light cover (2) |
| chest-high | heavy cover (4) |
| head-high or taller | blocks line of sight |

### Size

Size is a single step relative to a standard adult human. It changes how hard
a creature is to hit, and larger creatures are tougher. Body totals floor at 1.

| Size | Passive Defense | Body and Soak | Examples |
| --- | --- | --- | --- |
| Minuscule | +4 | -12 BP | insect drone, rat |
| Small | +2 | -6 BP | child, dog, courier drone |
| Standard | 0 | baseline | adult human |
| Large | -2 | +6 BP, +1 Soak | riding beast, light mech |
| Huge | -4 | +12 BP, +2 Soak | ground vehicle, battle mech |
| Massive | -6 | +18 BP, +3 Soak | dropship, building-scale |

Size modifiers to Passive Defense stack with cover and the Hook, still under
the defense cap in Section 6.

---

## 16. Social Conflict

When talk is the fight, ChromeSix runs it on the combat engine with different
labels, so a Face threatens a room the way a shooter threatens a corridor.

**Resolve** is the social buffer, the will to hold a position, and it works
like Body Points.

    Max Resolve = 12 + (Charm dice x 3)

**Composure** is the social Soak, the poise that shrugs off pressure.

    Composure = (Wit dice x 2) + Wit pips

A social exchange resolves in two rolls, like an attack:

* **The push.** Roll Charm and the fitting skill, Command to browbeat,
  Negotiate to bargain, Con to deceive, Performance to move a crowd, against the
  target's Passive Resolve (Section 5). Equal or higher lands.
* **The pressure.** On a hit, roll the leverage as a pool, weak 2D, ordinary
  3D, strong 4D and up as the stakes and evidence warrant, against Composure.
  The remainder is Resolve lost.

At 0 Resolve the target **yields** on the matter at hand: it agrees, folds,
talks, or backs down for the scene. This is a concession, not mind control. A
character never yields to something that betrays who they are, and the Game
Master may cap what one exchange can win. Resolve recovers like Body Points,
over a short rest of calm.

Pushing a roll, Grit, and the Charm maneuvers all apply, and this is where a
high-Charm, high-Wit build spends its weight, the other half of the balance the
combat attributes never touch. A lasting outcome, a won ally or a made enemy,
is written as a shift in standing or disposition (Section 14).
