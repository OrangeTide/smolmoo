// verb_check.moo : roll a ChromeSix skill check against a TN. MooScript port of
// verb_check.c, on the cs_moo.c gameplay bridge (M73).
//
// Usage: check <skill> [tn] [push N]. The skill names a pool (its governing
// attribute plus the ranked skill), rolled against a target number (default
// 10); "push N" spends Grit for N extra dice. The dice pool, the roll, the
// outcome and its margin, and any edge, glitch, or push tag are announced to
// the roller. The arg is parsed in MooScript; the gameplay math (the pool, the
// Grit spend, the roll with its wild die) crosses the bridge.

extern func obj_of(id: int) -> obj;
extern cs_moo_parse_push as func parse_push(arg: str) -> int;
extern cs_moo_skill_idx as func skill_idx(name: str) -> int;
extern cs_moo_grit_push as func grit_push(sheet: obj, n: int) -> int;
extern cs_moo_skill_pool as func skill_pool(sheet: obj, name: str, bonus: int) -> int;
extern cs_moo_roll as func roll_pool(pts: int) -> int;
extern cs_moo_wild as func wild() -> int;

// The k-th space-separated token of s, or "" past the end (1-indexed substr).
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

verb main(player: obj, room: obj, this: obj, dobj: obj, iobj: obj, arg: str)
    var cid: int = player.charid;
    if (cid < 1)
        player:tell("You have no character sheet.");
        return;
    endif
    if (arg == "")
        player:tell("Usage: check <skill> [tn] [push N]");
        return;
    endif
    var sheet: obj = obj_of(cid);

    var skill: str = token(arg, 0);
    if (skill_idx(skill) < 0)
        player:tell("Unknown skill.");
        return;
    endif

    // spend Grit for a push before rolling; refuse if the gauge is too low
    var push: int = parse_push(arg);
    var bonus: int = 0;
    if (push > 0)
        bonus = grit_push(sheet, push);
        if (bonus < 0)
            player:tell("You don't have the Grit to push that hard.");
            return;
        endif
    endif

    // the target number is the second token when numeric, else 10 (not "push")
    var tn: int = 10;
    var t1: str = token(arg, 1);
    if (t1 != "" && t1 != "push")
        tn = toint(t1);
        if (tn < 1)
            tn = 10;
        endif
    endif

    var pts: int = skill_pool(sheet, skill, bonus);
    var r: int = roll_pool(pts);
    var w: int = wild();

    var line: str = "CHECK " + skill + ": pool " + dice(pts);
    line = line + " roll " + tostr(r) + " vs " + tostr(tn);
    if (r >= tn)
        line = line + " -> SUCCESS (margin " + tostr(r - tn) + ")";
    else
        line = line + " -> FAIL (short " + tostr(tn - r) + ")";
    endif
    if (w > 0)
        line = line + " [EDGE]";
    else
        if (w < 0)
            line = line + " [GLITCH]";
        endif
    endif
    if (push > 0)
        line = line + " [pushed +" + tostr(push) + "D]";
    endif
    player:tell(line);
endverb
