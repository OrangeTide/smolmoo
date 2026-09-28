/* verb_jobs.c : accept and track a job contract (M37a). A job giver is a builder
 * object marked `job`=1 that carries a contract in props: a `job_desc` line, a
 * goal (a `job_target` creature for a bounty, or a `job_dest` room for a courier,
 * M37c), and the reward (`job_cp`, `job_creds`, and an optional `job_standing`
 * "faction:step"). An optional `job_min` "faction:step" gates the contract behind
 * a minimum standing, read through the giver's disposition so its `disp` offset
 * can vouch for you (M38a, M38c). An optional `job_cd` cooldown in seconds keeps
 * a contract from being farmed back to back (M39c). An optional `job_need`
 * names a campaign flag the character must carry on the sheet's `flags` list
 * before the contract is offered or accepted, so one contract can gate behind an
 * earlier one (M47a). An optional `job_grant` names a flag that `turnin` adds to
 * the sheet's `flags` list, the token that opens a later `job_need` contract
 * (M47b). A player holds one
 * job at a time, stored on the sheet as `job_giver` (the giver's id, 0 for none)
 * and `job_done` (the completion flag, set in M37b). `jobs` lists what is offered
 * here and your active contract, marking a gated one locked; `accept` takes one
 * you rank for; `abandon` drops it; `turnin`
 * (at the giver, once `job_done` is set) pays the reward into the sheet's `cp` and
 * `money` (M37b). The player owns the sheet, so this runs at caller authority,
 * not setuid. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* parse a "faction:step" string (a reward or a gate) into `fac` and a signed
 * `step`. Returns 1 on a well-formed pair, 0 otherwise. */
static int
job_fac_step(const char *s, char *fac, int faclen, int *step)
{
    int k = 0, sign = 1, v = 0;

    while (s[k] && s[k] != ':' && k < faclen - 1) { fac[k] = s[k]; k++; }
    fac[k] = '\0';
    if (!fac[0] || s[k] != ':') return 0;
    k++;
    if (s[k] == '-') { sign = -1; k++; }
    while (s[k] >= '0' && s[k] <= '9') { v = v * 10 + (s[k] - '0'); k++; }
    *step = sign * v;
    return 1;
}

/* print one giver's offer: its name, description, reward, and, when `ch` does
 * not meet the giver's `job_min` gate, a locked marker naming the band needed */
static void
show_offer(struct cs_out *o, int giver, int ch)
{
    char nm[32], desc[128], st[64], fac[32];
    int need;

    cs_getstr(giver, "name", nm, sizeof(nm));
    cs_getstr(giver, "job_desc", desc, sizeof(desc));
    cs_s(o, "  ");
    cs_s(o, nm);
    cs_s(o, ": ");
    cs_s(o, desc[0] ? desc : "a job");
    cs_s(o, " [");
    cs_i(o, cs_geti(giver, "job_cp", 0));
    cs_s(o, " CP, ");
    cs_i(o, cs_geti(giver, "job_creds", 0));
    cs_s(o, " creds]");
    cs_getstr(giver, "job_min", st, sizeof(st));
    if (job_fac_step(st, fac, sizeof(fac), &need)
            && cs_disposition(giver, ch, fac) < need) {
        cs_s(o, " (locked: needs ");
        cs_s(o, fac);
        cs_s(o, " ");
        cs_s(o, cs_standing_word(need));
        cs_s(o, ")");
    }
    /* M47a: a chain contract is locked until an earlier one grants its flag */
    {
        char pre[32];

        cs_getstr(giver, "job_need", pre, sizeof(pre));
        if (pre[0] && !cs_has_flag(ch, pre)) {
            cs_s(o, " (locked: requires ");
            cs_s(o, pre);
            cs_s(o, ")");
        }
    }
    cs_s(o, "\n");
}

/* the per-giver cooldown prop on the sheet: "cd<giver id>" holds the time this
 * giver's contract can be taken again (M39c). */
