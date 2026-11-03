// verb_sheet.moo : display a ChromeSix character sheet. MooScript port of
// verb_sheet.c, on the cs_moo.c gameplay bridge (M73).
//
// The layout is built with native MooScript strings: the name header, the four
// attributes as dice pools, the derived defence line, the resource line, the
// carry load, and active conditions. The three data-dense lines that carry
// their own gameplay vocabulary (skills, faction standing, earned clearances)
// are built by the C library and returned as strings, rather than rebuilding
// that vocabulary here. Reads only the caller's own sheet, so it is not setuid.

extern func obj_of(id: int) -> obj;
extern __moo_obj_has_prop as func hasprop(o: obj, name: str) -> bool;
extern host_rollup as func rollup(o: obj, field: str) -> int;
extern cs_moo_geti as func geti(o: obj, name: str, dflt: int) -> int;
extern cs_moo_pd as func pd(o: obj) -> int;
extern cs_moo_soak as func soak(o: obj) -> int;
extern cs_moo_maxbp as func maxbp(o: obj) -> int;
extern cs_moo_maxgrit as func maxgrit(o: obj) -> int;
extern cs_moo_carry as func carry(o: obj) -> int;
extern cs_moo_rest as verb rest(sheet: obj, room: obj);
extern cs_moo_decay as verb decay(sheet: obj);
extern cs_moo_skills_line as func skills_line(sheet: obj) -> str;
extern cs_moo_standing_line as func standing_line(sheet: obj) -> str;
extern cs_moo_clearances_line as func clearances_line(sheet: obj) -> str;

// Dice notation for a pool of pts points: "ND" or "ND+P" (three pips a die).
func dice(pts: int) -> str
    var p: int = pts;
    if (p < 0)
        p = 0;
    endif
    var d: int = p / 3;
    var pips: int = p - d * 3;
    var s: str = tostr(d) + "D";
    if (pips > 0)
        s = s + "+" + tostr(pips);
    endif
    return s;
endfunc

// Append one condition label to a comma list when its flag is set.
func cond(sheet: obj, flag: str, label: str, acc: str) -> str
    if (geti(sheet, flag, 0) > 0)
        if (acc == "")
            return label;
        endif
        return acc + ", " + label;
    endif
    return acc;
endfunc

verb main(player: obj, room: obj)
    var cid: int = player.charid;
    if (cid < 1)
        player:tell("You have no character sheet.");
        return;
    endif
    var sheet: obj = obj_of(cid);
    if (geti(sheet, "made", 0) < 1)
        player:tell("You haven't set up your character yet. Type 'chargen' to begin.");
        return;
    endif

    rest(sheet, room);              // pay out any short-rest BP before display

    var mbp: int = maxbp(sheet);
    var mgr: int = maxgrit(sheet);

    player:tell("=== " + sheet.name + " ===");

    var attrs: str = "Agility " + dice(geti(sheet, "agi", 0));
    attrs = attrs + "  Might " + dice(geti(sheet, "mig", 0));
    attrs = attrs + "  Wit " + dice(geti(sheet, "wit", 0));
    attrs = attrs + "  Charm " + dice(geti(sheet, "cha", 0));
    player:tell(attrs);

    var def: str = "Defense " + tostr(pd(sheet)) + "  Soak " + tostr(soak(sheet));
    def = def + "  BP " + tostr(geti(sheet, "bp", mbp)) + "/" + tostr(mbp);
    def = def + "  Grit " + tostr(geti(sheet, "grit", mgr)) + "/" + tostr(mgr);
    player:tell(def);

    var res: str = "Creds " + tostr(geti(sheet, "money", 0));
    res = res + "  Stims " + tostr(geti(sheet, "stims", 0));
    res = res + "  CP " + tostr(geti(sheet, "cp", 0));
    var crash: int = geti(sheet, "crash", 0);
    if (crash > 0)
        res = res + "  Crash -" + tostr(crash) + "D";
    endif
    player:tell(res);

    var cap: int = carry(sheet);
    var load: int = rollup(player, "weight");
    var ld: str = "Load " + tostr(load) + "/" + tostr(cap);
    if (load > cap * 2)
        ld = ld + " (overloaded)";
    else
        if (load > cap)
            ld = ld + " (encumbered)";
        endif
    endif
    player:tell(ld);

    var c: str = "";
    c = cond(sheet, "prone", "prone", c);
    c = cond(sheet, "stunned", "stunned", c);
    c = cond(sheet, "shaken", "shaken", c);
    c = cond(sheet, "bleed", "bleeding", c);
    c = cond(sheet, "suppress", "suppressed", c);
    c = cond(sheet, "exposed", "exposed", c);
    c = cond(sheet, "marked_by", "marked", c);
    c = cond(sheet, "blind", "blinded", c);
    c = cond(sheet, "held", "held", c);
    if (c != "")
        player:tell("Conditions: " + c);
    endif

    if (hasprop(sheet, "maneuvers"))
        var mv: str = sheet.maneuvers;
        if (mv != "")
            player:tell("Maneuvers: " + mv);
        endif
    endif

    var sk: str = skills_line(sheet);
    if (sk != "")
        player:tell(sk);
    endif

    decay(sheet);                   // fade idle faction standing before showing
    var st: str = standing_line(sheet);
    if (st != "")
        player:tell(st);
    endif
    var cl: str = clearances_line(sheet);
    if (cl != "")
        player:tell(cl);
    endif
endverb
