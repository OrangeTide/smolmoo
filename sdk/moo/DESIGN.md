# MooScript Design

Statically-typed scripting language for MUD/MOO systems. Inspired by
LambdaMOO's programming language, adapted for compilation to native code
and integration with prototype-OO MUD engines (target: smolmoo).

## Types

| Type      | Size   | Description                          |
|-----------|--------|--------------------------------------|
| `int`     | 32-bit | Signed integer                       |
| `float`   | 64-bit | Double-precision floating point       |
| `str`     | ptr    | Immutable flat string (arena-alloc)   |
| `obj`     | ptr    | Opaque object handle (host-resolved) |
| `bool`    | 32-bit | true/false                           |
| `err`     | 32-bit | Error code enum                      |
| `list<T>` | ptr    | Homogeneous immutable list           |
| `prop`    | ptr    | Boxed host property (tag + payload)  |
| `nil`     | 0      | Null value for obj/str/list          |

Most values fit in a machine word (32-bit value or pointer). `float`
is 64-bit (two words on m68k). Strings and lists are arena-allocated
and immutable -- no GC needed.

### Boxed Properties (`prop`)

The host boundary is dynamically typed -- `__moo_prop_get` can return
any value type.  The `prop` type makes this explicit with a two-word
arena-allocated struct:

```c
struct moo_prop {
    int tag;     // MOO_T_INT=0, MOO_T_STR=1, MOO_T_OBJ=2,
                 // MOO_T_LIST=3, MOO_T_ERR=4, MOO_T_BOOL=5,
                 // MOO_T_FLOAT=6
    int val;     // immediate (int/bool/err) or pointer (str/obj/list/float)
};
```

Property access (`obj.name`) returns `prop`.  Assigning to a typed
variable emits a runtime tag check:

    var p: prop = lobby.name;    // store boxed pointer as-is
    var name: str = lobby.name;  // check tag==1, panic $E_TYPE if not
    var n2: str = p;             // same check, unbox from prop variable

`typeof(val)` returns the tag as an int.  On statically typed values
the compiler folds it to a constant.

Verbs are always void -- no return values. Fire and forget. This
eliminates the need for return-value scheduling between verb tasks.

## Object References

Objects are referenced by path, resolved by the host:

    var cross: obj = $"wooden-cross";           // relative to current zone
    var cross: obj = $"/village/church/wooden-cross"; // absolute path

The `$"..."` syntax produces an `obj` value. Paths starting with `/` are
absolute; others are relative to the executing verb's zone context.

## Property Access and Verb Calls

    // property access — compiles to __moo_prop_get / __moo_prop_set
    var name: str = thing.name;
    thing.description = "A wooden cross.";

    // verb call — colon syntax, compiles to __moo_verb_call
    player:tell("You see a cross.");
    thing:move_to(player);

    // built-in function call — parentheses
    var n: int = length(name);
    var s: str = tostr(42);

## Verb Definitions

Verbs are the unit of execution. Always void — no return values.
Each verb receives typed parameters from the host's command dispatcher:

    verb look(this: obj, player: obj)
        var desc: str = this.description;
        player:tell(desc);

        for item in contents(this)
            player:tell("  " + item.name);
        endfor
    endverb

    verb take(this: obj, player: obj, dobj: obj)
        if (dobj.location != this)
            panic($E_RANGE);
        endif
        move(dobj, player);
        player:tell("You pick up " + dobj.name + ".");
    endverb

A source file compiles to one executable containing multiple verbs. The
host invokes verbs by name. Verb calls are fire-and-forget — no return
value scheduling needed between verb tasks.

## Error Handling: defer/panic/recover

Follows Go's model, simplified for MUD scripting:

    verb fragile_op(player: obj, dobj: obj)
        defer
            // always runs when verb returns (normal or panic)
            // LIFO order if multiple defers
        enddefer

        defer
            var e: err = recover();
            if (e != nil)
                player:tell("Error: " + tostr(e));
            endif
        enddefer

        if (!valid(dobj))
            panic($E_INVARG);
        endif

        // ... normal code ...
    endverb

