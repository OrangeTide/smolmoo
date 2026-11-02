// verb_makechar.moo : MooScript character-creation wizard (attributes + hook).
//
// A guided front end to the ChromeSix character sheet, written in MooScript to
// exercise the language and the object model. It walks the two opening steps,
// attributes then the hook, writing them to the player's sheet, then hands off
// to `chargen` for skills and the finalize (kit and derived stats). Creation
// state lives in the sheet's `cg_step` property, so this verb and `chargen`
// share one flow and interoperate step for step.

// obj_of turns the integer `charid` into the sheet's object handle. MooScript
// has no int->obj cast, so the host bridge supplies one (sdk/runtime/host_vm.c).
extern func obj_of(id: int) -> obj;

// The k-th space-separated token of s (0-based), or "" when there is none.
func token(s: str, k: int) -> str
    var n: int = length(s);
    var i: int = 1;
    var count: int = 0;
    while (i <= n)
        while (i <= n)
            if (substr(s, i, i) != " ")
                break;
            endif
            i = i + 1;
        endwhile
        if (i > n)
            return "";
        endif
        var start: int = i;
        while (i <= n)
            if (substr(s, i, i) == " ")
                break;
            endif
            i = i + 1;
        endwhile
        if (count == k)
            return substr(s, start, i - 1);
        endif
        count = count + 1;
    endwhile
    return "";
endfunc

verb main(player: obj, room: obj, this: obj, dobj: obj, iobj: obj, arg: str)
    var cid: int = player.charid;
    if (cid <= 0)
        player:tell("You have no character sheet.");
        return;
    endif
    var ch: obj = obj_of(cid);
    if (ch.made == 1)
        player:tell("Your character is already set up.");
        return;
    endif

    var step: int = ch.cg_step;
    if (step <= 0 || step > 3)
        step = 1;
        ch.cg_step = "1";
    endif
    if (step == 3)
        player:tell("Attributes and hook are set. Run 'chargen' to assign skills and finish.");
        return;
    endif

    if (token(arg, 0) == "")
        if (step == 1)
            player:tell("Step 1/2 attributes: makechar <agi> <mig> <wit> <cha>");
            player:tell("  each 3 to 9, and the four must total 24");
        else
            player:tell("Step 2/2 hook: makechar <street|cyber|awakened>");
        endif
        return;
    endif

    if (step == 1)
        var a: str = token(arg, 0);
        var m: str = token(arg, 1);
        var w: str = token(arg, 2);
        var c: str = token(arg, 3);
        if (m == "" || w == "" || c == "")
            player:tell("Enter four numbers: agi mig wit cha.");
            return;
        endif
        var av: int = toint(a);
        var mv: int = toint(m);
        var wv: int = toint(w);
        var cv: int = toint(c);
        if (av < 3 || av > 9 || mv < 3 || mv > 9 || wv < 3 || wv > 9 || cv < 3 || cv > 9)
            player:tell("Each attribute must be 3 to 9.");
            return;
        endif
        var sum: int = av + mv + wv + cv;
        if (sum != 24)
            player:tell("Attributes must total 24 (you have " + tostr(sum) + ").");
            return;
        endif
        ch.agi = tostr(av);
        ch.mig = tostr(mv);
        ch.wit = tostr(wv);
        ch.cha = tostr(cv);
        ch.cg_step = "2";
        player:tell("Attributes set.");
        player:tell("Step 2/2 hook: makechar <street|cyber|awakened>");
        return;
    endif

    // step == 2: the hook
    var hk: str = token(arg, 0);
    if (hk != "street" && hk != "cyber" && hk != "awakened")
        player:tell("Hook must be street, cyber, or awakened.");
        return;
    endif
    ch.hook = hk;
    ch.cg_step = "3";
    player:tell("Hook set. Now run 'chargen' to assign skills and finish.");
endverb
