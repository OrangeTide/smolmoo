/* verb_buy.c : ChromeSix purchase, the Grit money sink. A stim has a base
 * price, adjusted by the buyer's standing with the room vendor's faction
 * (rules Section 14): each step shifts the price 10 percent, and a vendor whose
 * faction holds the buyer Hostile (-2) or worse will not deal at all. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

#define STIM_BASE 75

void
_start(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int sh = cs_sheet(self);
    char *arg = (char *)vm_args->dobjstr;
    char word[16], faction[32];
    int i = 0, money, vendor, step, price;

    o.len = 0;
    while (arg && *arg == ' ') arg++;
    if (!arg || !*arg) { puts("Buy what?"); _exit(0); }
    while (arg[i] && arg[i] != ' ' && i < 15) { word[i] = arg[i]; i++; }
    word[i] = '\0';

    vendor = cs_geti(room, "vendor", 0);
    if (vendor <= 0) { puts("There is no one here to buy from."); _exit(0); }

    if (!cs_streq(word, "stim") && !cs_streq(word, "stims")) {
        puts("There is no such thing for sale here.");
        _exit(0);
    }

    /* standing with the vendor's faction sets the price and gates the sale */
    cs_getstr(vendor, "faction", faction, sizeof(faction));
    step = faction[0] ? cs_standing(sh, faction) : 0;
    if (step <= -2) {
        cs_s(&o, "The vendor refuses to deal with you (");
        cs_s(&o, cs_standing_word(step));
        cs_s(&o, ").\n");
        cs_flush(&o);
        _exit(0);
    }
    /* +3 Allied sells at 70 percent, -1 Watched at 110 percent */
    price = cs_divmod(STIM_BASE * (10 - step), 10, &i);

    money = cs_geti(sh, "money", 0);
    if (money < price) {
        cs_s(&o, "You can't afford a stim (");
        cs_i(&o, price);
        cs_s(&o, " creds). You have ");
        cs_i(&o, money);
        cs_s(&o, ".\n");
        cs_flush(&o);
        _exit(0);
    }

    cs_seti(sh, "money", money - price);
    cs_seti(sh, "stims", cs_geti(sh, "stims", 0) + 1);
    cs_s(&o, "You buy a stim for ");
    cs_i(&o, price);
    cs_s(&o, " creds. Balance ");
    cs_i(&o, money - price);
    cs_s(&o, ", stims ");
    cs_i(&o, cs_geti(sh, "stims", 0));
    cs_s(&o, ".\n");
    cs_flush(&o);
    _exit(0);
}
