/* verb_chargen.c : interactive ChromeSix character creation */

#include "chromesix_verb.h"

static const char *ATTRS[4] = { "agi", "mig", "wit", "cha" };

/* Parse the next non-negative integer from *p, advancing past it and any
 * leading spaces. *ok is 1 if at least one digit was read, else 0. */
static int
next_int(char **p, int *ok)
{
    char *s = *p;
    int v = 0, got = 0;

    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') {
        v = (v << 3) + (v << 1) + (*s - '0');
        s++;
        got = 1;
    }
    *p = s;
    *ok = got;
    return v;
}

static void
prompt(int step)
{
    if (step == 1) {
        puts("Step 1/3 attributes: chargen <agi> <mig> <wit> <cha>");
        puts("  each 3 to 9, and the four must total 24");
    } else if (step == 2) {
        puts("Step 2/3 hook: chargen <street|cyber|awakened>");
    } else {
        puts("Step 3/3 skills: chargen <name:pts> <name:pts> ...");
        puts("  points total 12, each at most its attribute + 6");
    }
}

int
main(void)
{
    struct cs_out o;
    struct cs_derived d;
    int self = vm_args->player;
    int ch = cs_geti(self, "charid", -1);
    char *arg = (char *)vm_args->dobjstr;
    char buf[12];
    int step, i;

    o.len = 0;
    if (ch < 0) { puts("You have no character sheet."); _exit(0); }

    step = cs_geti(ch, "cg_step", 0);
    if (step < 1 || step > 3) {
        step = 1;
        sys_setprop(ch, "cg_step", "1");
    }

    while (arg && *arg == ' ') arg++;
    if (!arg || !*arg) {
        prompt(step);
        _exit(0);
    }

    if (step == 1) {
        int v[4], ok, sum = 0;
        char *p = arg;

        for (i = 0; i < 4; i++) {
            v[i] = next_int(&p, &ok);
            if (!ok) { puts("Enter four numbers: agi mig wit cha."); _exit(0); }
        }
        for (i = 0; i < 4; i++) {
            if (v[i] < 3 || v[i] > 9) {
                puts("Each attribute must be 3 to 9.");
                _exit(0);
            }
            sum += v[i];
        }
        if (sum != 24) {
            cs_s(&o, "Attributes must total 24 (you have ");
            cs_i(&o, sum); cs_s(&o, ").\n");
            cs_flush(&o);
            _exit(0);
        }
        for (i = 0; i < 4; i++) {
            cs_itoa(buf, v[i]);
            sys_setprop(ch, ATTRS[i], buf);
        }
        sys_setprop(ch, "cg_step", "2");
        puts("Attributes set.");
        prompt(2);
        _exit(0);
    }

    if (step == 2) {
        char hk[16];
        int n = 0;

        while (arg[n] && arg[n] != ' ' && n < 15) { hk[n] = arg[n]; n++; }
        hk[n] = '\0';
        if (!cs_streq(hk, "street") && !cs_streq(hk, "cyber") &&
            !cs_streq(hk, "awakened")) {
            puts("Hook must be street, cyber, or awakened.");
            _exit(0);
        }
        sys_setprop(ch, "hook", hk);
        sys_setprop(ch, "cg_step", "3");
        puts("Hook set.");
        prompt(3);
        _exit(0);
    }

    /* step 3: skills, then finalize */
    {
        int pts[20], ok, sum = 0, spidx;
        char hk[16];
        char *p = arg;
        int n;

        for (i = 0; i < 20; i++) pts[i] = 0;

        while (*p) {
            char name[32];
            int ni = 0, idx, val;

            while (*p == ' ') p++;
            if (!*p) break;
            while (*p && *p != ' ' && *p != ':' && ni < 31) name[ni++] = *p++;
            name[ni] = '\0';
            if (*p != ':') {
                puts("Use name:points pairs, e.g. firearms:3 hacking:3.");
                _exit(0);
            }
            p++;
            val = next_int(&p, &ok);
            if (!ok) { puts("Missing points after ':'."); _exit(0); }
            idx = cs_skill_idx(name);
            if (idx < 0) {
                cs_s(&o, "Unknown skill: "); cs_s(&o, name); cs_s(&o, "\n");
                cs_flush(&o);
                _exit(0);
            }
            pts[idx] = val;
        }

        for (i = 0; i < 20; i++) {
            int cap;
            if (pts[i] == 0) continue;
            cap = cs_geti(ch, cs_skills[i].attr, 0) + 6;
            if (pts[i] > cap) {
                cs_s(&o, "Skill too high: "); cs_s(&o, cs_skills[i].name);
                cs_s(&o, " max "); cs_i(&o, cap); cs_s(&o, ".\n");
                cs_flush(&o);
                _exit(0);
            }
            sum += pts[i];
        }
        if (sum != 12) {
            cs_s(&o, "Skills must total 12 (you have ");
            cs_i(&o, sum); cs_s(&o, ").\n");
            cs_flush(&o);
            _exit(0);
        }
        spidx = cs_skill_idx("spellcasting");
        n = sys_getprop(ch, "hook", hk, sizeof(hk) - 1);
        if (n < 0) n = 0;
        hk[n] = '\0';
        if (spidx >= 0 && pts[spidx] > 0 && !cs_streq(hk, "awakened")) {
            puts("Spellcasting points need the Awakened hook.");
            _exit(0);
        }

        /* commit skills, funds, and current pools */
        for (i = 0; i < 20; i++) {
            char pn[40];
            cs_skname(pn, cs_skills[i].name);
            cs_itoa(buf, pts[i]);
            sys_setprop(ch, pn, buf);
        }
        sys_setprop(ch, "money", "300");
        sys_setprop(ch, "stims", "3");
        sys_setprop(ch, "maneuvers", "smartlink");
        cs_recalc(ch, &d);
        cs_itoa(buf, d.maxbp); sys_setprop(ch, "bp", buf);
        cs_itoa(buf, d.maxgrit); sys_setprop(ch, "grit", buf);
        sys_setprop(ch, "cg_step", "0");

        puts("Character complete. 300 creds, 3 stims; BP and Grit at maximum.");
        puts("Type 'sheet' to view it. Gear and CP unlocks arrive with combat.");
        _exit(0);
    }
}
