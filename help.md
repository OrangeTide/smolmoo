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
    @edit #N.prop            open a property in the web editor (read-write)
    @view #N.prop            open a property in the web viewer (read-only)
    @chmod #N.prop=rw,r,r    set per-property permission flags
    @chown #N=#M             transfer ownership (wizard only)
    @chgrp #N=#G             set an object's group
    @program #N              compile the object's src into a verb

`@dig <exit> to <name>` creates a new room and links it, or `@dig <exit> to
#N` links an existing room; both add a return exit named `back`. Object
arguments are `#N`, `&N`, or a name near you.

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

## programming

Write a verb in MooScript or C without rebuilding the server.

    @create #400             make a verb object under the Verb Prototype
    @set #N.verb=<word>      the command word players type
    @edit #N.src             write the source in the web editor
    @program #N [c|moo]      compile #N.src, storing the result on #N.elf

A MooScript verb's entry is `verb main(player, room, this, dobj, iobj, arg)`;
declare only the leading parameters it uses. A C verb defines its own `_start`
and includes `mulibc.h`. `@program` picks the language from the source (C when
it has an `#include`), or you can name it: `@program #N c` or `@program #N moo`.
Compiling requires the server to have the bundled SDK toolchain present. You can
only program an object you own.

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
