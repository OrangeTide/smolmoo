/* verb_jobs.c : accept and track a job contract (M37a). A job giver is a builder
 * object marked `job`=1 that carries a contract in props: a `job_desc` line, a
 * target for the goal, and the reward (`job_cp`, `job_creds`). A player holds one
 * job at a time, stored on the sheet as `job_giver` (the giver's id, 0 for none)
 * and `job_done` (the completion flag, set in M37b). `jobs` lists what is offered
 * here and your active contract; `accept` takes one; `abandon` drops it; `turnin`
 * (at the giver, once `job_done` is set) pays the reward into the sheet's `cp` and
 * `money` (M37b). The player owns the sheet, so this runs at caller authority,
 * not setuid. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* print one giver's offer: its name, description, and reward */
static void
show_offer(struct cs_out *o, int giver)
{
    char nm[32], desc[128];

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
    cs_s(o, " creds]\n");
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

        if (giver <= 0 || !cs_geti(giver, "job", 0)) {
            puts("That is not offering a job.");
            _exit(0);
        }
        if (cs_geti(ch, "job_giver", 0) > 0) {
            puts("You already have a job; abandon it first.");
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
        cs_seti(ch, "job_giver", 0);
        cs_seti(ch, "job_done", 0);
        cs_getstr(giver, "name", nm, sizeof(nm));
        cs_s(&o, nm);
        cs_s(&o, " pays you ");
        cs_i(&o, creds);
        cs_s(&o, " creds and ");
        cs_i(&o, cp);
        cs_s(&o, " CP.\n");
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
            if (cs_geti(e, "job", 0)) { show_offer(&o, e); any = 1; }
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