- `defer { ... }` — schedule block to run on verb return (LIFO order)
- `panic(err)` — abort, run deferred blocks, report to host if uncaught
- `recover()` — inside defer, catches panic value and resumes normal return

Most verbs never use recover. Unhandled panics propagate to the host which
reports the error to the player. Arena allocation means defer is rarely
needed for cleanup — it's for non-memory resources (cache pins, dirty
flags, etc).

## Error Codes

Predefined error constants (prefix `$E_`):

    $E_NONE      // no error
    $E_TYPE      // type mismatch
    $E_INVARG    // invalid argument
    $E_RANGE     // index out of range
    $E_PERM      // permission denied
    $E_PROPNF    // property not found
    $E_VERBNF    // verb not found
    $E_ARGS      // wrong number of arguments
    $E_RECMOVE   // recursive move
    $E_MAXREC    // maximum recursion depth
    $E_INVOBJ    // invalid object

## Control Flow

    // if/elseif/else
    if (x > 0)
        // ...
    elseif (x == 0)
        // ...
    else
        // ...
    endif

    // for-in (list iteration)
    for item in contents(room)
        player:tell(item.name);
    endfor

    // for-in range
    for i in 1..10
        player:tell(tostr(i));
    endfor

    // while
    while (count > 0)
        count -= 1;
    endwhile

    // break, continue work as expected

    // value switch
    switch (command)
        case "look":
            do_look(player, this);
        case "go", "walk":
            do_go(player, this, direction);
        else:
            player:tell("I don't understand that.");
    endswitch

    // range cases (pascal-style)
    switch (ch)
        case 0..9:
            result = ch + 48;
        case 10..35:
            result = ch + 55;
        else:
            result = 63;
    endswitch

    // type switch (interface matching)
    switch type (thing)
        case Describable:
            player:tell(thing.name);
        case Tellable:
            thing:tell("You see a thing.");
        else:
            // thing is plain obj here
    endswitch

## Switch Statement

Two forms: value switch and type switch.

### Value Switch

Evaluates the switch expression once, then tests each case in order.
No fall-through -- each case is self-contained.

    switch (x)
        case 1:
            result = 10;
        case 2, 3:
            result = 20;
        case 4..10:
            result = 30;
        else:
            result = 99;
    endswitch

Case patterns can be:

- **Single value:** `case 1:` -- tests equality
- **Multiple values:** `case 2, 3:` -- matches any listed value
- **Range:** `case 4..10:` -- inclusive range, both bounds evaluated

The `else:` clause handles values that match no case. The trailing
colon on `else:` disambiguates from `else` in if-chains.

The switch expression and case values must have the same type. Range
cases require `int` type. String cases use `__moo_str_eq`.

### Type Switch

Branches on interface conformance -- a type switch for duck typing.
Cases are tested in order. In each case block, the compiler narrows
the switch variable to the matched interface type:

    switch type (thing)
        case Describable:
            player:tell(thing.name);
            player:tell(thing.description);
        case Tellable:
            thing:tell("You see a thing.");
        else:
            // thing is plain obj here
    endswitch

The switch expression must be `obj` or an interface type. Each case
names an interface. The compiler desugars to a chain of `is` checks.
Without `else:`, unmatched objects fall through silently.

### Lowering

Value switch lowers to: store switch expression in a stack slot, then
a chain of conditional branches. Each case pattern reloads the slot
and compares. Integer equality uses `IR_CMPEQ`, ranges use
`IR_CMPLTS`/`IR_CMPGTS` (signed), string equality calls
`__moo_str_eq`. On match, jump to case body; after body, jump to end.

Type switch desugars to chained `is` checks (each `is` calls the
host to verify interface conformance).

## Operators

| Precedence | Operators                | Assoc |
|------------|--------------------------|-------|
| 1 (lowest) | `\|\|`                   | left  |
| 2          | `&&`                     | left  |
| 3          | `==` `!=`                | left  |
| 4          | `<` `<=` `>` `>=`        | left  |
| 5          | `in` `is`                | left  |
| 6          | `+` `-`                  | left  |
| 7          | `*` `/` `%`             | left  |
| 8 (highest)| `!` `-` (unary)         | right |

