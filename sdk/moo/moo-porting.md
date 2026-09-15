# Porting MooScript to Non-MOO Games

MooScript is loosely coupled to MOO concepts. The language compiles to native
code and talks to the host game engine through exactly 10 C functions. The
compiled script never touches game state directly; it calls back into the host
for everything.

## What Maps to a JRPG (or Similar)

- **Objects** become entities: party members, NPCs, enemies, items, map
  locations. MooScript treats them as opaque handles; it doesn't care what's
  behind them.
- **Properties** become stats, inventory slots, flags, dialogue state. The host
  stores them however it wants (ECS, flat arrays, etc.).
- **Verbs** become actions: `attack`, `defend`, `use_item`, `talk`. The host
  dispatches them; MooScript just names them.
- **`$"path"` references** (like `$"rooms/lobby"`) could address entities by
  path, ID, or tile coordinate. It's just a string the host resolves.
- **defer/panic/recover** works well for turn-based logic where actions can fail
  (miss, blocked, out of MP).
- **Arena allocation** fits a turn-scoped model naturally: allocate during a
  turn, reset when it ends. No GC.

## Host Interface

The entire host integration layer is 10 C functions (see `DESIGN.md` for full
signatures):

```c
/* Arena management */
void       *__moo_arena_alloc(int size);
void        __moo_arena_reset(void);

/* Property access */
struct moo_prop *__moo_prop_get(const char *obj, struct moo_str *prop);
void             __moo_prop_set(const char *obj, struct moo_str *prop, int val);

/* Object lifecycle */
const char *__moo_obj_create(const char *parent);
void        __moo_obj_recycle(const char *obj);
void        __moo_obj_move(const char *obj, const char *dest);

/* Object queries */
const char      *__moo_obj_location(const char *obj);
int              __moo_obj_valid(const char *obj);
struct moo_list *__moo_obj_contents(const char *obj);
```

Plus interface checking (`__moo_obj_has_prop`, `__moo_obj_has_verb`) and verb
dispatch (`__moo_verb_call`).

## Steps to Port

1. **Implement the host functions** against your game's entity system.
   `runtime/toy_host.c` is a working example with 4 test objects that shows
   exactly what each function needs to do.

2. **Replace or extend error codes** (`$E_RECMOVE` etc.) with game-relevant
   ones.

3. **Declare host-provided builtins with `extern`** for things MooScript
   doesn't have natively (random numbers, sprite commands, sound triggers).
   No compiler changes needed; extern declarations add new callable names
   that the linker resolves against your host C code:

   ```
   extern func random_range(lo: int, hi: int): int;
   extern verb play_sound(name: str);
   extern verb text_box(speaker: str, msg: str);
   extern func tile_at(x: int, y: int): int;
   ```

   If your C function names differ from the script-facing names, use the
   alias form:

   ```
   extern engine_rand as func random_range(lo: int, hi: int): int;
   extern engine_play_sfx as verb play_sound(name: str);
   ```

4. **Split game logic across modules** using `module`/`import`/`export`.
   Each `.moo` file compiles independently to a `.o`; the linker resolves
   cross-module calls:

   ```
   // combat.moo
   module combat;

   export verb resolve_attack(attacker: obj, target: obj)
       var dmg: int = attacker.strength - target.defense;
       dmg = dmg + random_range(-2, 2);
       if (dmg < 1)
           dmg = 1;
       endif
       target.hp = target.hp - dmg;
   endverb
   ```

   ```
   // dialogue.moo
   module dialogue;

   import combat;

   export verb challenge(npc: obj, player: obj)
       player:tell(npc.name + " attacks!");
       resolve_attack(npc, player);
   endverb
   ```

## Prelude Pattern

A host can ship a prelude file that redeclares the standard `__moo_*`
interface under clean names using link-name aliasing:

```
// prelude_moo.moo -- standard MOO host interface
extern __moo_obj_valid as func valid(o: obj): bool;
extern __moo_obj_contents as func contents(o: obj): list<obj>;
extern __moo_obj_location as func location(o: obj): obj;
extern __moo_obj_move as verb move(o: obj, dest: obj);
extern __moo_obj_create as func create(parent: obj): obj;
extern __moo_obj_recycle as verb recycle(o: obj);
```

A JRPG host ships its own prelude with no `__moo_*` functions at all.
The same MooScript source can drive a MUD or a JRPG depending on which
prelude and host are linked.
