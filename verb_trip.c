/* verb_trip.c : trip an engaged foe Prone (M40a). A melee maneuver inside an
 * active fight: on your turn, an opposed roll (your Might plus Brawl against the
 * foe's better of Might or Agility) and, on a win, the foe is knocked Prone
 * (rules Section 8). It ends your turn. It writes the room and the foe's sheet,
 * so it is setuid and elevates through grant_accept. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* is `foe` on the room's combat roster? */
static int
on_roster(int room, int foe)
{
    int ids[8], k = cs_roster_foes(room, 0, ids, 8), i;

    for (i = 0; i < k; i++)
        if (ids[i] == foe) return 1;
    return 0;
}

/* the higher of Might or Agility, in points */
static int
best_phys(int sheet)
{
    int m = cs_geti(sheet, "mig", 0), a = cs_geti(sheet, "agi", 0);

    return m > a ? m : a;
}

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int atk = cs_sheet(self);
    char *foename = (char *)vm_args->dobjstr;
    int foe, defsh, pool, aroll, droll, awild, dwild;
    char an[32], dn[32];

    grant_accept();

    while (foename && *foename == ' ') foename++;
    if (!foename || !*foename) { puts("Trip whom?"); _exit(0); }
    if (cs_wrong_mode(room, 0)) _exit(0);
    if (cs_geti(room, "cb_active", 0) != 1) {
        puts("You are not in a fight.");
        _exit(0);
    }
    if (cs_geti(room, "cb_turn", -1) != self) {
        puts("It is not your turn.");
        _exit(0);
    }
    if (cs_geti(room, "cb_acted", 0) == 1) {
        puts("You have already acted this turn.");
        _exit(0);
    }
    foe = sys_objfind(foename);
    if (foe <= 0) { puts("You don't see that here."); _exit(0); }
    defsh = cs_sheet(foe);
    if (cs_geti(defsh, "downed", 0)) { puts("It is already down."); _exit(0); }
    if (!on_roster(room, foe) || cs_geti(defsh, "band", 0) != 0) {
        puts("You must be in melee to trip them.");
        _exit(0);
    }
    if (cs_geti(defsh, "prone", 0)) { puts("They are already prone."); _exit(0); }

    pool = cs_geti(atk, "mig", 0) + cs_geti(atk, "sk_brawl", 0)
           - cs_crash_penalty(atk);
    if (pool < 1) pool = 1;
    aroll = cs_roll(pool, &awild);
    droll = cs_roll(best_phys(defsh), &dwild);

    cs_getstr(atk, "name", an, sizeof(an));
    cs_getstr(defsh, "name", dn, sizeof(dn));
    o.len = 0;
    cs_s(&o, an);
    if (aroll > droll) {
        cs_seti(defsh, "prone", 1);
        cs_s(&o, " trips ");
        cs_s(&o, dn);
        cs_s(&o, "; it sprawls prone.");
    } else {
        cs_s(&o, " grapples at ");
        cs_s(&o, dn);
        cs_s(&o, ", but it keeps its feet.");
    }
    sys_broadcast(room, cs_cstr(&o));
    cs_end_turn(room);
    _exit(0);
}