`+` is overloaded: integer/float addition and string concatenation.

`in` tests list membership: `item in some_list` → bool.

## Lists

Immutable, homogeneous, arena-allocated:

    var items: list<str> = {"sword", "shield", "potion"};
    var first: str = items[1];          // 1-indexed (MOO convention)
    var sub: list<str> = items[1..2];   // slice

    // built-in list functions
    var n: int = length(items);
    var with: list<str> = listappend(items, "arrow");
    var without: list<str> = listdelete(items, 2);

Lists are 1-indexed to match MOO convention. List literals use `{...}`
(matching LambdaMOO), while indexing and slicing use `[...]`.

## Memory Model

Arena allocation per verb invocation:

- Each verb call gets a fresh arena
- All strings, lists, and temporaries allocate from the arena
- When the verb returns (normally or via panic), the entire arena is freed
- No garbage collector, no reference counting
- Host-provided values (property strings, object handles) are copied into
  the arena on access

## Strings

The `str` type is a flat length-prefixed buffer, arena-allocated:

```c
struct moo_str {
    int len;
    const char *data;
};
```

String concatenation (`+`) allocates a new buffer from the arena and
copies both operands. String literals point directly into the binary's
rodata -- the `moo_str` header is arena-allocated but the data pointer
references the literal in place.

The host glue interface passes `struct moo_str *` for all string
parameters and return values.

## Host Glue Interface

The integration seam — direct `__moo_*` function calls that the host
provides at link time:

```c
struct moo_str {
    int len;
    const char *data;
};

struct moo_prop {
    int tag;     // MOO_T_INT=0, MOO_T_STR=1, MOO_T_OBJ=2,
                 // MOO_T_LIST=3, MOO_T_ERR=4, MOO_T_BOOL=5,
                 // MOO_T_FLOAT=6
    int val;     // immediate (int/bool/err) or pointer (str/obj/list)
};

struct moo_list {
    int count;
    int elem[];
};

// arena
void       *__moo_arena_alloc(int size);
void        __moo_arena_reset(void);

// object properties
struct moo_prop *__moo_prop_get(const char *obj, struct moo_str *prop);
void             __moo_prop_set(const char *obj, struct moo_str *prop,
                                int val);

// object lifecycle
const char *__moo_obj_create(const char *parent);
void        __moo_obj_recycle(const char *obj);
void        __moo_obj_move(const char *obj, const char *dest);

// object queries
const char      *__moo_obj_location(const char *obj);
int              __moo_obj_valid(const char *obj);
struct moo_list *__moo_obj_contents(const char *obj);

// interface checking
int         __moo_obj_has_prop(const char *obj, struct moo_str *name);
int         __moo_obj_has_verb(const char *obj, struct moo_str *name);

// verb dispatch
void        __moo_verb_call(const char *obj, struct moo_str *verb,
                            int argc, ...);
```

`__moo_prop_get` returns `struct moo_prop *` (arena-allocated boxed
value with type tag and payload).

Path resolution is not part of the glue interface. The host resolves
object paths before invoking verbs — by the time a verb runs, `this`
and all object arguments are already resolved handles. Zone context
is implicit in the host's invocation state.

## Extern Declarations

Extern declarations let scripts declare host-provided functions with typed
signatures. The host provides the implementation at link time, just like
the `__moo_*` functions. No changes to the IR or backend are needed;
extern calls use the same `IR_ARG` + `IR_CALL` path as existing builtins.

### Syntax

    extern func random_range(lo: int, hi: int): int;
    extern verb play_sound(name: str);

Extern verbs are void (fire-and-forget). Extern funcs return a typed value.

### Link-Name Aliasing

An optional alias decouples the script-facing name from the C symbol:

    extern __moo_obj_move as verb move(obj: obj, dest: obj);
    extern engine_play_sfx as verb play_sound(name: str);
    extern engine_rand as func random_range(lo: int, hi: int): int;

The first identifier is the linker symbol. The identifier after `verb` or
`func` is the MooScript name used in script code. If the alias is omitted,
both names are the same.

