// verb_inscribe.moo : carve a lasting inscription into the current room.
//
// A MooScript verb that writes shared state, proving three host bridges added
// in M71. `inscribe <text>` carves a message into the room, recording when;
// `inscribe` with no argument reads the current one back and reports how long
// ago it was made. It needs a setuid elevation to write the room object, the
// room resolved by objref, and a wall-clock timestamp.
//
//   getobj(player, "location") -> the room the player stands in, by objref
//   setpriv(1) / setpriv(0)    -> bracket the write to the shared room object
//   now()                      -> wall-clock seconds, stored and diffed
//
// hasprop guards the read: an unwritten room has no inscription to show. The
// timestamp is stored as text (tostr), as all property writes are; reading it
// back into an int relies on the host classifying a digit string as an int.

extern func obj_of(id: int) -> obj;
extern __moo_obj_has_prop as func hasprop(o: obj, name: str) -> bool;
extern host_getobj as func getobj(o: obj, name: str) -> obj;
extern host_broadcast as func broadcast(room: obj, msg: str) -> int;
extern sys_setpriv as func setpriv(on: int) -> int;
extern sys_now as func now() -> int;

verb main(player: obj, room: obj, this: obj, dobj: obj, iobj: obj, arg: str)
    var here: obj = getobj(player, "location");

    if (arg == "")
        if (hasprop(here, "inscription"))
            var text: str = here.inscription;
            var stamp: int = here.inscribed_at;
            var ago: int = now() - stamp;
            player:tell("Inscribed here: \"" + text + "\"");
            player:tell("Carved " + tostr(ago) + " seconds ago.");
        else
            player:tell("Nothing is inscribed here.");
        endif
        return;
    endif

    // name the author from their character sheet, falling back when unset
    var who: str = "Someone";
    var cid: int = player.charid;
    if (cid > 0)
        var sheet: obj = obj_of(cid);
        who = sheet.name;
    endif

    setpriv(1);
    here.inscription = arg;
    here.inscribed_at = tostr(now());
    setpriv(0);

    broadcast(here, who + " inscribes \"" + arg + "\" into the surroundings.");
endverb
