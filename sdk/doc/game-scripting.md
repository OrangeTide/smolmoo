# Game Scripting Language: Design Brief

Working notes for an in-world scripting language that non-technical
builders use to create content in a cooperative graphical MMORPG. This
brief collects goals, requirements, non-goals, prior art, and the open
design forks. It is a living document; sections marked **OPEN** are
unresolved.

## Review reconciliation (target-server maintainer, 2026-07-10)

The maintainer of the target RPG server (`aldeby` / demon-engine) reviewed
this brief against the shipped server: server-authoritative, a fixed 30 Hz
tick, a shared-C `sim.c` movement core, client prediction, netchan/microser
wire. The review's decisions (D1-D5) supersede this brief's implicit
premises where they differ; the rest stays design-fork reference. Reconciled
with our own constraints:

- **D1 / D3: near-term rules are generic C; the language layer is deferred.**
  The RPG system is written in portable C so one source compiles into the
  native client and server today and can later recompile to a VM. Excelsior,
  the two-VM model, and code-as-data are the *deferred in-world authoring
  path*, built if/when live-code authoring lands. Refinement to D3: **Lua and
  JavaScript are not candidates.** Dynamic scripting makes unit testing,
  interactive debugging, and coverage hard, and lets a slight input change
  reach a path holding a syntax error that a compiled language would have
  caught. A static, compiled authoring language is a *requirement*, which is
  Excelsior's lane; the realistic candidates are Excelsior and a bespoke VM,
  not a dynamic language.
- **WASM cost is mitigated by a parallel backend, not a nested VM.** The
  review flagged a ColdFire VM nested in a browser-WASM client as an
  unmeasured cost. The intended answer is not to nest a VM but to compile
  Excelsior *directly to both ColdFire and WASM* from one source, treating
  the two as equivalent compatible targets: a ColdFire server and a WASM
  client speak the same wire protocol generated from the same code. That WASM
  backend is skjegg work, out of scope for Excelsior itself, but it is the
  retargetable-compiler payoff and it neutralizes the nesting cost. Excelsior
  already compiles to ColdFire (it forks MooScript), so IR to ColdFire is the
  fastest path to a working compiler; WASM is the parallel target to add.
- **D2: engine/content split confirmed.** demon-engine is the engine; the
  game is content. In-world geometry editing lands first, live-code authoring
  later.
- **D4: decoupled schedulers.** The fixed 30 Hz movement tick is the
  heartbeat and the source of movement determinism (fixed `dt`). The PQ
  carries sparse game-logic events, decoupled from the frame: it may run at a
  finer resolution (microsecond timestamps if capturable) and is
  batch-dequeued every N frames. A backlog is a load average; late events,
  even ones that miss their 30 Hz tick, are expected, not an error. The two
  schedulers compose; the durative-effect "turn" is a game-logic tick, not
  the frame.
- **D5: no universal actor-per-object.** Plain world objects are data under
  native system code (a rock is 20-30 values under physics), not VM actors
  with verbs. Only entities that need scripted behavior carry logic, and even
  those do not automatically get a per-object suspended VM context. The
  reachability permission discipline is kept but scoped to the scripted-logic
  layer and enforced by how the logic-tier host exposes objects, not riding
  "the actor model gives it for free."

## The game

A cooperative graphical MMORPG, MOO-like in scope but 3D. Builders
create quests, spell effects, items, dungeon puzzles, and RPG mechanic
tweaks. Content is authored in-world through a built-in UI and text
editor, in the style of Second Life. The environment borrows from
Smalltalk: class/hierarchy browsing is part of the UI, and a builder can
click an object in the world to find its attached scripts and its place
in the hierarchy.

### Builder personas

Builders come from varied backgrounds and work in their area of
expertise. Three rough tiers:

1. **Map and object layers.** Lay down maps and objects; attach or
   lightly modify existing scripts and templates. Mostly parameters and
   wiring, little or no free-form code.
2. **Story and dialog writers.** Author quests, dialogue, and branching.
   Intermediate understanding, at the level of a web designer who knows
   HTML but is not a JavaScript programmer.
3. **Mechanics experts.** Dig into formulas, statistics, combat rules,
   and spell effects. Comfortable with real programming.

## Runtime and distribution

The runtime is the ColdFire VM. Each server Shard offers a set of VM
binaries that the client runs to keep the game in sync and to drive
client-side prediction. Rules travel with the content: a shard can
evolve its mechanics without pushing a client update or requiring manual
mod installation. Distribution is automatic.

The backend is built around content-addressable storage. A git-like
directory object (htree) provides path-like names for hashed objects. An
incremental update system supports lazy evaluation and transfer of
content over a networked DHT, so a client pulls only the VM binaries for
content it actually encounters.

Consequences for the language:

- The ColdFire VM is the sandbox and the unit of distribution. Untrusted
  builder code is safe because it runs inside the VM, not in the host
  process. This matches skjegg's native-codegen model: builder source
  compiles through the shared IR to ColdFire binaries.
- Compiled scripts are content-addressed blobs. Every edit yields a new
  hash; htree path names stay stable across versions. This gives
  versioning, dedup, and rollback for free.

## Build cache and content addressing

Content addressing is used at the granularity that fits each consumer,
not one granularity everywhere.

