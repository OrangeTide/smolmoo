/* verb_jobs.c : accept and track a job contract (M37a). A job giver is a builder
 * object marked `job`=1 that carries a contract in props: a `job_desc` line, a
 * goal (a `job_target` creature for a bounty, or a `job_dest` room for a courier,
 * M37c), and the reward (`job_cp`, `job_creds`, and an optional `job_standing`
 * "faction:step"). An optional `job_min` "faction:step" gates the contract behind
 * a minimum standing, read through the giver's disposition so its `disp` offset
 * can vouch for you (M38a, M38c). An optional `job_cd` cooldown in seconds keeps
 * a contract from being farmed back to back (M39c). A player holds one
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

    /* jobs : what is offered here, and your active contract */
    {
        int e = 0, any = 0, active;

        cs_s(&o, "Jobs offered here:\n");
        while ((e = sys_next(room, e)) != 0) {
            if (cs_geti(e, "job", 0)) { show_offer(&o, e, ch); any = 1; }
        }
        if (!any)
            cs_s(&o, "  none\n");
        active = cs_geti(ch, "job_giver", 0);
        if (active > 0) {
            cs_getstr(active, "name", nm, sizeof(nm));
            cs_s(&o, "Your job: ");
            cs_s(&o, nm);
            cs_s(&o, cs_geti(ch, "job_done", 0)
                     ? " (done; report back)" : " (in progress)");
            cs_s(&o, "\n");
        }
        cs_flush(&o);
        _exit(0);
    }
}