static void
cd_name(char *dst, int giver)
{
    dst[0] = 'c';
    dst[1] = 'd';
    cs_itoa(dst + 2, giver);
}

/* M52b: breadth-first from `from` toward `goal`, following exit dest props, and
 * copy the direction name of the first exit on a shortest path into `dir`.
 * Returns 1 when a path is found (dir set), 0 when the goal is unreachable within
 * the cap. The caller handles from == goal (already there) before calling. */
static int
path_step(int from, int goal, char *dir, int dirlen)
{
    int seen[32], n = 0, i;
    char step[32][16];

    seen[n] = from;
    step[0][0] = '\0';
    n++;
    for (i = 0; i < n; i++) {
        int r = seen[i], e = 0;

        while ((e = sys_next(r, e)) != 0) {
            char dst[16], en[16];
            const char *src;
            int d, j, dup = 0, k;

            cs_getstr(e, "dest", dst, sizeof(dst));
            if (!dst[0]) continue;
            d = cs_atoi(dst);
            if (d <= 0) continue;
            for (j = 0; j < n; j++)
                if (seen[j] == d) { dup = 1; break; }
            if (dup || n >= 32) continue;
            if (r == from) { cs_getstr(e, "name", en, sizeof(en)); src = en; }
            else src = step[i];
            if (d == goal) {
                for (k = 0; src[k] && k < dirlen - 1; k++) dir[k] = src[k];
                dir[k] = '\0';
                return 1;
            }
            seen[n] = d;
            for (k = 0; src[k] && k < 15; k++) step[n][k] = src[k];
            step[n][k] = '\0';
            n++;
        }
    }
    dir[0] = '\0';
    return 0;
}

/* M51b: print the active contract's briefing under a "Your job:" line -- its
 * objective (a bounty target, or a courier destination resolved from the
 * job_dest room id), a live objective status (M52b: the direction toward a
 * courier destination, or a bounty target's standing), its reward (creds, CP,
 * an optional standing step, and any job_grant clearance), and where to report.
 * Reads the giver the sheet points to and the room graph from `room`. Returns 1
 * when a contract is active and rendered, 0 when none. */
