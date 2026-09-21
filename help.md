<!-- SPDX-License-Identifier: 0BSD OR CC0-1.0 -->
# smolmoo help topics

The general commands every smolmoo world shares: chat, movement, building,
feedback, and the web tools. Game-specific commands (combat, gear, skills) are
documented with the game itself; on the ChromeSix world see
`chromesix-smolmoo.md`.

Each section below is one help topic. The same topics are seeded on the
in-game help object (`#0.help`), so `help <topic>` prints a short version of the
matching entry and `help` alone lists the topics. The blurbs there are the
quick reference; this file is the full one. Editing a topic property on
`#0.help` (see the `building` and `editor` topics) changes what players read
in-game.

## say

    say <text>
    "<text>

Speak aloud to everyone in your current room. Others see `<name> says,
"<text>"`; you see `You say, "<text>"`. The bare `"` is a shorthand, so
`"hello` reads the same as `say hello`.

## emote

    emote <action>
    :<action>
    ::<possessive action>

Describe an action in the third person to the room. `emote waves` shows
`<name> waves`. The `:` prefix is the shorthand. `::` glues the text directly
onto your name for possessives, so `::'s datapad beeps` shows `<name>'s datapad
beeps`.

## whisper

    whisper <player> <text>

Send a private line to one player who is in the room with you. Only that player
sees it. Use `page` to reach someone elsewhere.

## page

    page <player> <text>

Send a private line to a connected player anywhere on the server, in the room
or not. If they are not connected you are told so.

## look

    look

Show your current room: its name, description, the exits leading out, and who
else is present. Free to use any time.

## help

    help
    help <topic>

`help` lists the available topics. `help <topic>` prints the entry for one of
them, for example `help emote`. This page is the long form of the same content.

## quit

    quit

Disconnect from the server. Your character and its state persist; log back in
to resume.

## building

