# smolmoo : a smol (small) MOO (MUD (Multi User Dungeon) Object Oriented)

smolmoo is a small, self-contained MUD/MOO game server written in C. It
speaks HTTP and Server-Sent Events, so players connect with a browser
instead of a telnet client, and the whole world runs from a single binary
and a content-addressed store. It has no external dependencies and is
built to run on a Raspberry Pi.

The world is a prototype-based object system. Game logic lives in verbs
that run on an in-process RISC-V RV32 virtual machine, written in either
C or MooScript and compilable from inside the running game. The world is
saved as a signed, content-addressed history that can be rewound and
verified. A ChromeSix combat and social system (an OpenD6 variant) ships
as verbs on top of the engine.

The design inspiration is dm (deathmatch), a telnet MUD written in 16 KiB
of source. Keeping the source small is a first-class constraint.

## Goals

- [x] Implement a small web-based (HTTP) game that feels like a MUD/MOO.
- [x] Primitive text interface in the browser. Essentially a full-screen
      monospaced terminal.
- [x] Core server is small. Target: under 20 kLoC including vendored
      libraries (smolvfs), excluding the MooScript compiler and test code.
      The core server is about 19 kLoC (`smolmoo.c`, the RV32 VM, and the
      vendored smolvfs and monocypher). `smolmoo.c` itself is about 7.5 kLoC.
- [x] Dependency-free. The distribution comes with all the utilities and
      libraries needed to build and run on a Raspberry Pi (Linux).

## Non-Goals

- Scalability is not designed in. There is no need to handle thousands of
  connections.

## Building, Installing, and Running

### Prerequisites

A C99 compiler (gcc or clang) and standard POSIX utilities. No external
libraries are required.

### Build

    make

This compiles the server and places the binary at `_build/smolmoo`.
Object files go in `_build/`, SDK compiler tools in `_build/sdk/`.
To remove all build artifacts:

    make clean

### Install Verbs

    make install

This builds the SDK compiler toolchain (skjegg), cross-compiles each verb
source listed in `verbs.conf` to a RISC-V RV32 ELF, and stores everything
in the content-addressable store (`depot/`). You must run this before
the first boot.

### Run

    make run

Or run directly:

    _build/smolmoo serve

On first boot (or with `--bootstrap`), an admin invite code is printed
to stderr. Open `http://localhost:7777` in a browser and use that code
to create the first account.

Environment variables configure the server:

    SMOLMOO_PORT    listen port (default 7777)
    SMOLMOO_DEPOT   depot directory (default depot)
    SMOLMOO_HTML    html file (default index.html)

Running `smolmoo` with no arguments prints usage.

### Running on a Server

Run in the background with logging:

    nohup ./smolmoo serve >> server.log 2>&1 & echo $! > smolmoo.pid

A wrapper script makes configuration easier and can be pasted directly
into a systemd `ExecStart=`:

    #!/bin/sh
    # run-server.sh
    export SMOLMOO_PORT=7777
    export SMOLMOO_DEPOT=depot
    export SMOLMOO_HTML=index.html
    exec ./smolmoo serve

For systemd, point `ExecStart=` at the script or inline the command:

    [Service]
    WorkingDirectory=/opt/smolmoo
    ExecStart=/opt/smolmoo/run-server.sh
    Restart=on-failure

### Deploy

To deploy smolmoo to another machine, copy these files:

    smolmoo             the server binary
    depot/              content-addressable store (verb ELFs + world state)
    index.html          browser client

The server runs from the directory containing `depot/`.
No reverse proxy is needed. The built-in HTTP server handles everything.

Or use `make bundle` to build, install verbs, and package everything
into `_build/smolmoo.tar.bz2`:

    make bundle

### Cross-compiling

Override `CC`, `OBJCOPY`, and `STRIP` to cross-compile. The SDK
toolchain (skjegg) always builds with the host compiler.

