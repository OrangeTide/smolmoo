// verb_heft.moo : report the total weight the player is carrying.
//
// Proves the rollup host bridge added in M71. sys_rollup sums one numeric
// field over an object's whole containment subtree, cached and recomputed
// lazily by the host, so rollup(player, "weight") is the carried load in one
// call rather than a walk of the inventory. It is a pure read, so the verb is
// not setuid.

extern host_rollup as func rollup(o: obj, field: str) -> int;

verb main(player: obj, room: obj, this: obj, dobj: obj, iobj: obj, arg: str)
    var load: int = rollup(player, "weight");
    player:tell("You are carrying " + tostr(load) + " weight.");
endverb