### AST Representation

Two new node kinds: `N_EXTERN_VERB` and `N_EXTERN_FUNC`. Each carries:

- `name` -- script-facing name (e.g. `"move"`)
- `link_name` -- linker symbol (e.g. `"__moo_obj_move"`), or NULL if same
- `->a` -- parameter list (same as verb/func params)
- `->type` -- return type (extern func only)

No body.

### Lowering

In `lower_program()`, extern declarations are processed alongside
verb/func definitions during the initial scan:

- The script-facing name is registered in the verb table (so `is_verb()`
  returns true and calls resolve)
- The link name is added to the runtime symbol list (so the assembler
  emits an undefined reference and the linker resolves it)

When the `N_CALL` handler encounters a call to an extern name, it falls
through to the existing generic path: emit `IR_ARG` for each argument,
emit `IR_CALL` with the link name as the symbol.

### Host Implementation

The host provides C functions matching the declared signatures, following
the same calling convention as `__moo_*` functions (args pushed
right-to-left on stack, return value in d0/a0):

```c
int random_range(int lo, int hi)
{
    return lo + (rand() % (hi - lo + 1));
}

void play_sound(struct moo_str *name)
{
    engine_play_sfx(name->data, name->len);
}
```

### Prelude Pattern

With link-name aliasing, the existing hardcoded builtins can be
redeclared as extern declarations in a prelude file:

    // prelude.moo -- standard host interface
    extern __moo_obj_valid as func valid(o: obj): bool;
    extern __moo_obj_contents as func contents(o: obj): list<obj>;
    extern __moo_obj_location as func location(o: obj): obj;
    extern __moo_obj_move as verb move(o: obj, dest: obj);
    extern __moo_obj_create as func create(parent: obj): obj;
    extern __moo_obj_recycle as verb recycle(o: obj);

This turns the 30+ lines of special-case `strcmp` chains in `lower_expr`
into data. A JRPG host ships its own prelude with no `__moo_*` functions;
a MOO host ships the standard one.

## Modules

Modules enable separate compilation. Each `.moo` file is a module.
Scripts can export verbs, funcs, and constants, and import them from
other modules.

### Syntax

    module combat;

    import items;

    export func roll_damage(attacker: obj, weapon: obj): int
        var base: int = weapon.damage;
        var bonus: int = attacker.strength / 4;
        return base + bonus;
    endfunc

    export verb resolve_attack(attacker: obj, target: obj)
        var wpn: obj = equipped_weapon(attacker);
        var dmg: int = roll_damage(attacker, wpn);
        var hp: int = target.hp - dmg;
        target.hp = hp;
    endverb

    func equipped_weapon(who: obj): obj
        return who.weapon;
    endfunc

`module` declares the module name. `import` brings another module's
exported symbols into scope. `export` marks a verb, func, or const as
public.

### Compilation Model

Each `.moo` file compiles independently to a `.o`:

    skj-moo -c -o build/combat.o combat.moo
    skj-moo -c -o build/dialogue.o dialogue.moo

When compiling a file with `import combat;`, the compiler reads
`combat.moo` to extract exported declarations (signatures, const values,
interface definitions). It parses only top-level structure, skipping
function bodies. This gives it enough to type-check cross-module calls
without generating code for the imported module.

No separate interface file format for v1. The source file is the
interface. This matches how Oberon originally worked.

### Symbol Visibility

Without modules, all verbs are global and all funcs are local:

    fn->is_local = (fn_ast->kind == N_FUNC);

With modules, visibility is controlled by the `export` keyword:

    fn->is_local = !fn_ast->exported;

Exported symbols get `.globl` in the assembly output. Non-exported verbs
and funcs get `.local`, invisible to the linker.

### No Name Mangling (v1)

Symbols use their bare names at the linker level. `roll_damage` in module
`combat` is just `roll_damage` to the linker. If two modules export the
same name, you get a linker error. This matches C's model.

Import brings names into scope unqualified:

    var dmg: int = roll_damage(attacker, wpn);

Qualified name syntax (e.g. `combat.roll_damage()`) is deferred to v2,
if name collisions become a problem in practice.