static int
show_briefing(struct cs_out *o, int ch, int room)
{
    int active = cs_geti(ch, "job_giver", 0);
    char nm[32], tgt[32], dst[16];
    int done = cs_geti(ch, "job_done", 0);

    if (active <= 0) return 0;
    cs_getstr(active, "name", nm, sizeof(nm));
    cs_s(o, "Your job: ");
    cs_s(o, nm);
    cs_s(o, done ? " (done; report back)" : " (in progress)");
    cs_s(o, "\n");
    cs_getstr(active, "job_target", tgt, sizeof(tgt));
    cs_getstr(active, "job_dest", dst, sizeof(dst));
    if (tgt[0]) {
        cs_s(o, "  Objective: defeat "); cs_s(o, tgt); cs_s(o, "\n");
        cs_s(o, done ? "  The target is down.\n"
                     : "  The target is still at large.\n");
    } else if (dst[0]) {
        int goal = cs_atoi(dst);
        char rn[32];

        cs_getstr(goal, "name", rn, sizeof(rn));
        cs_s(o, "  Objective: reach ");
        cs_s(o, rn[0] ? rn : dst);
        cs_s(o, "\n");
        if (done) {
            cs_s(o, "  Delivered; report back.\n");
        } else if (goal == room) {
            cs_s(o, "  You are at the destination.\n");
        } else {
            char dir[24];

            if (path_step(room, goal, dir, sizeof(dir)) && dir[0]) {
                cs_s(o, "  Head "); cs_s(o, dir); cs_s(o, " from here.\n");
            } else {
                cs_s(o, "  The destination is not nearby.\n");
            }
        }
    }
    cs_s(o, "  Reward: ");
    cs_i(o, cs_geti(active, "job_creds", 0));
    cs_s(o, " creds, ");
    cs_i(o, cs_geti(active, "job_cp", 0));
    cs_s(o, " CP");
    {
        char st[64], fac[32], grant[32];
        int step;

        cs_getstr(active, "job_standing", st, sizeof(st));
        if (job_fac_step(st, fac, sizeof(fac), &step)) {
            cs_s(o, ", standing with "); cs_s(o, fac);
        }
        cs_getstr(active, "job_grant", grant, sizeof(grant));
        if (grant[0]) { cs_s(o, ", clearance "); cs_s(o, grant); }
    }
    cs_s(o, "\n");
    cs_s(o, "  Report to "); cs_s(o, nm); cs_s(o, ".\n");
    return 1;
}

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int ch = cs_geti(self, "charid", -1);
    char *verb = (char *)vm_args->verb;
    char nm[32];

    o.len = 0;
    if (ch < 0) { puts("You have no character sheet."); _exit(0); }

    if (cs_streq(verb, "accept")) {
        int giver = vm_args->dobj;
        char st[64], fac[32], cd[16];
        int need;

        if (giver <= 0 || !cs_geti(giver, "job", 0)) {
            puts("That is not offering a job.");
            _exit(0);
        }
        if (cs_geti(ch, "job_giver", 0) > 0) {
            puts("You already have a job; abandon it first.");
            _exit(0);
        }
        cd_name(cd, giver);
        if (cs_geti(ch, cd, 0) > sys_now()) {
            puts("That fixer has no fresh work for you yet.");
            _exit(0);
        }
        cs_getstr(giver, "job_min", st, sizeof(st));
        if (job_fac_step(st, fac, sizeof(fac), &need)
                && cs_disposition(giver, ch, fac) < need) {
            cs_s(&o, "You do not rank for that job (needs ");
            cs_s(&o, fac);
            cs_s(&o, " ");
            cs_s(&o, cs_standing_word(need));
            cs_s(&o, ").\n");
            cs_flush(&o);
            _exit(0);
        }
        /* M47a: refuse a chain contract until an earlier one grants its flag */
        {
            char pre[32];

            cs_getstr(giver, "job_need", pre, sizeof(pre));
            if (pre[0] && !cs_has_flag(ch, pre)) {
                cs_s(&o, "You are not cleared for that job yet (requires ");
                cs_s(&o, pre);
                cs_s(&o, ").\n");
                cs_flush(&o);
                _exit(0);
            }
        }
        cs_seti(ch, "job_giver", giver);
        cs_seti(ch, "job_done", 0);
        cs_getstr(giver, "name", nm, sizeof(nm));
        cs_s(&o, "You take the job from ");
        cs_s(&o, nm);
        cs_s(&o, ".\n");
        cs_flush(&o);
        _exit(0);
    }

    if (cs_streq(verb, "turnin") || cs_streq(verb, "report")) {
        int giver = cs_geti(ch, "job_giver", 0);
        int cp, creds;

        if (giver <= 0) { puts("You have no job to report."); _exit(0); }
        if (sys_getobj(giver, "location") != room) {
            puts("You must report to the one who gave the job.");
            _exit(0);
        }
        if (!cs_geti(ch, "job_done", 0)) {
            puts("The job is not done yet.");
            _exit(0);
        }
        cp = cs_geti(giver, "job_cp", 0);
        creds = cs_geti(giver, "job_creds", 0);
        cs_seti(ch, "cp", cs_geti(ch, "cp", 0) + cp);
        cs_seti(ch, "money", cs_geti(ch, "money", 0) + creds);
        cs_getstr(giver, "name", nm, sizeof(nm));
        cs_s(&o, nm);
        cs_s(&o, " pays you ");
        cs_i(&o, creds);
        cs_s(&o, " creds and ");
        cs_i(&o, cp);
        cs_s(&o, " CP.");
        /* an optional standing reward, "faction:step" on the giver (M37c) */
        {
            char st[64], fac[32];
            int step;

            cs_getstr(giver, "job_standing", st, sizeof(st));
            if (job_fac_step(st, fac, sizeof(fac), &step)) {
                cs_standing_add(ch, fac, step);
                cs_s(&o, " Your standing with ");
                cs_s(&o, fac);
                cs_s(&o, " shifts.");
            }
        }
        /* an optional campaign-flag grant, the chain token this contract hands
         * on (M47b); a later contract's job_need gates on it */
        {
            char grant[32];

            cs_getstr(giver, "job_grant", grant, sizeof(grant));
            if (grant[0]) {
                cs_flag_add(ch, grant);
                cs_s(&o, " You are now cleared as ");
                cs_s(&o, grant);
                cs_s(&o, ".");
            }
        }
        cs_s(&o, "\n");
        /* arm this giver's cooldown, so the same contract cannot be farmed back
         * to back (M39c); zero or absent job_cd leaves it repeatable at once. */
        {
            int cdsecs = cs_geti(giver, "job_cd", 0);
            char cd[16];

            if (cdsecs > 0) {
                cd_name(cd, giver);
                cs_seti(ch, cd, sys_now() + cdsecs);
            }
        }
        cs_seti(ch, "job_giver", 0);
        cs_seti(ch, "job_done", 0);
        cs_flush(&o);
        _exit(0);
    }

    if (cs_streq(verb, "abandon")) {
        if (cs_geti(ch, "job_giver", 0) <= 0) {
            puts("You have no job to abandon.");
            _exit(0);
        }
        cs_seti(ch, "job_giver", 0);
        cs_seti(ch, "job_done", 0);
        puts("You abandon the job.");
        _exit(0);
    }

    /* M52a: board -- every open contract in the district, not just this room. Walk
     * the room graph breadth-first from here, following each room's exit `dest`
     * props and visiting each room once up to a fixed cap, and list the givers each
     * room holds through show_offer under a room header. Flush per room so a large
     * district does not overrun the output buffer. Reads only world state. */
    if (cs_streq(verb, "board")) {
        int seen[32], n = 0, any = 0, i;

        seen[n++] = room;
        cs_s(&o, "Contracts nearby:\n");
        for (i = 0; i < n; i++) {
            int r = seen[i], e = 0, hdr = 0;

            while ((e = sys_next(r, e)) != 0) {
                char dst[16];

                if (cs_geti(e, "job", 0)) {
                    if (!hdr) {
                        char rn[32];

                        cs_getstr(r, "name", rn, sizeof(rn));
                        cs_s(&o, rn); cs_s(&o, ":\n");
                        hdr = 1;
                    }
                    show_offer(&o, e, ch);
                    any = 1;
                }
                cs_getstr(e, "dest", dst, sizeof(dst));
                if (dst[0]) {
                    int d = cs_atoi(dst), j, dup = 0;

                    for (j = 0; j < n; j++)
                        if (seen[j] == d) { dup = 1; break; }
                    if (!dup && d > 0 && n < 32) seen[n++] = d;
                }
            }
            cs_flush(&o);
        }
        if (!any) cs_s(&o, "  none\n");
        cs_flush(&o);
        _exit(0);
    }

    /* M51c: contracts -- a consolidated "what am I working on" view a player can
     * call from anywhere: the active briefing, the earned clearances, and a
     * one-line standing summary, reading only the caller's own sheet. */
    if (cs_streq(verb, "contracts")) {
        if (!show_briefing(&o, ch, room))
            cs_s(&o, "No active contract.\n");
        cs_clearances_line(ch, &o);
        cs_decay(ch);           /* fade idle standing first, as the sheet does */
        cs_standing_line(ch, &o);
        cs_flush(&o);
        _exit(0);
    }

    /* jobs : what is offered here, and your active contract */
    {
        int e = 0, any = 0;

        cs_s(&o, "Jobs offered here:\n");
        while ((e = sys_next(room, e)) != 0) {
            if (cs_geti(e, "job", 0)) { show_offer(&o, e, ch); any = 1; }
        }
        if (!any)
            cs_s(&o, "  none\n");
        show_briefing(&o, ch, room);
        cs_flush(&o);
        _exit(0);
    }
}
