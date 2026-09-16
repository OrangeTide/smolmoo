// verb_testref.moo : exercise richer verb-call argument marshalling.
// Object arguments are routed to the target verb's dobj/iobj, and a string
// argument to its argstr, all through the compiler's argument type tags.
verb main(player: obj, room: obj)
    room:reflect(room);      // dobj == this (room)  -> REFLECT:MATCH
    room:reflect(player);    // dobj != this         -> REFLECT:NOMATCH
    room:greet("hi");        // string arg -> argstr  -> GREETED:hi
endverb