### Build Integration

    MODULES = combat dialogue inventory movement

    build/%.o: %.moo $(wildcard *.moo)
    	skj-moo -c -o $@ $<

    build/game: $(MODULES:%=build/%.o) build/host_jrpg.o build/start.o
    	skj-ld -o $@ $^

The conservative `$(wildcard *.moo)` dependency triggers recompilation
of all modules on any source change. A dependency scanner or compiler
`-MD` flag can be added later.

### smolmoo Implementation

For smolmoo, compiled verbs are ColdFire ELF binaries loaded into
the VM's 128KB memory space. The `__moo_*` functions are implemented
as LINE_A hypercalls (trap into the host). `vm_args` at fixed address
0x380 provides the verb invocation context. Verb ELFs are stored in
a content-addressable depot.

## Compilation Pipeline

    MooScript source (.moo)
        │
        ▼
    Lexer ──► token stream
        │
        ▼
    Parser (recursive descent + precedence climbing) ──► AST
        │
        ▼
    Type checker ──► typed AST
        │
        ▼
    Lowering ──► IR (extended TinC opcodes)
        │         - property access → host glue IR_CALL
        │         - verb calls → host dispatch IR_CALL
        │         - string/list ops → runtime library IR_CALL
        │
        ▼
    Register allocator (linear scan)
        │
        ├──► ColdFire backend (cf_emit.c) ──► m68k assembly ──► ELF
        ├──► Bytecode backend ──► stack VM bytecode (development)
        └──► (future: WASM, Q3VM, etc.)

## Building

### Prerequisites

Install the m68k cross-compilation toolchain and QEMU user-mode emulator:

    sudo apt install gcc-m68k-linux-gnu binutils-m68k-linux-gnu qemu-user

### Host compiler

    make                    # builds build/mooc (native x86-64)

### Cross-compiled compiler

    make mooc-m68k          # builds build/mooc-m68k (static m68k ELF)
    qemu-m68k build/mooc-m68k -o out.s input.moo

The m68k build is statically linked against glibc so it runs under
`qemu-m68k` with no sysroot needed.  It produces byte-identical assembly
to the native host compiler.

### Test programs

    make check              # compile all tests/*.moo, link with m68k runtime,
                            # run under qemu-m68k, verify exit codes

Each `.moo` test is compiled to m68k assembly by `build/mooc`, assembled
and linked with the bare-metal runtime (`runtime/start.S`, `str.c`,
`list.c`, and a host stub), then executed under `qemu-m68k`.

## Verb Dispatch at Runtime

### Development (qemu-m68k, bytecode VM)

Option A: verb name passed as argv[1] (or register d0 pointing to
string). `_start` contains a jump table dispatching to verb functions.
Each invocation is a process execution.

### Production (bare metal ColdFire)

Option C: long-running task. `_start` registers verb handlers with the
host (like signal handler registration). Host sends verb invocations as
messages. Task stays alive, each invocation gets a fresh arena.

    // pseudocode for _start under Option C
    _start:
        register_verb("look", _verb_look)
        register_verb("take", _verb_take)
        register_verb("drop", _verb_drop)
        ready()    // signal host: ready for invocations
        // host calls registered functions directly from here

## Built-in Functions

### Core (compiled into language)

    typeof(val) → int        // type tag (0=int 1=str 2=obj 3=list 4=err 5=bool 6=float)
    tostr(val) → str         // string conversion (int, float, err)
    toint(val) → int         // integer parse/coerce (str, float)
    tofloat(val) → float     // float conversion (int, str)
    length(val) → int        // string or list length
    valid(obj) → bool        // object reference valid?

### String (runtime library)

    substr(s, start, end) → str     // substring (1-indexed)
    index(s, sub) → int             // find substring, 0 = not found
    strsub(s, old, new) → str       // substitute first occurrence

### List (runtime library)

    listappend(lst, val) → list     // new list with val appended
    listdelete(lst, idx) → list     // new list without element at idx
    listset(lst, idx, val) → list   // new list with element replaced