- **Build cache (symbol granularity).** The compiler and linker keep a
  content-addressable cache, backed by smolvfs, keyed at
  `path/to/file::symbol` in the htree pointing to the hash of the compiled
  artifact. This is Unison's model: names are a metadata layer over
  hashes. It buys three things. Renames touch only the name-to-hash index,
  so nothing that depends on the symbol rebuilds. Editing a body changes
  only that blob's hash and forces rebuild of its transitive dependents,
  which is correct. Garbage collection marks from the live name table
  through dependency edges and sweeps unreferenced blobs, reaping old
  versions and dead symbols.
- **Runtime load and transfer (module granularity).** The VM loads
  pre-linked module or image blobs whole. It never assembles a program
  from loose per-symbol snippets at startup, which would require a runtime
  dynamic linker doing relocation and symbol resolution at load time. DHT
  transfer is at this coarse granularity too.

Deferred quoted fragments (see the code-as-data spectrum) are linked into
their owning object's module ahead of time. Their hash serves cache,
dedup, and transfer, not startup assembly.

## Goals

- A non-technical builder can write a small script after reading a
  one-page reference, with a usable mental model in minutes.
- Failure is safe: a bad script fails cleanly, never corrupts shared
  world state, never hangs the shard.
- Scripts are small and local. They attach behavior to one object, room,
  item, or quest and react to events.
- One shared substrate (object model, sandbox, distribution, sync)
  serves all three personas; only the authoring surface differs.

## Requirements

- **Sandboxed execution** with hard per-script resource caps: instruction
  or tick budget, memory arena size, wall-clock per event.
- **Server-authoritative execution.** The server owns RNG and truth;
  client-side prediction is an optimistic hint the server reconciles, not
  lockstep. Clients never run RNG (that would let a cheater delay and
  re-roll). Shared VM binaries buy prediction quality and patch-free rule
  updates, not client/server bit-identity. The determinism that remains is
  the server's own event ordering (the timestamped priority queue) and
  replay on the canonical VM, both already handled.
- **Forgiving surface**: minimal punctuation, inferred types where
  possible, readable keywords, and error messages phrased in the
  builder's terms that point at the mistake.
- **Host boundary is the only world access.** Scripts touch game state
  only through sanctioned host calls, as MooScript already does through
  its ~10 C callbacks.
- **A permission and ownership model** inside the sandbox, so one
  builder's script cannot grief another's objects in a shared world.

## Non-goals

- Not a general-purpose language. No file, network, or FFI access from
  scripts; no arbitrary imports; no threads. Event handlers are the
  concurrency model.
- No manual memory management, no pointers.
- Not maximally fast. "Fast enough for small per-event scripts" is the
  bar.
- No client-run RNG. The server owns all randomness.
- Not for large programs. A shard's rule set is the ceiling of what
  scripts may do.

## Prior art and the lessons taken

The systems below converge on one shape: an object with attached event
handlers, opaque engine handle types, a large library of host intrinsics,
compiled to a sandboxed and deterministic bytecode VM, with live in-world
editing. skjegg and MooScript already have most of that spine.

- **LambdaMOO.** The canonical in-world builder language. Its ownership
  plus programmer/wizard bits plus per-property and per-verb read/write
  flags are the model for the permission layer a cooperative world needs.
- **NWScript (NWN Aurora).** C-like, compiled to a stack-VM bytecode. The
  code unit is an event handler on an object (OnEnter, OnDeath, OnUsed,
  OnHeartbeat). Power lives in a large intrinsic library and opaque engine
  types (object, effect, location, itemproperty), not in language
  features. Non-professional modders shipped large content this way.
- **LSL (Second Life).** The closest match to the in-world editing
  experience. Its distinctive idea is the explicit state machine: an
  object's behavior is a set of states, each with event handlers. Opaque
  key (UUID) handles, strict per-script memory and instruction caps, time
  sliced across many objects. Millions of non-coders used it despite its
  quirks.
- **Inform and the z-machine.** The z-machine (1979) is the historical
  proof of this distribution model: a portable, deterministic bytecode VM
  small enough to ship a whole world to every client and run identically.
  The ColdFire-binaries-over-DHT approach is that idea plus
  content-addressing. Inform 7's natural language shows true
  non-programmers can author logic, but it is also a caution: I7 is harder
  to debug once off the happy path than the more conventional Inform 6.
  Lean toward structured, prose-flavored surfaces, not full natural
  language.
- **Logo.** Words and lists as the two aggregate types, very low
  punctuation (`[a b c]`, no commas or quotes on words), `run [ ... ]` to
  execute a list as code. The transferable bits are list-processing
  friendliness and minimal punctuation.
- **Scheme (TinScheme, in-tree).** Homoiconic: code and data share one
  form. Quasiquote and unquote build data with holes, which fits dialog
  templating with variable insertion.

## The language: Excelsior

The language is named **Excelsior**, a fork informed by LambdaMOO rather
than a MooScript revision. MooScript diverges from MOO's purpose and
architecture enough here (static typing, a coroutine turn model, capability
security, user-defined constructors, code-as-data) that keeping the MOO
name would misrepresent both. MooScript stays as-is for smolmoo; Excelsior
is a new front end that carries LambdaMOO's lessons without its conventions.

Source files use `.exs` for the body and `.exi` for the interface, mirroring
Ada's `.adb` and `.ads`. The `.exi` spec carries more than Ada's: under
option A's separate compilation it exports concrete signatures, the coarse
declared permissions, and the advisory manifest of what a script touches. So
an `.exi` is an Ada spec plus a permissions and disclosure record.