Raspberry Pi (aarch64):

    make CC=aarch64-linux-gnu-gcc \
         OBJCOPY=aarch64-linux-gnu-objcopy \
         STRIP=aarch64-linux-gnu-strip

Static x86_64 binary with musl (single binary, no shared libraries):

    make CC=musl-gcc LDFLAGS=-static

### Smoke Tests

    make test

Runs `test.sh`, which starts the server on a temporary port and exercises
login, chat, commands, verbs, and the web editor. `make test` runs `make
install` first, so verbs are recompiled from source. It does not
re-bootstrap the world, though: with a `depot/` already present the world
loads from it, not from `world.data`. After changing the world seed, or when
results look stale, rebuild from a clean baseline with `rm -rf depot _build`.

    make stress            # run the suite 10 times, fail on any run
    make stress STRESS_N=20 # a longer streak

`make stress` runs the suite repeatedly against one build to catch timing
flakiness that a single run usually misses. The harness never waits with a
fixed `sleep`; it polls for a condition against a ceiling, and a poll returns
the instant the condition holds, so a ceiling only bounds a genuine miss and
never slows a healthy run. There are three ceilings, longest to shortest need:
a readiness wait for server boot and the session id (`wait_ready`, `wait_sid`),
a generous asserted-outcome wait (`check_log`), and a short best-effort sync
before an action (`waitgrep`). A readiness gate that truly times out fails once
and names the instance, rather than letting the block run against an empty
session id and fail every check in it. Do not reintroduce a fixed `sleep` or a
short fixed count in the harness: that is what made the suite flake under load.

## Command-Line Tools

The `smolmoo` binary is a single executable with several subcommands.
Run it with no arguments for a usage summary. Every subcommand reads
`SMOLMOO_DEPOT` to locate the content-addressable store (default
`depot`), so the examples below can be pointed at any depot.

    smolmoo serve [--bootstrap]        start the game server
    smolmoo install [--sdk DIR] <verbs.conf>
                                       compile verbs into the depot
    smolmoo export [N | N-M ...]       write world objects to stdout
    smolmoo merge <file>               import objects, renumbered
    smolmoo migrate                    convert legacy world.data to omap

### export

`export` serializes objects from the depot to standard output in the
`world.data` text format. With no arguments it writes every persistent
object. Arguments select a subset: a bare number picks one object, and
`N-M` picks an inclusive range. Several selectors can be combined.
Progress and status messages go to standard error, so the object data
on standard output can be redirected to a file without contamination.

    smolmoo export > world.txt          # whole world
    smolmoo export 101 > lobby.txt       # a single object
    smolmoo export 100-150 500-600 > sel.txt

This replaces the earlier `dump` command. The name `dump` still works
as an alias for `export` with no selectors.

### merge

`merge` reads an export file and adds its objects to the current world,
renumbering them into a fresh contiguous range above the highest
existing object id. References among the merged objects (parent,
`owner`, `group`, and any property whose value is an object number) are
rewritten to the new ids. References that point outside the merged set,
such as to the system object `#0` or a shared prototype, are left
untouched so they still resolve in the destination world. The remapped
range is printed to standard error, and the result is saved to the
depot.

    smolmoo export 200-250 > area.txt    # from the source world
    SMOLMOO_DEPOT=other/depot smolmoo merge area.txt

Use `export` followed by `merge` to copy a hand-built area from one
world into another without id collisions.

## Accounts & Invite Codes

Account creation is invite-only. Every new account requires a valid
invite code. This keeps the server closed to the public while
letting existing players bring in friends.

### First Boot

On first boot (no accounts) or with `smolmoo serve --bootstrap`,
the server prints a one-time admin invite code to stderr:

    [bootstrap] admin invite: ABCD-EF01-GH23

This code allows up to 50 uses. Use it to create the first account
via the browser login screen (click "need an account?").

### Invite Code Format