### Host (through glue interface)

    move(obj, dest)                 // move object to destination
    create(parent) → obj            // create from prototype
    recycle(obj)                    // destroy object
    contents(obj) → list<obj>       // contained objects
    location(obj) → obj             // containing object

## Debug Output

Two mechanisms, different purposes:

### `///` trace comments

`///` is a trace comment. When tracing is enabled, the text after `///`
is emitted as a raw string to debug output. When tracing is disabled,
the lexer discards it as a regular comment. No expression evaluation —
just static text.

    /// verb look fired
    /// checking exits for this room
    /// about to move player

Syntax highlighters treat `///` as a comment variant — existing editor
support for `//` comments extends naturally. The distinction between
"real" comments (`//`) and trace comments (`///`) is visible at a glance.

### `trace` statement

For computed debug messages with variable interpolation:

    trace "player " + player.name + " entered " + this.name;

`trace` is a keyword, not a function. It takes a `str` expression.

### Backend mapping

| Target              | Both forms emit                              |
|---------------------|----------------------------------------------|
| ColdFire bare metal | HC_PRINT hypervisor call                     |
| qemu-m68k           | `write(STDERR_FILENO, data, len)`            |
| Bytecode VM         | host callback to stderr or log sink          |

`///` comments are cheap — the string literal is baked into the
binary's rodata and written directly. `trace` statements with
concatenation allocate a flat buffer from the arena.

### Compile-time stripping

A compiler flag (`-Drelease` or similar) strips all `trace` statements
and `///` comments from the output. The parser still validates `trace`
syntax, but the lowering pass emits no IR. `///` comments are simply
not emitted by the lexer. Analogous to C's `assert()`/`NDEBUG`.

## Methods and Interfaces

### Method Calls on Value Types

The colon operator `:` dispatches method calls on any type. For `obj`,
the call goes through the host as before. For value types (`int`, `str`,
`list<T>`, `bool`, `err`), the compiler resolves the method at compile
time to a direct call -- no vtable, no runtime dispatch:

    var n: int = name:length();
    var bigger: list<int> = items:append(42);
    var label: str = count:tostr();

The traditional function-call syntax remains valid. Method syntax is
sugar -- the compiler lowers both forms to the same IR:

    length(name)          // equivalent to name:length()
    listappend(items, 42) // equivalent to items:append(42)

### Built-in Methods

| Type      | Method                    | Function equivalent         | Returns   |
|-----------|---------------------------|-----------------------------|-----------|
| `str`     | `:length()`               | `length(s)`                 | `int`     |
| `str`     | `:substr(start, end)`     | `substr(s, start, end)`     | `str`     |
| `str`     | `:index(sub)`             | `index(s, sub)`             | `int`     |
| `str`     | `:strsub(old, new)`       | `strsub(s, old, new)`       | `str`     |
| `list<T>` | `:length()`               | `length(lst)`               | `int`     |
| `list<T>` | `:append(val)`            | `listappend(lst, val)`      | `list<T>` |
| `list<T>` | `:delete(idx)`            | `listdelete(lst, idx)`      | `list<T>` |
| `list<T>` | `:set(idx, val)`          | `listset(lst, idx, val)`    | `list<T>` |
| `int`     | `:tostr()`                | `tostr(n)`                  | `str`     |
| `int`     | `:tofloat()`              | `tofloat(n)`                | `float`   |
| `float`   | `:tostr()`                | `tostr(f)`                  | `str`     |
| `float`   | `:toint()`                | `toint(f)`                  | `int`     |

### Interfaces

An interface declares the properties and verbs an `obj` must support.
Interfaces are compile-time contracts verified at runtime by the host
-- duck typing. If the object has the required properties and verbs,
it satisfies the interface:

    interface Describable
        name: str;
        description: str;
    endinterface

    interface Tellable
        name: str;
        tell(msg: str);
    endinterface

Properties are listed as `name: type`. Verbs are listed as
`name(params)` -- always void per MooScript's rules.

### Interface-Typed Variables

Use an interface name wherever `obj` would appear -- as a variable
type, parameter type, or list element type:

    verb look(this: Describable, player: Tellable)
        player:tell(this.name);
        player:tell(this.description);
    endverb