## Surface direction: code-as-data over a static-typed core

Keep a MooScript-like mechanics-tier surface, soften it for
approachability, and add code-as-data facilities so the same substrate
represents dialog and structured data. Borrow homoiconicity from Scheme
and the light list syntax from Logo. The syntax is Algol-family (roughly
Algol 68 expression syntax) with BASIC/Fortran block terminators (`endif`,
`endfor`, `endcase`).

### The Excelsior surface

- Keep static typing for atomic types (`int`, `float` stay untagged
  machine values, no unboxing or runtime tag checks). Allow local
  inference for locals where the type is unambiguous, so `var x = 5` is
  accepted and resolves to `int`. Annotate parameters when a body is
  ambiguous. See "Type strategy" below.
- Keep the readable block delimiters (`verb ... endverb`, `if ... endif`).
- Forgiving parse and error recovery, with messages aimed at the builder.
- Verbs stay fire-and-forget (void), which fits event handlers cleanly.
- Small vector and matrix value types, so 3D data passes cleanly. Heavy
  geometry (intersections, collision) is a hypervisor call to native host
  code, not VM work.

### Type strategy: static, monomorphic (option A, decided)

Excelsior is statically typed. Each verb resolves to one concrete
signature; local inference removes annotation noise where the type is
obvious, but there is no runtime tagging of atomic values and no
per-call-site polymorphism. `int` and `float` are plain machine values.

The one deliberate dynamic point is `prop`, smolmoo's property type whose
runtime type is not known at compile time. It is a compiler-level tagged
union, not hand-written boilerplate, and it preserves the part of
LambdaMOO worth keeping without adopting whole-language dynamic typing.

Because scripts are monomorphic and mostly annotated, adding parametric
polymorphism later (option C) would be additive and would not disturb
legacy scripts. It is a future possibility, not a v1 commitment. The
earlier idea of distributing generic bodies as IR blobs for on-demand
monomorphization is rejected: it implies a per-snippet runtime linker,
which we do not want (see "Build cache and content addressing").

### Data literals

A Logo-flavored list literal for structured data: dialog trees, stat
tables, item and template definitions. Low punctuation so the story and
map personas can read and edit it without programming fluency. Exact
syntax is OPEN; the intent is nested lists and records that the engine
walks as data.

### Quote and quasiquote

A friendly quote form so a builder can hand the engine a piece of behavior
or a template as data, and a quasiquote form for dialog and messages with
holes to fill (player name, item, count). This is the Scheme idea with a
gentler surface.

**Captures (decided).** A lazy fragment or quasiquote may carry a capture
list that bounds and optionally renames what it can see from the enclosing
scope. The list also bounds what a deferred fragment can reach. The rule follows C++ lambda capture lists for the explicit case
and Rust's always-inferred capture for the implicit case:

- **Unspecified:** the compiler infers the capture set from what the
  fragment names, and emits that inferred set as a visible manifest on the
  object, so nothing is hidden.
- **Specified:** the list is exhaustive. Only the named bindings are
  visible, and each may be renamed (expose internal `hp` as `health`).

Immediate holes (`${...}`) need no captures; the value is spliced at
construction. Captures matter only when evaluation is deferred.

### The synthesis: code-as-data is the htree

The important connection to the engine: content-addressable storage plus
the htree is already a code-as-data substrate. A quoted expression a
builder attaches to an object is, at runtime, a reference to a compiled
ColdFire fragment by hash. Homoiconicity at the authoring surface maps
onto a Merkle DAG of compiled fragments at runtime. A "quoted action"
stored on an object is a compiled verb blob addressed by hash, dispatched
by the host when the event fires.

This suggests a spectrum, from cheap and safe to powerful and risky:

1. **Compile-time quoted data literals.** Dialog trees, stat tables, item
   defs as literal nested data known at compile time. Compiles to static
   structures, no runtime eval, fully deterministic. Covers most of the
   value for builders.
2. **Quoted or deferred code fragments.** Store a condition or action on
   an object, run it later. Compiled ahead of time and linked into the
   owning object's module; the hash serves cache, dedup, and transfer, not
   startup assembly. No general interpreter required.
3. **Full runtime eval.** Construct new code at runtime and run it. Most
   powerful, needs an interpreter in the VM (TinScheme could serve),
   riskiest for determinism, performance, and security. Likely gated or a
   non-goal at first.

### Dialog runtime model (from the fetch-quest worked example)

A dialog is a static, content-addressable tree. Its dynamic parts are
compiled lambdas embedded by hash (the function pointer): a `when` guard is
`fn(ctx) -> bool`, a `${...}` hole is `fn(ctx) -> value`, a `do` action is
`fn(ctx) -> void`. A small generic walker interprets the static skeleton
and calls these fragments to test guards, fill holes, and run actions, so
the structure is fixed while the path taken is dynamic. This is NWN's
model, where a dialog node references its condition and action scripts by
resref. It stays at spectrum item 2 (precompiled fragments plus one
walker); no general eval in the VM.

This settles the immediate-versus-lazy question inside a runtime-scope
constructor like `dialog`: anything that touches the conversation context
is a call-at-visit lambda. The "immediate, fold to a literal now" case
applies only to compile-time constructors such as a stat table or grid.