Codes are `XXXX-XXXX-XXXX`, 12 characters from a 30-character
alphabet (digits plus unambiguous uppercase letters, no D/F/I/O/Q/U).
About 58.9 bits of entropy.

### How Invites Work

Each account starts with 3 single-use invite codes. Players can
view their codes in-game with `@invite`. When all uses of a code
are consumed, a 24-hour refresh timer garbage-collects exhausted
codes and tops each account back up to 3.

Admin accounts get 1 invite code with 10 uses instead, and can
generate additional codes on demand with `@invite new`.

### Sharing an Invite

Players can share their code directly, or send a link:

    http://yourserver:7777/invite/ABCDEF01GH23

The login page auto-fills the code boxes and switches to account
creation mode.

### Admin Commands

    @invite         list your invite codes and usage
    @invite new     create an additional code (admin only)

### Passwords

Passwords are hashed with argon2id (via monocypher). Stored as
`<hex_salt>$<hex_hash>` on the account object's `pwhash` property.
Parameters: 256 KB work area, 3 passes, 1 lane.

### Architecture

Accounts and invites are regular persistent objects distinguished
by parent:

    #500  Account Prototype   — parent of all account objects
    #600  Invite Prototype    — parent of all invite objects

Account objects carry `name`, `pwhash`, and optionally `admin`.
Invite objects carry `code`, `max_uses`, `used`, `owner` (objref
to account), and optionally `admin`. Both prototypes are registered
on the System Object (`#0.acct=#500`, `#0.invite=#600`) and
auto-created by `acct_init()` if missing.

The pre-login gate in `dispatch()` only allows `connect` and
`create` commands from unauthenticated sessions (`cc[sid].obj ==
OBJ_NONE`). All other commands are rejected until login succeeds.

## Permissions

Every property carries per-property flags with owner/group/world
triplets. Regular properties use `r` (read) and `w` (write) per
triplet. Default is `rw,r,r` (owner read-write, group and world
read-only).

Every object has an `owner` field (object reference to an account)
and an optional `group` field (object reference to a group object).
Access checks run in order: wizard (admin flag) overrides all, then
owner bits, then group member bits, then world bits.

### World Data Syntax

Flags appear between the property name and `=`, separated by `:`:

    name=The Lobby           # default (rw,r,r)
    secret:,,=hidden         # no world or group read
    shared:rw,rw,r=value     # group-writable

Omitting flags defaults to `rw,r,r`. The three comma-separated
positions are owner, group, world.

### Verb Execution

Execute permission is embedded in the `elf` property value using
Unix 12-bit octal mode:

    elf=[0755, b2:abc123...]    # owner rwx, group rx, world rx
    elf=[0700, b2:abc123...]    # owner-only execution

The verb object's `owner` and `group` fields determine who matches
which permission triplet. `perm_can_exec_elf()` checks the execute
bits (0100/0010/0001) against the invoking player's relationship to
the verb object. Bare `b2:hash` format is accepted for backward
compatibility (defaults to mode 0755).

### Groups

Group objects (parent #700) hold a comma-separated `members`
property listing account object references. Commands:

    @group create <name>    create a new group
    @group add <grp> <acct> add member (owner or wizard)
    @group remove <grp> <acct> remove member
    @group list [<grp>]     list groups or group members

### Commands

    @chmod #N.prop=rw,r,r set flags explicitly (owner or wizard)
    @chown #N=#M          transfer ownership (wizard only)
    @chgrp #N=#G          set object group (owner or wizard)
    @examine #N           shows owner, group, per-property flags

## World History

Every world save is recorded as a signed version in an append-only
chain, so the world keeps a verifiable, rewindable history. Each version
names the saved root, carries a sequence number, and links to its
predecessor. A record is signed with the server's key (EdDSA over
curve25519 with BLAKE2b, via monocypher) and self-addressed in the CAS,
so it verifies with no registry and no trust in whoever served it.