Create and edit objects. Most building commands are wizard or owner gated.

    @create #<parent>        make a new object under a parent prototype
    @clone #N                copy an object into a new one you own
    @recycle #N              destroy an object you own
    @set #N.prop=value       set a property (also `&N.prop=value`)
    @examine #N              show an object's owner, group, and property flags
    @move #N to <dest>       relocate an object you own into a room or container
    @dig <exit> to <room>    make a room and a linked exit pair (room owner)
    @go #N                   teleport yourself to a room (wizard only)
    @find <name>             list objects whose name matches
    @contents [#N]           list what is in a room or container
    @proto list              list the prototypes registered on #0
    @edit #N.prop            open a property in the web editor (read-write)
    @view #N.prop            open a property in the web viewer (read-only)
    @chmod #N.prop=rw,r,r    set per-property permission flags
    @chown #N=#M             transfer ownership (wizard only)
    @chgrp #N=#G             set an object's group
    @program #N              compile the object's src into a verb
    @wake #N                 start #N as an autonomous agent (wizard only)
    @sleep #N                stop an agent and keep it stopped (wizard only)

`@dig <exit> to <name>` creates a new room and links it, or `@dig <exit> to
#N` links an existing room; both add a return exit named `back`. Object
arguments are `#N`, `&N`, or a name near you.

`@wake #N` starts object `#N` as an agent: a program that keeps running with
no player present. `#N.brain` must name the agent verb (a program that links
the event-loop runtime; see the `programming` topic). The agent runs under the
system session, so its timer keeps firing in an empty room. `@wake` marks the
object awake in the world, so it comes back on its own after a restart or a
`@rewind`. `@sleep #N` stops it and clears that mark, so it stays stopped.

Permission flags are three comma-separated owner/group/world triplets of `r`
and `w`; the default is `rw,r,r`. See also the `editor`, `programming`, and
`groups` topics.

## reset

Keep rooms populated with a living world. A reset rule is an object under the
Reset Prototype (`#910`) that names a room, a prototype to clone, and how many
to keep.

    @create #910             make a reset-rule object
    @set #N.room=#<room>     where to spawn
    @set #N.proto=#<proto>   what to clone (e.g. an NPC prototype)
    @set #N.count=<n>        how many live instances to maintain
    @reset [#area]           reconcile the rules now (wizard only)

`@reset` tops each room up to `count` live (non-downed) children of `proto`,
cloning only the shortfall. It is idempotent: running it again spawns nothing
until instances die or are removed, so it both stocks a fresh room and
repopulates one that has been cleared. An optional `@set #N.area=#<area>` on a
rule lets `@reset #<area>` reconcile just one area's rules.

## store

Sell goods from a store. A store is any object that holds priced item objects
in its contents: a vendor NPC, a vending machine, a crate.

    list [from <store>]        show what a store sells and the prices
    buy <item> [from <store>]  buy one, into your hands, for creds

With no `from`, the store is the room's `vendor` NPC. A named `from` picks an
object in the room, so a vending machine is a store with no NPC. `buy` moves one
instance of the item to you and draws its price off your creds; the stock goes
down by one. Standing with the store's faction shifts the price, and a Hostile
or worse faction refuses the sale.

A builder stocks a store by putting priced items in it: set `price` on an item
(`@set #N.price=5`) and `@move` it into the store, or keep it stocked with a
reset rule (see the `reset` topic) whose room is the store. On the ChromeSix
world a store that sets a `stim` base price (`@set #N.stim=75`) also sells the
counter-based stim dose through `buy stim`.

## behavior

Make an NPC react when a player walks into its room. Set a `behavior` property
on the mob (or on a prototype it inherits from) to a comma-separated list of
reactions.

    @set #N.behavior=greet          say a line to the room on entry
    @set #N.behavior=aggro          open a fight on the newcomer
    @set #N.behavior=greet,aggro    both
    @set #N.greeting=<text>         the line `greet` says (optional)

Reactions fire only while a player is present, so an empty room stays quiet and
costs nothing. `aggro` starts a normal fight, so the mob needs combat stats; the
simplest source is to parent it to an NPC prototype that already has them. A
mob with no `behavior` property does nothing. Walking into a fight that is
already running does not pull you in; `attack` to join it.

For a mob to act on its own, with no player present, wake it as an agent:

    @set #N.brain=#453              the mob agent (__rover)
    @set #N.behavior=wander         step a random exit each tick
    @set #N.behavior=patrol         follow the `route` stops in order
    @set #N.route=#A,#B,#C          patrol stops (room ids), wraps at the end
    @set #N.dwell=3000              tick period in ms (default 5000)
    @wake #N                        start it (wizard only)

A woken mob keeps its `greet`/`aggro` reactions as well, so `wander,greet` both
wanders and greets. `patrol` takes precedence over `wander`. It holds position
while downed, dead, or in a fight. See also the `programming` topic for writing
your own agent.

## vehicles

A vehicle is a moving room: a room that carries its riders from stop to stop.
Riders board it, ride it, and step off wherever it currently is.

    @create #100                    a vehicle is a room object
    @set #N.vehicle=1               mark it as boardable
    @set #N.name=carriage           what riders board by name
    @set #N.description=<text>      what riders see inside
    @set #N.location=#<stop>        its current stop (where it starts)
    @set #N.route=#A,#B,#C          the stops it travels, in order
    @set #N.brain=#456              the vehicle agent (__transit)
    @set #N.dwell=8000              tick period in ms for a timed vehicle
    @set #N.transit=4000            travel time in ms between stops (optional)
    @set #<stop>.line=#N            let `call` on that platform summon it
    @wake #N                        start it (wizard only)

With a positive `dwell` the vehicle is a train: it advances one stop each tick,
on its own, wrapping at the end of the route. With no `dwell` it is an elevator:
it never moves until a rider aboard names a stop with `floor <n>` (n counts from
1 along the route), or someone on a platform calls it. A platform can summon its
vehicle only if that stop's room names the vehicle in a `line` prop.

With a positive `transit` the vehicle takes that many milliseconds to travel
between stops: it departs, rides with its doors shut, then arrives. While it is
under way `board`, `disembark`, `floor`, and `call` are refused, so a rider is
carried along and cannot get off in the middle. With no `transit` the trip is
instant, as before. Players use it with:

    board <vehicle>                 climb aboard, when it is at your stop
    floor <n>                       (aboard an elevator) go to the nth stop
    call                            (on a platform) summon its line here
    disembark                       step off at the vehicle's current stop

## programming

Write a verb in MooScript or C without rebuilding the server.

    @create #400             make a verb object under the Verb Prototype
    @set #N.verb=<word>      the command word players type
    @edit #N.src             write the source in the web editor
    @program #N [c|moo]      compile #N.src, storing the result on #N.elf

A MooScript verb's entry is `verb main(player, room, this, dobj, iobj, arg)`;
declare only the leading parameters it uses. A C verb defines `main` and
includes `mulibc.h`; the runtime supplies `_start`, which calls `main` and exits
with its return value. `@program` picks the language from the source (C when
it has an `#include`), or you can name it: `@program #N c` or `@program #N moo`.
Use `@program #N agent` to build an event-loop agent (see below); it is a C
program that also links the agent runtime. Compiling requires the server to
have the bundled SDK toolchain present. You can only program an object you own.

A one-shot verb runs to the end of `main` and stops. An agent instead links the
event-loop runtime and keeps running, handling events as they arrive. An agent
defines two functions in place of `main`:

    void on_event(const struct verb_event *m);  handle one event
    int  verb_dwell(void);   tick period in ms (<= 0 to block, no ticks)

`on_event` receives an `EV_TIMER` each time the dwell elapses with no other
event, `EV_ENTER` when a player walks into the agent's room, and `EV_USER` with a
string argument when another verb routes a request to it with `sys_notify` (as
the `floor` verb does to a vehicle). Compile it with `@program #N agent`, name
it in another object's `brain` property, and start that object as an agent with
`@wake #N` (see the `building` topic).

## history

Inspect and roll back the world's saved history. Every world save is
recorded as a signed version in an append-only chain.

    @history         list saved versions, newest first
    @rewind <seq>    restore the world to an earlier version (wizard only)
    @gc [keep]       collect old versions and dead objects (wizard only)
    @fsck            check the depot for corruption (wizard only)

`@history` shows each version's sequence number, save time, and root, and
marks the one that is currently live. `@rewind <seq>` restores the world to
the root of that version. The rewind is itself recorded as a new version, so
the chain only ever moves forward and the rollback is auditable. Rewinding
reloads the live world and disconnects every session, since each player's
in-world presence belongs to the state being replaced. Reconnect afterward.

`@gc` bounds depot growth. It keeps the newest `keep` versions (default 128)
plus the live world, and removes the saved object versions and old version
records that nothing retained still needs. Verb code is never touched.

`@fsck` checks the depot for damage without changing anything. It confirms
every stored object still hashes to its own address, that the live world's
objects, pages, and referenced verb code are all present, and that the signed
history chain verifies. It reports counts of any corrupt, unreadable, or
missing objects.

## groups

    @group create <name>              create a permission group
    @group add <group> <player>       add a member (owner or wizard)
    @group remove <group> <player>    remove a member
    @group list [<group>]             list groups, or one group's members

Groups let several accounts share write access to an object through its `group`
field and the group triplet of each property's flags.

## invite

    @invite        list your invite codes and how many uses remain
    @invite new    create an additional code (admin only)

New accounts join with an invite code. Each account starts with three
single-use codes; share the code, or a link of the form
`http://<server>/invite/<CODE>`, which pre-fills the sign-up form. Exhausted
codes are refreshed on a 24-hour timer.

## feedback

    @gripe <text>
    @typo <text>
    @bug <text>
    @idea <text>
    @suggest <text>
    @comment <text>

Send a note to the server operators, tagged by kind. The message is logged with
your name and room. Use it to report a typo, a bug, or an idea without leaving
the game.

## editor

The web property editor edits any property you have write access to, in a
browser tab with live markdown highlighting.

    @edit #N.prop    open the editor (read-write)
    @view #N.prop    open the viewer (read-only)

Ctrl+S (or Cmd+S) saves. The underlying HTTP endpoints are `GET`/`POST
/prop?obj=N&prop=P&sid=S` for raw values and `GET /edit` and `GET /view` for
the pages. Permission follows the owner/group/world model.

## wiki

The server can host a wiki: `#0.wiki` points at an object whose properties are
pages, each viewable and editable through the web editor. An operator sets it
up by creating an object and registering it with `@set #0.wiki=#<id>`. Pages
use the same markdown subset as the property viewer, where the syntax
characters stay visible in place.