Consequences:

- **Determinism is the callee's responsibility, not the tree's.** RNG
  such as `roll_d20` lives inside the called lambda and must draw from the
  authoritative seeded stream. Guards then split into two kinds.
  Client-predictable guards are pure over state the client already knows
  (`has(item)`, quest flag, faction rank); the client walker evaluates them
  locally for instant dialog UI. Server-authoritative guards depend on RNG
  or hidden state and must defer to the server. The engine likely marks
  which is which. This feeds the numeric and determinism fork.
- **A first-class callable type is implied.** `fn(ctx) -> T`, and `when`,
  `${...}`, and `do` are sugar for lambda literals that capture the ambient
  `ctx` plus their lexical captures, with the capture manifest rule
  unchanged. The concurrency model below later reversed the lean toward
  general first-class lambdas, since coroutines absorb the heavy uses
  (schedulers, reactions); the likely answer is that lambdas are restricted
  to embedding sites (dialog guards, quoted actions) or expressed as named
  verbs, keeping the rest of the language first-order. Revisit when the
  surface is sketched.

Ambient rule, settled: an event handler runs in the context of the object
the event is about, reached through `self` (an `on_death` handler runs with
`self` bound to the dying creature). Lexical captures stay plain names, so
there is no collision. Quest-state storage stays a host or schema decision
exposed through verbs, not baked into the language; `enum` and a compound
dialog action (`do a() then b`) earned their place.

## Concurrency and turn model

From the spell-effect worked example. The runtime models each running
verb or effect as a green-thread coroutine: a small ColdFire VM context
that runs, then suspends at a synchronization point (a channel send or
receive) and resumes later. This is LambdaMOO's own task model (`suspend`,
`fork`, tick budgets) rather than a new invention, and the capture and
resume mechanism is already plumbed into TinScheme and TinC continuations.

The consequence for the language is large. An effect's state lives on its
suspended stack, so there is no stored closure that outlives the turn and
nothing to garbage-collect. Callbacks, schedulers, and stored reactions
collapse into straight-line code.

Builders never write raw `spawn`, `select`, or channels, though. Those are
CSP primitives that deadlock and starve turns in non-expert hands, and an
earlier draft over-exposed them. They are demoted to runtime-internal. The
builder surface collapses to a single structured construct, the **durative
effect**, which covers every concurrency workflow that came up (duration
tick, buff enter/exit, delayed one-shot, event reaction, AI loop):

    effect burn(caster: obj, victim: obj, amount: int) for 3 turns
        each turn
            do damage(victim, amount, element.fire)
        on victim.died
            do explode(loc_of(victim), 3, caster)
            stop
    endeffect

Every clause is optional:

- `on start` / `on end` are enter and exit hooks (apply and remove a buff).
- `each turn` is the per-turn body (a damage-over-time tick, regen).
- `for N` / `while <cond>` bound the lifetime (a timed buff, an AI loop
  that runs `while alive(self)`).
- `on <object>.<event>` is an interrupt that reacts and may `stop` early.

The caster starts one per target with `apply`, in a plain `for` loop:

    verb cast_frostfire(caster: obj, center: loc)
        for t in entities_in(center, 6)
            if allied(caster, t)
                continue
            endif
            do damage(t, frost_damage(caster, t), element.frost)
            apply burn(caster, t, 8)
        endfor
    endverb

Two shorthands are degenerate durative effects: `after <delay> do ...` (a
delayed one-shot) and `when <object>.<event> do ...` (a one-shot reaction).

Safe by construction: the runtime owns the select over the turn tick and
subscribed events, so there are no builder-visible channels and thus no
blocking receive to deadlock on. Progress is structural, the `each turn`
body runs once and yields, the lifetime is bounded by `for`/`while`/`stop`,
and the per-task tick budget backstops intra-turn runaway. Underneath, an
effect is still a coroutine holding its state on its stack, scheduled by the
PQ and freeze/thaw-able. Only the sharp primitives are removed. This also
confirms the earlier reversal on first-class lambdas: the heavy uses
(schedulers, reactions) are gone.

Ordering is an architecture concern, not a language rule. The runtime
driver provides a single time-ordered priority queue (reusing `PQ_xxx`
from iox). Events carry a `(timestamp, tiebreak)` key. A player-initiated
event is stamped at creation, and a chained or delegated trigger inherits
its initiator's timestamp, so causally linked effects stay adjacent in the
queue. This is a discrete-event scheduler; determinism comes from the
queue, not from language semantics. Two nuances: equal timestamps need a
deterministic tiebreak, since inheritance makes ties common; and
simultaneity (the mutual kill) is a resolution rule that applies all
equal-timestamp events before processing the deaths they cause, so two
fighters can both land a killing blow.

The tiebreak is a non-crypto hash (adler32 or crc32) of a fixed byte
layout of `timestamp | objectid`, computed only when a tie occurs.
Hashing rather than comparing raw object ids means no object
systematically wins ties; different ids win at different moments, and it is
reproducible. Order on the triple `(timestamp, hash, objectid)` with the
raw id last, so a total order survives even when two ids hash-collide (a
32-bit hash can). The algorithm and byte order must be pinned so client and
server agree. This is load-bearing only for equal-timestamp effects that do
not commute; commutative ones the simultaneity rule already covers.

