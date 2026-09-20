# smolmoo design

The internal design of the smolmoo engine: the data model, the RISC-V RV32
verb VM and its syscalls, verb dispatch and argument matching, task scheduling,
and the cached containment rollups. This is the design doc; `README.md` is the
operator and player guide (building, running, accounts and invites, permissions,
the web tools, in-game help). Game logic ships as verbs and world data: nothing
here is specific to any one game. The ChromeSix rules and how they map onto
these primitives live in `chromesix.md` and `chromesix-smolmoo.md`.

## Overview

- Integrated HTTP server, no reverse proxy. Only the minimum needed is built.
- SSE (Server-Sent Events) for server-to-client push, HTTP POST for
  client-to-server commands. No WebSocket (avoids the upgrade handshake and
  frame parsing) and no long-poll (avoids extra round-trips).
- A single-threaded `select(2)` event loop over a fixed connection array (like
  the dm reference MUD). Pending HTTP connections carry read buffers and are
  dispatched when complete.
- Memory-based world: the server loads the world from a single file on boot and
  writes a snapshot on demand (`save`) or on a trigger.
- Each connection gets a dedicated RISC-V RV32 VM, so no state machine is
  needed for user input and parsing.
- MOO-like objects held in memory: a list of free-form properties with
  prototype-OO inheritance. Numbered objects, where `#0` is the system object
  and `#0.name` is a property on `#0`.
- Timers drive anything that must run on a future tick: turn tracking, events,
  snapshot saving.

### Threading

Single-threaded for now. The target Raspberry Pi is multi-core and the expected
workload splits into I/O-bound work (the HTTP server, world save) and CPU-bound
work (RV32 VM execution). Threading with pthreads (an rwlock on objects, a
message ring between the I/O and VM threads) is roughly 100 to 150 lines. The
`rv_run(cpu, N)` model makes preemption natural: run N instructions, return to
the I/O thread, repeat.

## Data model

Objects are numbered (`#0` the system object) and hold a flat list of free-form
properties. Properties are strings or object references only: `obj_serialize`
writes just those two kinds, so numeric properties are not persisted as numbers.
The convention throughout is to store numbers as string properties and parse
them with `atoi` in the verb. A property value beginning with `#` is read as an
object reference.

Inheritance is prototype-based through each object's `parent`: a property lookup
walks the parent chain, child first, then ancestors. Prototypes register on the
system object, so `#0.room` is the generic room, `#0.exit` the generic exit, and
so on. These act as base classes, and `#0` holds a convenient reference to each.
A game adds its own prototypes the same way (for example a creature base and a
container that owns its verbs) rather than by any special mechanism.

Containment is a separate relation from inheritance: an object's `location`
property (an object reference) names its container. The parent chain is
inheritance; the `location` chain is where a thing physically sits. `sys_next`
walks a container's direct contents.

## Cached containment rollups

Many derived quantities are a sum over an object's containment subtree: total
carried weight, the item count a container's capacity limits, and, in the same
shape, gear-derived combat stats. Computing these by walking the subtree on
every read is wasteful, so the host caches them.