The compiler checks property types and verb signatures against the
interface declaration. At runtime, `obj.prop` and `obj:verb()` still
dispatch through the host -- no vtable, no changed calling convention.
The interface adds a type-safety layer on top.

Widening from interface to `obj` is always safe:

    var o: obj = d;               // OK -- widening, no check needed

Narrowing from `obj` to an interface requires an explicit cast with
`as`, which inserts a runtime check:

    var thing: obj = $"sword";
    var d: Describable = thing as Describable;

The `as` cast calls the host to verify the object has every property
and verb declared in the interface. If any check fails, it panics
with `$E_TYPE`. The defer/panic/recover mechanism handles propagation
-- no special try/catch syntax needed:

    defer
        var e: err = recover();
        if (e != nil)
            player:tell("Not a describable object.");
        endif
    enddefer
    var d: Describable = thing as Describable;
    // if we get here, d is valid
    player:tell(d.name);

Bare assignment from `obj` to an interface type without `as` is a
compile-time error:

    var d: Describable = thing;   // ERROR -- use 'as' to narrow

Interface-typed parameters work the same way -- the caller must pass
an interface-typed value or use `as` at the call site:

    verb look(this: Describable, player: Tellable)
        player:tell(this.name);
        player:tell(this.description);
    endverb

    // calling:
    var room: obj = $"church";
    room as Describable:look(player as Tellable);

### Runtime Interface Checking (`is` and `as`)

Two operators for interface conformance:

`as` -- narrowing cast with runtime check. Panics with `$E_TYPE` on
failure. Use when you expect the object to conform:

    var d: Describable = thing as Describable;

`is` -- boolean test, no panic. Use when you want to branch:

    if (thing is Describable)
        // compiler narrows 'thing' to Describable in this block
        player:tell(thing.name);
    endif

Both call into the host to verify the object has every property and
verb declared in the interface. Short-circuits on the first missing
property or verb.

### Host Glue Additions

Runtime interface checking uses `__moo_obj_has_prop` and
`__moo_obj_has_verb` (declared in the host glue interface above).
The compiler generates a sequence of these calls for each `is` check
or type switch case, short-circuiting on the first failure.

### What Interfaces Are Not

Interfaces do not:
- Generate vtables or change how `obj:verb()` dispatches
- Define object structure -- the host owns that
- Support inheritance -- keep them flat and specific
- Work on value types -- `int`, `str`, `list` have known capabilities
  at compile time; interfaces are for the `obj` black box

The `obj` type remains special. The compiler cannot verify at compile
time that a particular object satisfies an interface -- narrowing from
`obj` to an interface always requires an explicit `as` cast (runtime
check) or an `is` test (boolean branch). Bare assignment from `obj`
to an interface-typed variable is a compile-time error.

## Deviations from LambdaMOO

### Error Handling: defer/panic/recover vs try-except

LambdaMOO's error handling is a core language feature with three
complementary mechanisms:

1. **try-except blocks** -- catch specific error codes with handler
   clauses, each receiving a 4-element error info list
2. **try-finally blocks** -- guaranteed cleanup regardless of error
3. **Inline error catching** -- backtick syntax for expression-level
   error handling: `` `expr ! E_PERM => default` ``
4. **raise()** -- explicitly raise an error value up the call stack

MooScript replaces all of these with Go-style defer/panic/recover:

- `defer ... enddefer` replaces both try-finally (guaranteed cleanup)
  and try-except (via `recover()` inside a defer block)
- `panic(err)` replaces `raise(error_code)`
- `recover()` inside a defer block catches the panic value

This is a deliberate deviation. The try-except model requires the
compiler to generate exception tables or implement stack unwinding.
defer/panic/recover is simpler to compile -- defer blocks are just
LIFO cleanup callbacks, and panic triggers them sequentially. The
tradeoff: error handling in MooScript is less granular than LambdaMOO.
You can't catch a specific error code inline at an expression; you
must wrap the panicking code in a function boundary and recover in a
defer block.