This is the "game OS" framing: the runtime driver is the kernel
(scheduler, event queue, actor dispatch, capability checks) and Excelsior
is what runs on top.

Tiering: the durative-effect construct is the mechanics-tier concurrency
surface; raw `spawn`, `select`, and channels stay runtime-internal. Story
and map builders never see even the effect construct; their code is
straight-line verbs and the dialog and menu constructors, which suspend
implicitly at turn or input boundaries in the style of MOO's `suspend`.
Input-driven interaction (a bank session, a conversation) is handled by
those constructors, not by builder-written concurrency.

Prior art: this is close to Pony (actor-per-object, message passing).
Pony's per-actor heaps and concurrent GC (ORCA) are the reference for the
granularity fork below. Keep Pony's two capability systems distinct,
though: its `iso/val/ref/box/tag/trn` are reference (deny) capabilities for
data-race freedom, separate from the object-capabilities Excelsior uses for
authority. Excelsior can likely skip the reference-capability machinery,
because immutable-by-default values (strings, lists are already immutable
and content-addressed) plus actor-owned mutable state give data-race
freedom by construction: sending immutable data between actors is safe.

## Interaction model: verbs are entry points, not the whole interaction

Not every interaction is a verb. A verb is an actor's entry-point message,
the boundary. The interaction behind it is the object's own protocol. You
send a bank-teller one verb (`talk`, or a MUD `tell`), and that spawns a
session coroutine that runs a dialog menu (graphical) or a tiny command
language (MUD) and holds the transaction state. Only the entry crosses the
verb boundary; the rest is straight-line coroutine code inside the teller
actor, the LSL state machine expressed as a coroutine.

This dissolves the encapsulated-service case that seemed to need "run as
owner." The teller session mutates the bank's own state, which is ambient
to that actor and needs no elevation, and the player's gold arrives as a
validated message the player sent by choosing to deposit, which the bank
actor accepts. No cross-owner privilege anywhere. The `.exi` lists the entry verbs and
their contracts; the session protocol is private `.exs` body. See "Client
and dialog architecture" for how a session drives the client GUI.

## Client and dialog architecture

Two VMs cooperate per player. Server-side objects hold authoritative game
logic. The client loads a runtime VM image during the early handshake (the
server requires it); this client VM is "the game" for that client. It acts
on player-object events proxied from the server and turns client input into
events sent to the player object on the server. The model is
server-authoritative (see deployment below).

**GUI.** The client has birdie-gui, a retained-mode C GUI library. The
client VM drives it through hypervisor calls to a fixed palette of supported
dialog types, refreshing a window's state or data graph as new data
arrives. The interchange format (binary blob, JSON, whatever) is a contract
between the client C code and the VM, not specified here.

**Sessions are conversations between two objects.** A merchant sends the
player object "open a trade with me." The exchange runs in three phases:

- *Initiation.* The player object replies OK, BUSY, or DENIED. This is the
  lightweight capability model in action: the target actor validates an
  incoming request against its own policy. BUSY comes from the one-dialog
  rule below; DENIED is policy (in combat, blocked player). The decision is
  proxied to the client VM, which knows its window state.
- *Data phase.* On OK, the merchant streams updates that populate the trade
  window, and client manipulation routes events back to the merchant. The
  player object is the hub: the merchant talks to the player object, never
  to the client directly, and the player object relays and may filter what
  reaches its client proxy.
- *Termination.* Either side ends it: the player accepts and the merchant
  confirms, the merchant cancels (the player walked out of range), or either
  rejects.

**Server authority (anti-cheat).** The client window is a view. The
authoritative session state lives between the two server-side objects, and
completion is a two-object confirm: only when both the player and the
merchant confirm does the server transfer items into inventory. The client
cannot fabricate the outcome, which is what stops trade scams and dupes.

**One dialog at a time.** At most one dialog is active per player, drawn
from the precanned palette; the rest stay dormant and are recalled and
populated on demand. A second request while one is open returns BUSY. This
keeps the client's window management trivial (no dynamic instancing, no
window stack) and the VM-to-GUI interface small.

**Relation to concurrency.** A session is a session coroutine, the
input-driven sibling of the durative effect: a body that processes exchange
events, `on <event>` interrupts (walked-out-of-range cancels), and a
completion. So sessions fold into the collapsed concurrency model. The
story-writer's dialog constructor is the data-driven session sugar (a dialog
tree pushed to a dialog window); a custom protocol like trade is a
mechanics-tier session.

Dialog-busy is client-authoritative, by architecture rather than choice.
The client owns its GUI, so it is the only thing that can present a dialog,
and it can always refuse with BUSY. The server cannot force the client to
honor a conversation it will not display, so a server-side mirror could not
be more authoritative, only wrong. The round-trip before the initiator
learns OK or BUSY is inherent and accepted. This is safe against a cheating
client, because presentation is not game state: a client that lies BUSY only
denies itself the interaction, while the trade outcome stays
server-authoritative. Authority follows who can honor it, the server over
game state, the client over presentation.

Deployment: server-authoritative, decided. The server is a library, not a
fixed process. It runs in two configurations: a dedicated server (a text
console application, server library only), and inside the graphical client
in host mode (the server library runs in-process with a client alongside it
in the same program). Either way there is a clear authoritative server, and
the client talks to it through the same event and message interface; only
the transport differs, in-process loopback in host mode, networked for a
dedicated server. So the two-VM model and the client-prediction code path
are identical whether the server is remote or co-located, and there is no
separate single-player codebase; host mode just has near-zero latency. This
suits the community model: a player runs the graphical client in host mode
for solo or a small shard, or a dedicated console server for a persistent
one, and the trusted shard operator runs the authority in each case. The
server-as-library is the unit that federates over SHOAL.

