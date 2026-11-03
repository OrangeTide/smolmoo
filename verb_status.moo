// verb_status.moo : a one-line vitals readout (rules Section 12), MooScript
// port of verb_status.c. The HP and Grit bars, the wound ladder, and any
// active conditions, for a glance without opening the full sheet.
//
// This is the first MooScript verb to use the ChromeSix gameplay library, by
// way of the cs_moo.c bridge (M72): geti reads sheet props with a fallback,
// maxbp/maxgrit unpack the derived stats, and rest pays out short-rest BP
// before the readout. The bars and condition list are built with MooScript's
// own strings (no cs_out bridge), so the output helpers stay native. It reads
// only the caller's own sheet, so it is not setuid.

extern func obj_of(id: int) -> obj;
extern cs_moo_geti as func geti(o: obj, name: str, dflt: int) -> int;
extern cs_moo_maxbp as func maxbp(o: obj) -> int;
extern cs_moo_maxgrit as func maxgrit(o: obj) -> int;
extern cs_moo_rest as verb rest(sheet: obj, room: obj);

// A ten-cell bar, "LABEL [####------] cur/max", matching cs_meter.
func meter(label: str, cur: int, max: int) -> str
    var m: int = max;
    var c: int = cur;
    if (m < 1)
        m = 1;
    endif
    if (c < 0)
        c = 0;
    endif
    var filled: int = c * 10 / m;
    if (filled > 10)
        filled = 10;
    endif
    var bar: str = label + " [";
    var i: int = 0;
    while (i < 10)
        if (i < filled)
            bar = bar + "#";
        else
            bar = bar + "-";
        endif
        i = i + 1;
    endwhile
    return bar + "] " + tostr(c) + "/" + tostr(m);
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
        player:tell("You have no character sheet. Type 'chargen' to create one.");
        return;
    endif
    var sheet: obj = obj_of(cid);
    if (geti(sheet, "made", 0) < 1)
        player:tell("You haven't set up your character yet. Type 'chargen' to begin.");
        return;
    endif

    rest(sheet, room);              // pay out short-rest BP before the readout

    var mbp: int = maxbp(sheet);
    var mgr: int = maxgrit(sheet);
    var line: str = meter("HP", geti(sheet, "bp", mbp), mbp);
    line = line + "  " + meter("GRIT", geti(sheet, "grit", mgr), mgr);

    var wounds: int = geti(sheet, "wounds", 0);
    if (wounds > 3)
        wounds = 3;
    endif
    line = line + "  WOUNDS ";
    var i: int = 0;
    while (i < 3)
        if (i < wounds)
            line = line + "o";
        else
            line = line + ".";
        endif
        i = i + 1;
    endwhile

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
        line = line + "  (" + c + ")";
    endif

    player:tell(line);
endverb