The signing key is generated on first serve as a 32-byte seed in a
`0600` file next to the depot (`<depot>.key`, or `SMOLMOO_KEY`). It is
never written into the depot, since the depot is the part that gets
served. If no key can be loaded, saves still work but are not recorded
in the history.

Because a chain is signed, an instance extends only history in its own
topic. If a depot is restored from a backup, or otherwise brought up
under a different key than the one that signed its history, the server
re-bases: it keeps the restored world exactly as saved, but starts a
fresh, self-signed history chain from that point rather than a broken one
it could not extend. The prior versions are no longer rewindable under
the new key, which is the honest result of not holding the key that
signed them. Keep the `<depot>.key` file with its depot to preserve the
full rewindable history across a move.

### Commands

    @history         list saved versions, newest first
    @rewind <seq>    restore the world to an earlier version (wizard only)
    @gc [keep]       collect old versions and dead objects (wizard only)
    @fsck            check the depot for corruption (wizard only)

`@history` shows each version's sequence number, save time, and root,
and marks the one currently live. `@rewind <seq>` restores the world to
that version's root. The rewind is itself recorded as a new version, so
the chain only moves forward and every rollback stays auditable.
Rewinding reloads the live world and disconnects every session, since a
player's in-world presence belongs to the state being replaced. Players
reconnect afterward.

Because every save keeps its root forever, the depot grows over time.
`@gc` bounds that: it retains the newest `keep` versions (default 128)
and the live world, marks every object still reachable from them, and
removes the superseded object versions, object-map pages, and version
records that nothing retained needs. Verb code (stored separately) is
never collected, so the marking never has to reason about it.

`@fsck` checks depot integrity without modifying anything. It recomputes
every stored object's address from its bytes and flags any that no
longer match (corruption or bit rot), confirms the live world's objects,
object-map pages, and referenced verb ELFs are all present, and walks the
signed history chain to verify each record's signature and linkage. It
reports the counts of corrupt, unreadable, and missing objects.

Saves themselves run off the main loop. A background writer thread does
the fsync-durable store while the event loop keeps serving, throttled so
at most two saves are ever outstanding.

This signed, verifiable history is the local half of what the CAS and
signing foundation makes possible. The networked half, federation across
servers (publishing the world root as a signed topic, peering by hash,
and off-site backup followers), is deferred to a distant future by
decision. It amounts to a new networked subsystem, and it is not
interesting until a server carries hundreds of active players, so it is
gated by server maturity rather than explored ahead of that. See FUTURE.md for
that menu and the full rationale.

## Browser UI

The client (`index.html`) is a full-screen monospace terminal driven
by SSE push and HTTP POST input.

- **ANSI color rendering.** Full SGR support: 16 base colors,
  256-color palette (`38;5;N` / `48;5;N`), truecolor
  (`38;2;R;G;B`), bold, italic, underline, and wavy underline (`4:3`).
  All rendered as inline CSS styles.
- **Status bar.** The server pushes room name and fuel gauge after
  every command and on fuel regen. Displayed in a fixed bar above
  the input line.
- **Command history.** Up and down arrow keys recall previous
  commands. History is kept in memory (not persisted).
- **Scrollback cap.** Output is capped at 2000 lines. Oldest
  lines are removed as new ones arrive.

## Object Editor

The web-based property editor lets you edit object properties in
the browser. It opens in a new tab with live markdown syntax
highlighting (a transparent textarea over a highlighted `<pre>`).

### Commands

    @edit #N.prop       open property in the editor (read-write)
    @view #N.prop       open property in the viewer (read-only)

The editor tab shows the property value in a text area with a Save
button. Ctrl+S (or Cmd+S) saves without reaching for the mouse.
Permission checks follow the owner/group/world model. You can
only edit properties you have write access to.

The viewer renders the property value with markdown highlighting
(see Wiki below) and provides an Edit button to switch to edit
mode.

