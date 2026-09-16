// verb_look.moo : room look verb, MooScript port of verb_look.c.
// Prints the current room's name and description to the player.
verb main(player: obj, room: obj)
    var name: str = room.name;
    var desc: str = room.description;
    player:tell("=== " + name + " ===");
    player:tell(desc);
endverb