The inline error catching expression (`` `expr ! codes => fallback` ``)
has no MooScript equivalent. Code that would use it must use a
defer/recover pattern or restructure to avoid the error case.

### Case Sensitivity

LambdaMOO string comparison (`==`) and `index()` are case-insensitive
by default. MooScript is case-sensitive everywhere. LambdaMOO provides
`strcmp()` and `equal()` for explicit case-sensitive comparison, and
`index()` takes an optional third argument to enable case-sensitivity.

MooScript's case-sensitive default is intentional -- it matches the
behavior of every other compiled language and avoids hidden performance
costs from case folding. A `strcmp()` or case-insensitive `index()`
variant can be added later if needed.

### strsub: First Occurrence vs All

LambdaMOO's `strsub()` replaces **all** occurrences of the pattern.
MooScript's `strsub()` currently replaces only the **first**
occurrence. This is a known deviation. Replacing all occurrences
requires a loop in the runtime; replacing the first is the simpler
and more predictable primitive from which replace-all can be built.

### Truth Values

LambdaMOO treats object references and error values as always falsy,
regardless of their value (even `#123` or `E_PERM` are false). Empty
strings and empty lists are also false. Non-zero integers and
non-empty strings/lists are true.

MooScript uses conventional boolean semantics: 0/false is false,
everything else (including objects and errors) is true. This matches
what programmers from other languages expect.

### What We Dropped

| Feature                          | Reason                                    |
|----------------------------------|-------------------------------------------|
| Dynamic typing                   | Static types for compilation               |
| `fork` (delayed tasks)           | Host provides scheduling                   |
| Dynamic verb dispatch            | Static resolution at compile time          |
| Heterogeneous lists              | Typed lists simpler to compile             |
| `pass()` to parent verb          | Host handles prototype chain               |
| Wizard/programmer permission bits| Host-side security policy                  |
| Tick counting                    | Host wraps execution with limits           |
| `$foo` system object shorthand   | Replaced by `$"path"` zone-based object refs |
| Verb return values               | Verbs are void; fire-and-forget dispatch    |

### Not Yet Implemented (May Add Later)

| Feature                          | Notes                                           |
|----------------------------------|--------------------------------------------------|
| Ternary `expr ? true_val \| alt` | LambdaMOO uses `?` / `\|`; add if/when needed   |
| `$` in index expressions         | `mylist[$]` = last element; syntactic sugar      |
| List splicing `@`                | `{@list1, @list2}` and `verb(@args)` expansion   |
| Scattering assignment            | `{a, b, @rest} = list` destructuring             |
| Named loops                      | `for name (var) in ...` for targeted break/continue |
| `rindex(s, sub)`                 | Find last occurrence of substring                |
| `listinsert(lst, val [, idx])`   | Insert before index (vs listappend after)        |
| `setadd(lst, val)`               | Add to list if not already present               |
| `setremove(lst, val)`            | Remove first occurrence from list                |
| `is_member(val, lst)`            | Case-sensitive list membership                   |
| `toliteral(val)`                 | Value to MOO literal string representation       |
| `match(s, pattern)`              | Regular expression matching                      |
| Exponentiation `^`               | Integer/float exponentiation operator            |

## Example: Complete Verb File

```
// room_verbs.moo — verbs for generic room prototype

verb look(this: obj, player: obj)
    player:tell(this.name);
    player:tell(this.description);

    var here: list<obj> = contents(this);
    if (length(here) > 0)
        player:tell("You see:");
        for item in here
            if (item != player)
                player:tell("  " + item.name);
            endif
        endfor
    endif

    var exits: str = this.exits;
    if (exits != nil)
        player:tell("Exits: " + exits);
    endif
endverb

verb say(this: obj, player: obj, text: str)
    var msg: str = player.name + " says, \"" + text + "\"";
    for who in contents(this)
        who:tell(msg);
    endfor
endverb

verb go(this: obj, player: obj, direction: str)
    /// go verb entered
    var dest: str = this.("exit_" + direction);
    if (dest == nil)
        player:tell("You can't go that way.");
        return;
    endif
    trace "go: " + player.name + " heading " + direction;
    player:move_to(dest);
endverb
```