## Design decisions and open questions

### Object model: class-based, Strongtalk-flavored (decided)

The axis is class-based (Smalltalk, Java, C++) versus prototype-based
(Self, JavaScript, LambdaMOO), which is separate from the static-vs-live
axis. Smalltalk shows class-based can be fully live (class browser, runtime
edits), so class-based does not imply C++ rigidity. LambdaMOO is
prototype-based, so choosing class is a deliberate departure, justified by
choices already made:

- **Static typing (option A) wants a class.** A class is a static type:
  fixed field layout, known signatures, one `.exi`. A prototype's shape is
  per-object and mutable, which fights static typing. LambdaMOO is
  prototype *and* dynamically typed for this reason; the two go together.
- **The actor model wants self-state local.** Reading `self.x` must be
  cheap and ambient. Prototype delegation could put `self.x` on the parent
  object, turning a self-read into a cross-actor call. Class instances carry
  their own fields, local to their actor.
- **Migration is cleaner per-instance.** A class instance has its own full
  shape to version; a prototype instance's shape is partly its parent's, so
  a parent retype ripples through delegators.
- **Prototype-copy-on-mutable-slot converges to this anyway.** Copying all
  mutable slots on clone to avoid the collision-test footgun rebuilds class
  instances with extra steps.

The builder experience is preserved because it came from the live axis, not
the prototype axis: clone-and-tweak is Smalltalk `copy` or instantiate; a
one-off script on a single object is a per-instance verb override (a
singleton method); the class browser is the Smalltalk affordance directly.

The prior art for statically-typed, class-based, live is **Strongtalk** (a
statically-typed Smalltalk). That is the target: live class browser, runtime
subclassing, `copy` for clone-and-tweak, per-instance verb overrides for
one-off scripts, static types via the class.

Recovering the two reasons MOO used prototypes:

- **Less state per object.** Class-level defaults plus copy-on-write: an
  instance reads the class default until it writes, then gets its own slot.
  Per-instance storage is the diff, and freezing only the delta-from-defaults
  gives small dedup-friendly content-addressed blobs.
- **Revise a whole tree via the parent.** Adding a field to a class gives
  every instance and subclass that field; existing live instances pick it up
  through the automatic compatible-change migration (new field, default,
  lazy). A cheap automatic migration instead of zero-cost delegation.

**ColdFire implementation.** Because the field layout is static, an
instance's fields are the VM's global/static segment: `self.x` compiles to a
fixed global address, no object header, no delegation lookup. The actor is
that running VM. A small generated runtime that walks the known layout gives
freeze/thaw to JSON or a compact binary, so an actor is live (running VM) or
archived (serialized globals) interchangeably, and the delta-from-defaults
drops into the htree as a content-addressed blob. Migration operates on the
thawed shape.

This also pages idle actors out to storage, thawing on demand, which bounds
the resident footprint (see the concurrency granularity fork).

### Concurrency context granularity (OPEN)

The coroutine turn model (see "Concurrency and turn model") trades the
closure-lifetime question for a granularity one. Many small VM contexts,
one per effect or verb, versus heavier per-object-serialized contexts.
Per-object serialization buys the actor property: one task per object
means its state is touched by one thread, so no data races on its
properties and no locking, and it aligns with the permission boundary. The
cost is footprint: a suspended ColdFire context is a register file plus a
stack, and thousands add up, so the size of a suspended context is a real
design number to pin down.

Two things bound this. Freeze/thaw (see the object model) pages idle actors
to storage and thaws on demand, so only the active working set is resident,
not the whole population. And Pony's per-actor heaps with concurrent GC
(ORCA) are the reference for sizing and reclaiming the resident set. So the
open number is the resident working-set footprint, not the total.

### Numeric model: hardware float, plus fixed-point

Float determinism is dropped. The server is the single authority, so
cross-implementation bit-identity is not required, and same-machine replay
still reproduces because one implementation computes each value. Floats are
plain hardware IEEE 754 double, executed by translating ColdFire float ops
to the host FPU (SSE2/AVX) at native speed. No pinning, no reproducible
transcendental library, no soft-float apparatus.

Add a **fixed-point decimal type** for builders who want exact drift-free
stat math (no "99.9999 damage"). It is relatively simple and worth having
alongside the hardware double.

### Permission model: reference-reachability, not fine-grained metering