A rollup is the sum of one numeric field over an object's containment subtree
(its descendants by `location`, the object's own field excluded). An empty field
name counts items instead, each descendant contributing 1. Each object caches
its own subtree sum for one field. `prop_set` marks the affected containers
stale when a change below them can alter the total: a move (a `location` write)
invalidates both the old and the new container chains, any other field write
invalidates the enclosing chain, and `obj_free` invalidates on destruction. A
read recomputes only a stale node, folding in each child's own cached subtree
sum, so fresh child caches are reused. A recursion depth bound guards a
pathological containment cycle.

Verbs reach it through `sys_rollup(obj, field)`. Weight and item count are the
first users (`sys_rollup(creature, "weight")` for load, `sys_rollup(container,
"")` for the count a `cap` limits); deriving combat stats from worn and held
gear is the same shape and can reuse it.

## The verb VM

A RISC-V RV32 emulator (`rv32.c`) provides the sandboxed execution environment.
Each connection gets a private `rv_cpu` instance with a 128 KB little-endian
address space.

The platform is language-agnostic: any toolchain that produces an RV32 ELF
works. The bundled path is the in-tree skjegg toolchain, `skj-cc-rv-psabi` (C,
standard RISC-V ILP32 psABI) then `skj-as-rv` (assembler) then `skj-ld-rv`
(linker). The default scripting language for world builders is MooScript, a
statically typed LambdaMOO-inspired language
(see `sdk/moo/lambdamoo-syntax.md`); its compiler is a later milestone, so verbs
today are written in C. Evaluated alternatives were Picol (Tcl, weak
sandboxing), Forth/MUF (proven in TinyMUCK but hostile syntax for casual
builders), and a custom bytecode VM (the right abstraction but more code than a
stock CPU emulator for equivalent isolation).

Each verb links against a small runtime, `sdk/runtime/verb_rt_rv.S` (the ecall
syscall stubs plus the 64-bit integer helpers the RV32 backend calls). A verb is
a self-contained program: it defines `main` and includes `mulibc.h`. The runtime
supplies `_start`, which calls `main` and exits with its return value (a verb may
also `_exit` directly). `skj-run` runs the same emulator standalone for
testing.

### Verbs as a service

Verb programs are RISC-V RV32 ELFs stored in a content-addressable store (CAS).
`world.data` references them by BLAKE2b hash (`elf=b2:<hash>`). The CAS stores
files under `depot/<2-char prefix>/<hash>`, memory-mapped read-only at load time.

Most verb programs run to completion: the host starts a fresh VM instance for
each invocation, with no restart protocol. Agents are the exception. They link
the event-loop runtime and stay resident under the system session, handling
events until the server restarts (see `OLC.md`).

### Task scheduling

VM tasks are heap-allocated (up to 1024 concurrent tasks, about 128 MB). Each
verb invocation creates a task. Tasks that complete within their initial
instruction budget are reaped immediately, so simple verbs carry no overhead.
Tasks that suspend or exceed their budget stay in the task queue and are stepped
each main-loop iteration. Tasks are reaped automatically when their owning
session disconnects.

`sys_spawn(hash, delay_ms, args)` creates a new task from an ELF hash. The
spawned task inherits the parent's session and player/room context. If
`delay_ms > 0` the task sleeps before becoming runnable. Returns the task ID, or
-1 on error.

`sys_suspend(delay_ms)` suspends the current task: `delay_ms > 0` sleeps for that
duration, `0` yields (re-queued immediately), `< 0` sleeps indefinitely (woken by
an external event). Returns 0.

### Task states

    TASK_FREE      slot is unused
    TASK_READY     runnable, stepped on the next scheduler pass
    TASK_SLEEPING  suspended, woken by a timer or a mailbox event
    TASK_DEAD      allocated but not yet set up

A task is bound to a session and reaped when that session closes, except for
agents, which run under a reserved system session the reaper never frees (see
`OLC.md`). Login, character creation, `@program`, and the editors are host C
driven by HTTP requests and session state, not VM programs.

### Mini libc

Verb programs link against a tiny C library. There is no ISO C or POSIX
conformance; the goal is familiar interfaces at minimal cost.

- `FILE*` is opaque: cast the fd integer directly (`(FILE*)1` is stdout), with
  no struct allocation.
- I/O is unbuffered: `fprintf` formats to a stack buffer and calls
  `write(fd, buf, len)`. Verb output goes to the player's session; there is no
  `read` from a VM program.

Two runtime patterns (the CRT in `verb_rt_rv.S` supplies `_start`, which calls
`main`):

```c
/* one-shot verb — runs to completion */
int main(void) {
    /* act on vm_args, print, return */
    return 0;
}

/* agent — link -lverbmain, which supplies an event-loop main() */
int  verb_dwell(void) { return 200; }         /* tick period in ms */
void on_event(const struct verb_event *m) {
    /* handle EV_TIMER / EV_ENTER / ... */
}
```

Environment variables (getenv/setenv) are intentionally omitted; space is
reserved in the linker-script memory map for them if needed later.

## Syscalls

The syscall table is small:

    0  sys_exit       terminate VM
    4  sys_write      write to fd (stdout/stderr -> player session)
    6  sys_broadcast  send message to all in room
    7  sys_getprop    read object property into buffer
    8  sys_setprop    write object property (permission checked)
    9  sys_objfind    resolve object name in scope
   10  sys_spawn      create new task from ELF hash
   11  sys_suspend    suspend current task
   12  sys_random     uniform random int in [0, max)
   13  sys_move       move an object to a destination (permission checked)
   14  sys_next       next object in a container (contents walk)
   15  sys_rollup     cached sum of a field over a containment subtree
   16  sys_create     create a persistent object under a parent
   17  sys_recycle    destroy an object the caller owns
   18  sys_call       resolve and run a verb on a target object
   19  sys_hasverb    test whether a target responds to a verb
   20  sys_setpriv    raise to / drop from the verb owner's authority
   21  sys_getmsg     pop one event from the task mailbox
   22  sys_listen     route an object's events to this task
   23  sys_getobj     read an objref property's value as an int
   24  sys_post       wake a task by id (bare EV_WAKE)
   25  sys_taskid     the current task's id
   26  sys_notify     deliver an EV_USER (with a string arg) to an object's agent

Numbers 1-3 and 5 are unused: an earlier file-descriptor event model (open,
close, read, and a `sys_wait` multiplexer) was removed once the OLC-5 mailbox
(`sys_listen`/`sys_getmsg`) replaced it.

`sys_getobj(obj, name)` returns the object id stored in an objref property (for
example `location` or `dest`), or -1 if the property is missing or not an
objref. `sys_getprop` returns only string values, so this is the read path for
object-valued properties.

Syscalls use the RISC-V `ecall` instruction. Arguments follow the standard
ILP32 psABI: up to seven in `a0`-`a6`, the syscall number in `a7`, the return
value in `a0` (0 = OK, negative = errno). A 64-bit argument occupies an aligned
register pair. Each has an inline wrapper in `mulibc.h`, with the stub in
`verb_rt_rv.S`.

`sys_random(max)` returns a uniform integer in `[0, max)`, backed by the host's
`rand_bytes` (which reads `/dev/urandom`, the same source used for password
salts and invite codes). `max <= 0` is rejected. A single d6 is
`1 + sys_random(6)`.

### Object syscalls

Verb programs read and write object properties and resolve object names through
these:

`sys_getprop(obj, name, buf, bufsz)` reads a property value into `buf`. Returns
the length, or negative on error.

`sys_setprop(obj, name, val)` writes a string value to a property. Permission is
checked against the invoking player using the owner/group/world model. Returns 0
on success, `-E_PERM` if denied, `-E_INVARG` if the object does not exist.

`sys_objfind(name)` resolves an object name in the invoking player's scope
(inventory then room contents then room then `#0`, then one level into an open
container or a body within reach). Returns the object ID, or `OBJ_NONE` (-1) if
not found.

`sys_move(obj, dest)` sets `obj`'s `location` to `dest`. `sys_next(container,
after)` returns the next object located in `container` with an id greater than
`after`, or 0 at the end, for a contents walk. `sys_rollup(obj, field)` is the
cached subtree sum described above.

`sys_create(parent)` allocates a fresh persistent object under `parent`, owned
by the invoking player, and returns its id, or a negative error. `sys_recycle(obj)`
destroys an object; the caller must own it, or be a wizard. Both reject
ephemeral targets and the system object.

`sys_call(target, verb, argstr, dobj, iobj)` runs a verb on another object, the
primitive behind MooScript's `target:verb(arg)`. It resolves `verb` by name on
the target's parent chain and the global `#0.verb` prototype, checks the
caller's execute permission, and runs the verb as a new fire-and-forget task
with `this` bound to `target`, the caller's player and room, `argstr` as the
argument string, and `dobj`/`iobj` as object arguments (`OBJ_NONE` when unused).
Returns 0, `-E_VERBNF` if no such verb, or `-E_PERM` if the caller cannot
execute it. Because verbs are void, there is no return value from the called
verb. The MooScript host bridge fills the arguments from a `target:verb(...)`
call by type: object arguments become `dobj` then `iobj`, the first string
argument becomes `argstr`.