### HTTP Endpoints

    GET  /prop?obj=N&prop=P&sid=S    read property value
    POST /prop?obj=N&prop=P&sid=S    write property value (body = new value)
    GET  /edit?obj=N&prop=P&sid=S    editor page
    GET  /view?obj=N&prop=P&sid=S    viewer page

## Programming Verbs

New verbs can be written in MooScript or C from inside the game,
without rebuilding the server:

    @create #400          make a verb object under the Verb Prototype
    @set #N.verb=greet    set the command word players will type
    @edit #N.src          write the source in the web editor
    @program #N [c|moo]   compile #N.src and attach it as the verb

`@program` reads the object's `src` property, compiles it with the
bundled SDK toolchain, stores the resulting RISC-V ELF in the depot,
and sets the object's `elf` property. The language is taken from the
source (C when it has an `#include`, MooScript otherwise), or named
explicitly as `@program #N c` or `@program #N moo`. A MooScript verb
declares `verb main(...)`; a C verb defines `main` and includes
`mulibc.h` (the runtime supplies `_start`). The source stays on `src`, so you can re-edit and
re-`@program` at any time. You may only program an object you own.

The compile runs on the server, so a server that hosts in-game
programming must ship the SDK (`_build/sdk`, from `make install`) and
`vm_rv.ld` alongside the binary and depot. A serve-only deployment
that never uses `@program` does not need them.

## Wiki

`#0.wiki` points to a dedicated wiki object. Properties on that
object are wiki pages, viewable and editable through the browser.

To set up the wiki, create an object and register it:

    @create wiki
    @set #0.wiki=#<id>

Content uses a minimal markdown subset where syntax characters stay
visible (they are styled in place, not stripped):

- `# Heading` through `###### Heading`: green, scaled font size
- `**bold**`: bold
- `_italic_`: italic
- `` `code` ``: orange highlight
- `[text](url)`: external link (opens in new tab)
- `[[page]]`: wiki link (opens in viewer)

Wiki links (`[[page]]`) resolve to the wiki object, so `[[rules]]`
opens `/view?obj=<wiki>&prop=rules`. The editor fetches `#0.wiki`
at load time to discover the wiki object ID. If `#0.wiki` is not
set, wiki links fall back to `#0`.

## Design

The engine internals are documented in `smolmoo.md`: the event loop and
threading model, the object and data model, the RISC-V RV32 verb VM and
its syscalls, verb dispatch and argument matching, task scheduling, and
the cached containment rollups. The ChromeSix game rules and how they map
onto these primitives are in `chromesix.md` and `chromesix-smolmoo.md`.

## Reference Project

dm (deathmatch) MUD is the reference and inspiration for this project.
It was an attempt to write a telnet-based MUD in 16 kilobytes of source
code. Its sources are not bundled here. `dm-sm.c` is the minified version
and `dm.c` is the commented version.

## See Also

- `smolmoo.md`: the engine design. Data model, the RV32 verb VM and syscalls,
  verb dispatch, task scheduling, and the cached containment rollups.
- `help.md`: the general player commands (chat, movement, building, feedback,
  the web tools), one topic per entry. The same topics are seeded on the
  in-game `#0.help` object, so `help` lists them and `help <topic>` prints one.
- `chromesix-smolmoo.md`: the ChromeSix game mechanics and its game-specific
  commands (combat, gear, skills).
- `FUTURE.md`: the deferred feature menu (federation, packing, asset store)
  and the rationale for each.
- `OLC.md`: the online-creation design record. How builders create areas,
  rooms, items, NPCs, stores, and vehicles in-game, mostly as verbs.
- [Mini Six: Bare Knuckle Edition](http://www.antipaladingames.com/p/mini-six.html)
- [Mini Six: Bare Bones Edition](https://www.drivethrurpg.com/en/product/144558/mini-six-bare-bones-edition)
</content>
</invoke>
