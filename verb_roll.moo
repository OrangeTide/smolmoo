// verb_roll.moo : roll a die and announce it to the room (MooScript).
//
// A self-contained demonstration that a MooScript verb can produce room output
// and use randomness, through the broadcast and random host bridges added in
// M70. `roll` with no argument rolls a six-sided die; `roll <sides>` rolls a
// d<sides> (clamped to a sane range). The result is announced to the whole
// room, the roller included, since a dice roll is public.

// obj_of turns the integer charid into the sheet handle (no int->obj cast in
// the language; the host bridge supplies one). random and broadcast are the
// host syscalls, bound here under clean MooScript names.
extern func obj_of(id: int) -> obj;
extern sys_random as func random(max: int) -> int;
extern host_broadcast as func broadcast(room: obj, msg: str) -> int;

verb main(player: obj, room: obj, this: obj, dobj: obj, iobj: obj, arg: str)
    var sides: int = toint(arg);
    if (sides < 2)
        sides = 6;
    endif
    if (sides > 1000)
        sides = 1000;
    endif

    // name the roller from their character sheet, falling back when unset
    var who: str = "Someone";
    var cid: int = player.charid;
    if (cid > 0)
        var sheet: obj = obj_of(cid);
        who = sheet.name;
    endif

    var n: int = 1 + random(sides);
    broadcast(room, who + " rolls a d" + tostr(sides) + " and gets " + tostr(n) + ".");
endverb