`sys_hasverb(target, verb)` resolves the verb the same way `sys_call` does but
runs nothing, returning 1 if `target` responds to a verb the caller may
execute and 0 otherwise. It backs MooScript's interface checks.

## Verb dispatch and argument matching

A dispatch table handles the built-in host commands (say, emote, whisper, and
the rest). Everything else falls through to verb resolution, which finds a
matching verb object and runs it in the VM. Verb resolution follows LambdaMOO:
player then player.location then `#0`, and at each step the parent chain is
walked (child objects first, then ancestors).

Verbs are regular objects whose `name` property is the verb word and whose `elf`
property points to the CAS-stored binary. An optional `args` property declares
the argument spec with typed slots:

    args=<slot> <slot> ...

Slot types:

    <obj>       resolve as object (inventory -> room -> #0,
                then one level into an open container or a body)
    <player>    resolve as connected player name
    <exit>      resolve as exit in current room only
    <text>      free-form text (consumes remaining words)
    <any>       unresolved text (consumes remaining, must be last)
    bareword    literal atom (sugar for <atom=bareword>)
    <atom=X|Y>  match one of the pipe-separated alternatives

`<obj>`, `<player>`, and `<exit>` require successful resolution: if the named
thing is not found the match fails and verb search continues to the next
candidate. `<text>` and `<any>` always succeed. Up to 8 slots per spec. Bare
words in the spec are sugar for `<atom=word>`; all prepositions are atoms.

