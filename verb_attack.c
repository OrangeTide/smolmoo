/* verb_attack.c : ChromeSix attack. Starts a fight, draws a new foe into
 * an active one, or resolves a turn against a foe already in the fight. */

#include "chromesix_verb.h"

/* object id of the __combat task verb (verbs.conf) */
#define COMBAT_VERB 420

/* Is `foe` already a combatant on the room's roster? */
static int
on_roster(int room, int foe)
{
    int ids[8], k = cs_roster_foes(room, 0, ids, 8), i;

    for (i = 0; i < k; i++)
        if (ids[i] == foe) return 1;
    return 0;
}

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int atk = cs_sheet(self);
    char *foename = (char *)vm_args->dobjstr;
    int foe, defsh, weapon, down, band, pool_mod, pd_bonus;
    int push = 0, push_bonus = 0, aimed = 0;

    grant_accept();   /* writes room combat state and the foe's sheet */

    o.len = 0;
    while (foename && *foename == ' ') foename++;
    if (foename) push = cs_parse_push(foename);   /* strips trailing "push N" */
    if (!foename || !*foename) { puts("Attack whom?"); _exit(0); }
    if (cs_wrong_mode(room, 0)) _exit(0);
    foe = sys_objfind(foename);
    if (foe <= 0) { puts("You don't see that here."); _exit(0); }
    defsh = cs_sheet(foe);
    if (cs_geti(defsh, "downed", 0)) { puts("It is already down."); _exit(0); }

    if (cs_geti(room, "cb_active", 0) != 1) {
        char roster[24], num[12];
        int i = 0, j;

        cs_itoa(num, self);
        for (j = 0; num[j]; j++) roster[i++] = num[j];
        roster[i++] = ',';
        cs_itoa(num, foe);
        for (j = 0; num[j]; j++) roster[i++] = num[j];
        roster[i] = '\0';

        sys_setprop(atk, "cover", "0");
        sys_setprop(atk, "cover_bonus", "0");
        sys_setprop(atk, "moved", "0");
        sys_setprop(atk, "move_bank", "0");
        sys_setprop(atk, "stance", "");
        sys_setprop(defsh, "band", "0");        /* the foe you close on */
        sys_setprop(room, "cb_mode", "fight");
        sys_setprop(room, "cb_roster", roster);
        cs_seti(room, "cb_round", 0);
        cs_seti(room, "cb_turn", 0);
        cs_seti(room, "cb_acted", 0);
        cs_seti(room, "cb_active", 1);

        char h[72];
        if (cs_elf_hash(COMBAT_VERB, h, sizeof(h)) == 0)
            sys_spawn(h, 0, "");
        cs_s(&o, "You move on the ");
        cs_s(&o, foename);
        cs_s(&o, " -- combat begins!\n");
        cs_flush(&o);
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

    /* draw a fresh foe into the fight; frontage decides whether it can be
     * pressed in melee at once or has to hang back at Short. Set its band
     * from the melee count BEFORE adding it to the roster, so its own
     * default band does not inflate that count. */
    if (!on_roster(room, foe)) {
        char rbuf[64], num[12];
        int rn = sys_getprop(room, "cb_roster", rbuf, sizeof(rbuf) - 1), j;

        cs_seti(defsh, "band",
                cs_engaged_count(room, self) < cs_frontage(atk) ? 0 : 1);
        if (rn < 0) rn = 0;
        rbuf[rn] = '\0';
        cs_itoa(num, foe);
        if (rn > 0 && rn < (int)sizeof(rbuf) - 14) {
            rbuf[rn++] = ',';
            for (j = 0; num[j] && rn < (int)sizeof(rbuf) - 1; j++)
                rbuf[rn++] = num[j];
            rbuf[rn] = '\0';
            sys_setprop(room, "cb_roster", rbuf);
        }
    }

    weapon = cs_geti(atk, "wielded", 305);
    band = cs_geti(defsh, "band", 0);
    if (!cs_range_check(weapon, band, &pool_mod)) {
        puts("Your target is out of reach. Close the distance first.");
        _exit(0);
    }
    if (cs_weapon_empty(weapon)) {
        char wn[32];

        cs_getstr(weapon, "name", wn, sizeof(wn));
        o.len = 0;
        cs_s(&o, "*click* -- the ");
        cs_s(&o, wn);
        cs_s(&o, " is empty. Reload.\n");
        cs_flush(&o);
        _exit(0);                       /* an empty click does not spend the turn */
    }
    if (push > 0) {
        push_bonus = cs_grit_push(atk, push);
        if (push_bonus < 0) {
            puts("You don't have the Grit to push that hard.");
            _exit(0);
        }
    }
    pd_bonus = (band > 0 && cs_geti(defsh, "moved", 0)) ? 2 : 0;
    /* Smartlink aim (set by `use smartlink`): this one attack ignores the
     * target's cover and drops its Passive Defense by 2, then the aim clears. */
    if (cs_geti(atk, "aim", 0)) {
        pd_bonus += -2 - cs_cover(defsh);
        cs_seti(atk, "aim", 0);
        aimed = 1;
    }
    cs_attack_resolve(atk, defsh, weapon,
                      pool_mod + push_bonus - cs_crash_penalty(atk),
                      pd_bonus, 1, &o, &down, 0);
    if (aimed) cs_s(&o, " [aimed]");
    if (push > 0) {
        cs_s(&o, " [pushed +");
        cs_i(&o, push);
        cs_s(&o, "D]");
    }
    if (cs_uses_ammo(weapon) && cs_fire(weapon) == 0)
        cs_s(&o, " -- empty!");
    cs_s(&o, "\n");
    sys_broadcast(room, cs_cstr(&o));
    cs_end_turn(room);
    _exit(0);
}