The VM sandbox stops a script escaping the machine; it does nothing about
one builder's script affecting another's objects. The collision worked
example (Toft's chest, Mara's curse, Rell's placement, a player opener)
settled the model. Two layers, kept separate:

**Authorship-ownership (edit time).** Who may edit a container's source,
extend it, or change its class. This lives in the htree, essentially
version control. The rule is extend-yes, mutate-no: a builder may subclass
or clone another's objects and verbs but cannot alter the originals.
Objects, verbs, and properties carry an author. This layer is about *code*,
not about what a running frame may touch.

**Runtime authority (reference reachability).** Object-capability
discipline in its lightweight form, which the actor model already gives:
authority is reachability. A frame can only send a message to an object it
holds a reference to, references are unforgeable and obtained only by being
passed one, and there is no ambient authority or global handle to arbitrary
objects. This is also the answer to the no-central-authority world (the
Walled City of Gibson's Bridge trilogy is the north star): authority cannot
emanate from a center that does not exist, so it is reachability and
delegation of references. It rejects ownership-as-authority (a verb running
with all of its owner's rights, as in LambdaMOO's verb-owner model, Unix
`setuid`, or SQL `SECURITY DEFINER`), which is the confused-deputy problem.

The confused deputy is handled without any effect accounting. When Mara's
`curse_strike` is passed the opener, it can only send a damage *message*,
which the player's own actor validates (safe zone, armor, whether it accepts
damage from this source here). It cannot read `opener.inventory` unless the
player actor chooses to answer. The target actor owns its policy, which is
where the decision belongs. Reading `self` stays ambient to the object;
cross-object reach needs a held reference.

On top of reachability, three lightweight pieces, all cheap and debuggable:

- **Coarse declared permissions.** The few genuinely dangerous powers
  (affect another player's state, spawn or destroy objects, persist,
  hypercall or network) are declared in the `.exi`, visible to owners and
  moderators, and checked coarsely, not threaded per call. This is what NWN
  and LSL do, and it is comprehensible to non-technical builders.
- **Capability as handle.** Holding a thing grants a specific power: the
  trace channel (hold the trace id, subscribe to an object's trace stream),
  or a key that opens a locked door. A capability you *have* that unlocks a
  door, not a token you must *present* for ordinary computation. This is
  where capabilities are a feature, used deliberately.
- **The manifest as advisory disclosure, not a runtime gate.** The compiler
  still statically computes what an effect touches ("opener HP, room
  contents") and shows it in the class browser for the owner or moderator to
  review at attach time. This keeps the consent-surface value with zero
  runtime cost and no silent failures. Player consent moves from per-action
  popups to shard-level trust plus builder-time review, right for a
  moderated community.

**Dropped: the fine-grained effect system.** An earlier draft layered a
mandatory effect system on top: per-verb capability manifests as runtime
gates, interaction envelopes with subset-checking down the call chain, and
attenuated use-metered tokens ("damage-once", counted). It is dropped. It is
a debugging burden that lands hardest on the least-technical builders in the
live-edit loop, which is the product's core: a script that silently fails
because a metered token lapsed or a manifest was not a subset of an envelope
several frames up gives the builder nothing to act on. And it buys
least-authority guarantees that a medium-to-high-trust world (invite-only,
moderated, federated shards with a ban button) does not critically need.
Match the mechanism's cost to the trust model. As shown above, actor
validation already handles the confused deputy, so the effect system was
redundant as well as expensive.

The encapsulated-service case (a bank writing a ledger) needs nothing
special: the service mutates its own state, ambient to its actor, and the
caller's contribution arrives as a validated message (see "Interaction
model").

**Per-owner resource quotas** round it out: per-task tick budgets (from the
coroutine model) stop runaway loops; a per-owner task and memory quota
stops a script exhausting the shard.

Still social as much as technical (who may grant what, trust between
builders), but no longer a blank space, and the capability question is
settled: lightweight object-capability discipline (reachability plus actor
validation) simplifies and hardens for free, while the fine-grained effect
metering was the debugging burden and is dropped.

### Live edit, persistence, and migration: specified, one sub-question open

A builder edits a live world. The worked example (Rell edits
`barrow_chest.on_open` mid-effect; Toft retypes `container.contents` while
thousands of instances hold old-shaped data) splits this into parts that
our machinery handles unequally.

**Behavior edits: drain, not swap.** An edit yields a new hash; the old
blob is immutable and stays. Coroutines already running the old code are
pinned by hash and finish under it; the htree path remaps so only new
invocations get new code. This is MOO's "recompile affects future calls,"
and it is safe because a suspended continuation may be incompatible with
the new code's suspend points.

**Propagation is a capture-mode choice.** By-hash freezes a behavior;
by-path follows edits. A path-captured long-lived coroutine (an NPC loop, a
session) re-resolves its behavior from the htree at its next turn barrier, a
natural safe swap point. Short effects freeze; persistent agents follow the
path. This is capture-a-value versus capture-a-reference from the capture
manifest, gated at existing suspension points.

**Code-versus-code migration is already solved by content-addressing.**
Unison-style, everything depends on specific hashes, so an upstream retype
is a new version; a downstream extender keeps running against the old hash
until it chooses to upgrade and reconcile. Toft cannot involuntarily break
Rell, which a no-central-authority world requires.

**Data-versus-instance migration must be lazy.** The DHT forbids
stop-the-world: you do not hold all instances. Each instance carries the
shape-version it was written against and migrates forward on access.
Compatible changes (add a field with a default, rename, widen) apply
structurally and automatically; a breaking retype requires a
builder-supplied `migrate old -> new` verb, itself a content-addressed
fragment. This is Protobuf/Avro schema evolution and Erlang `code_change`.

**No revocation problem.** With the lightweight capability model there are
no metered tokens to revoke mid-flight. A running effect holds object
references on its stack, which stay valid under drain semantics, and coarse
declared permissions are checked when an effect is attached or started, not
per operation, so an edit to a permission affects future starts, not effects
already running.

**OPEN sub-question:** a by-hash-frozen coroutine expects old-shaped data,
but lazy migration moves the instance forward, so frozen code meets
migrated data. Cross-object access is actor-mediated, so the actor
boundary (already the permission boundary) is the natural schema-adaptation
boundary: the owner can serve a property in the shape the requester's
`.exi` contract expects. Undecided whether the rule is forward-only storage
plus compat read-views, or reader-directed migration (which thrashes when
old and new readers alternate).

## Gaps and risks

A critical pass over the brief. Each item is either a gap with a direction
now set, or a risk to carry forward.

### Language surface (direction set, work pending)

The brief designed the substrate far more than the language it is named
for. Direction: Excelsior stays MooScript-like, Algol-family (roughly Algol
68 expression syntax) with BASIC/Fortran block terminators, plus the data
literals discussed. The full grammar, type-system detail, error model, and
intrinsic library are still to write. This is the next real work.

### 3D and the host boundary

The server side is not graphical, so this is mostly a runtime concern. For
the language, add small vector and matrix value types so 3D data passes
cleanly. Heavy geometry (intersections, collision) does not belong in the
ColdFire VM; expose it as a hypervisor call to native host code. So the
language gains vec/mat types and a hypercall convention, not a large
graphics API.

### Debugging (mechanism set)

MooScript-style trace comments, plus a capability-shaped live channel: each
object has a unique 64 or 128-bit trace address, and anyone holding it can
subscribe to that object's trace stream (the owner by default, or any
grantee). Debugging is a listen subscription, reusing the capability model.
Async traces across coroutines and turns still need format work, but the
transport is settled.

### Governance and moderation (reframed)

The product is not a for-profit MMO. It is a toolkit for community-run,
community-moderated, often invite-only private shards, on the subreddit
model: the platform provides the tools, the community does the moderating.
Federated servers exchange access permissions and keep a coherent access
policy. This is why there is no central wizard: the world is small and
private by design, run by as many or as few people as an operator wants.
Residual technical risk: freeze/thaw over immutable content-addressed state
is an item-duplication vector (re-thaw a pre-spend snapshot); low stakes on
a casual shard, but a shard running an economy must guard it.

### Networking and content availability (covered separately)

Not underthought here; it has its own design, SHOAL
(the SHOAL.md note in the smolvfs project): a content-addressed replication confederation
over smolvfs, in the IPFS/Dat/Perkeep family, with fetch-by-hash verified
against local storage or a peer, offline-first nodes, signed single-writer
topics, and asset plus player-state portability across federated shards.
Availability, routing, and hosting live there, out of scope for this brief.

### Capabilities: answered

The question was whether a capability system simplifies and hardens or
burdens live debugging. Answer: it splits. Lightweight object-capability
discipline (authority is reference reachability, plus actor validation of
received messages) simplifies and hardens, and the actor model already
gives it for free; its only failure mode, "no reference to X," is a clear
local error. The fine-grained effect system layered on top (manifests as
runtime gates, envelopes, subset-checking, metered attenuated tokens) is the
debugging burden and is dropped, over-built for a moderated community's
trust model. See the permission model section.

### CAS scope (narrowed)

Not content-addressing everywhere. CAS (smolvfs) is for game assets
(models, textures, audio, scripts) cached and exchanged between hosting
providers (S3, HTTP) and clients, and, over SHOAL, for portable shareable
content like player-character records so a player can move between
federated shards. The Unison-style symbol-granular build cache is optional,
not core.

### ColdFire VM choice (justified)

The native-CPU-VM choice was analyzed in
the coldfire-emulator research note. A tiny bytecode is not much
smaller in practice (the Quake 3 VM is about 2,000 LOC but needs a whole
toolchain built), while a real ISA brings GCC and FreePascal for free.
ColdFire V4e was the one finalist where every arithmetic and floating-point
op compiles to a single hardware instruction, in a roughly 2,545-LOC
emulator. The multi-toolchain targeting is the payoff. Not overtuned.

### Concurrency (collapsed)

The over-exposed coroutine, channel, and select model is collapsed to a
single builder-facing construct, the durative effect (see "Concurrency and
turn model"). It covers the workflows that came up (duration tick, buff
enter/exit, delayed one-shot, event reaction, AI loop) with optional
clauses, plus the `after` and `when` shorthands. Raw CSP is
runtime-internal, so builders cannot deadlock or starve a turn. Remaining:
input-driven sessions are handled by the dialog and menu constructors,
which still need their own spec.

### Numeric (simplified)

Float determinism is dropped; see the numeric model section. Floats are
plain hardware IEEE double, and a fixed-point type is added for exact stat
math. The soft-float pinning and reproducible-transcendental machinery are
gone.

## Next steps

- Detail the dialog and menu constructor surface (the data-driven session
  sugar) on top of the client and dialog architecture now specified.
- Close the remaining open items: the concurrency working-set number, the
  live-edit adaptation rule, and the permission social policy.
- Sketch the Excelsior surface concretely against current MooScript: the
  option-A static types with local inference, the data-literal syntax, and
  the quote/quasiquote forms, with a worked dialog tree and a worked
  mechanics formula.
- Prototype the compile-time data-literal path first (spectrum item 1),
  since it is the safest and covers the most builder value.
- Consider the class-based object model plus the coroutine turn loop as the
  first implementation slice, since everything else hangs off that spine.

---