Matching is greedy and atom-delimited: non-atom slots consume input words until
the next atom boundary. The first typed slot fills dobj/dobjstr, atoms between
fill prepstr, and the second typed slot fills iobj/iobjstr.

    args=                          bare verb, no arguments (look)
    args=<obj>                     verb <dobj> (examine sword)
    args=<obj> in <obj>            verb <dobj> in <iobj> (put sword in chest)
    args=<obj> <atom=to|at> <player>   give sword to Alice
    args=<text>                    verb followed by free text (say)
    args=<any>                     verb + anything (no resolution)

Omitting the `args` property matches unconditionally.

Object resolution search path: player inventory then room contents then room
then `#0`. As a lowest-priority fallback it then reaches one level into any open
container (declaring a `cap`) or a dead body within reach, so an item inside a
satchel or on a fallen body resolves by name. Exit resolution is the current
room only.

The VM receives pre-parsed arguments in `struct vm_args`: player, room, argstr
(the raw tail), this_obj (the verb owner), dobj and iobj (resolved object IDs or
`OBJ_NONE`), dobjstr, iobjstr, prepstr (the raw strings), and verb (the verb
name).

## Building and installing verbs

Verbs are ordinary objects. Add a source file and register it in `verbs.conf`
with its object id, parent, verb word, source file, mode, and args spec.
`make install` runs `smolmoo install verbs.conf`, which compiles each verb and
stores it in the depot, the same path all verbs use. Because the depot is a
build artifact, always `make install` after changing a verb, the VM, or the
world, and `rm -rf depot _build` for a clean baseline when in doubt.

Under the hood the `install` subcommand, for each line in `verbs.conf`:

    1. cross-compiles the .c source with the in-tree skjegg RV32 toolchain,
       linking the verb runtime (sdk/runtime/verb_rt_rv.S) -> RV32 ELF
    2. hashes the ELF with BLAKE2b-256
    3. stores it in the CAS: depot/<2-char prefix>/<hash>
    4. writes the verb object (id, parent, name, args, and elf=[mode, b2:hash])
       into the depot's object map

The verb objects live in `verbs.conf`, not in `world.data`; the world snapshot
carries only game data and references verbs by hash.
