/* verb_store.c : generic buy/sell/list plus the ChromeSix stim shop. A store is
 * any object that holds priced item objects in its contents: a vendor NPC, a
 * vending machine, a crate. `list [from <store>]` shows the stock; `buy <item>
 * [from <store>]` sells one instance, moving the object to the buyer and
 * drawing its price off their creds. With no `from`, the store is the room's
 * `vendor` NPC. Standing with the store's faction, plus the store's own `disp`
 * offset, shifts the price ten percent per step and gates the sale at
 * Hostile or worse (rules Section 14). A store
 * that sets a `stim` base price sells the counter-based stim dose, the Grit
 * money sink (rules Section 8), through the fallback path when no priced item
 * matches the name. Selling an object instance decrements stock; a reset rule
 * with the store as its room restocks it.
 *
 * `sell <item> [to <store>]` is the inverse: a priced item the player carries
 * moves into the store and credits the player. A store buys back only if it
 * sets a `buyback` percent (of the item's base price); the standing shift
 * favors the seller, so a higher standing pays more, and Hostile still refuses.
 * A store that sets both `stim` and `buyback` buys stim doses back off the
 * sheet counter. Equipped gear must be removed first. */
/* SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#include "chromesix_verb.h"

/* case-insensitive full-name equality */
static int
ci_eq(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = *a, cb = *b;

        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* first priced (price > 0) item in the store's contents named `name`, or 0 */
static int
priced_named(int store, const char *name)
{
    int it = 0;
    char nm[32];

    while ((it = sys_next(store, it)) != 0) {
        if (cs_geti(it, "price", 0) <= 0) continue;
        cs_getstr(it, "name", nm, sizeof(nm));
        if (ci_eq(nm, name)) return it;
    }
    return 0;
}

/* first object in the room's contents named `name`, or 0 (a vending machine) */
static int
room_obj_named(int room, const char *name)
{
    int it = 0;
    char nm[32];

    while ((it = sys_next(room, it)) != 0) {
        cs_getstr(it, "name", nm, sizeof(nm));
        if (ci_eq(nm, name)) return it;
    }
    return 0;
}

/* first priced item the player carries named `name`, or 0. Reuses priced_named,
 * since carried items are the player object's direct contents. */
static int
carried_priced_named(int who, const char *name)
{
    return priced_named(who, name);
}

/* apply faction standing to a base price: each step shifts it ten percent */
static int
priced_at(int base, int step)
{
    int r;

    return cs_divmod(base * (10 - step), 10, &r);
}

int
main(void)
{
    struct cs_out o;
    int self = vm_args->player;
    int room = vm_args->room;
    int sh = cs_sheet(self);
    char *verb = (char *)vm_args->verb;
    char *arg = (char *)vm_args->dobjstr;
    char item[48], sname[32], faction[32];
    int store, step, money, price, good, i, j, len, fpos, iend, seplen;

    o.len = 0;

    /* split the argument into "<item> <sep> <store>": find a standalone "from"
       (buy, list) or "to" (sell), whichever comes first. */
    while (arg && *arg == ' ') arg++;
    len = 0;
    while (arg && arg[len]) len++;
    fpos = -1;
    seplen = 0;
    for (i = 0; i < len; i++) {
        if (i != 0 && arg[i - 1] != ' ')
            continue;
        if (i + 4 <= len && arg[i] == 'f' && arg[i + 1] == 'r' &&
            arg[i + 2] == 'o' && arg[i + 3] == 'm' &&
            (i + 4 == len || arg[i + 4] == ' ')) {
            fpos = i; seplen = 4; break;
        }
        if (i + 2 <= len && arg[i] == 't' && arg[i + 1] == 'o' &&
            (i + 2 == len || arg[i + 2] == ' ')) {
            fpos = i; seplen = 2; break;
        }
    }
    iend = (fpos >= 0) ? fpos : len;
    while (iend > 0 && arg[iend - 1] == ' ') iend--;
    for (i = 0, j = 0; i < iend && j < (int)sizeof(item) - 1; i++)
        item[j++] = arg[i];
    item[j] = '\0';
    sname[0] = '\0';
    if (fpos >= 0) {
        i = fpos + seplen;
        while (arg[i] == ' ') i++;
        for (j = 0; arg[i] && j < (int)sizeof(sname) - 1; i++)
            sname[j++] = arg[i];
        sname[j] = '\0';
    }

    /* resolve the store: a named object in the room, else the room's vendor */
    if (sname[0]) store = room_obj_named(room, sname);
    else store = cs_geti(room, "vendor", 0);

    if (cs_streq(verb, "list")) {
        int it = 0, any = 0, stimbase;
        char nm[32];

        if (store <= 0) {
            puts(sname[0] ? "There is no such store here."
                          : "There is nothing to buy here.");
            _exit(0);
        }
        cs_getstr(store, "faction", faction, sizeof(faction));
        step = faction[0] ? cs_disposition(store, sh, faction) : 0;
        cs_getstr(store, "name", nm, sizeof(nm));
        cs_s(&o, nm);
        cs_s(&o, " offers:\n");
        cs_flush(&o);
        /* one line per priced item; flush each so a large store never overflows
           the line buffer */
        while ((it = sys_next(store, it)) != 0) {
            int base = cs_geti(it, "price", 0);

            if (base <= 0) continue;
            cs_getstr(it, "name", nm, sizeof(nm));
            cs_s(&o, "  ");
            cs_s(&o, nm);
            cs_s(&o, " -- ");
            cs_i(&o, priced_at(base, step));
            cs_s(&o, " creds\n");
            cs_flush(&o);
            any = 1;
        }
        /* the counter-based stim dose, if this store sells it */
        stimbase = cs_geti(store, "stim", 0);
        if (stimbase > 0) {
            cs_s(&o, "  stim -- ");
            cs_i(&o, priced_at(stimbase, step));
            cs_s(&o, " creds\n");
            cs_flush(&o);
            any = 1;
        }
        if (!any) puts("  (nothing in stock)");
        _exit(0);
    }

    if (cs_streq(verb, "sell")) {
        int buyback, base;
        char nm[32], slot[16];

        if (!item[0]) { puts("Sell what?"); _exit(0); }
        if (store <= 0) {
            puts(sname[0] ? "There is no such store here."
                          : "There is no one here to sell to.");
            _exit(0);
        }
        cs_getstr(store, "faction", faction, sizeof(faction));
        step = faction[0] ? cs_disposition(store, sh, faction) : 0;
        if (step <= -2) {
            cs_s(&o, "The vendor refuses to deal with you (");
            cs_s(&o, cs_standing_word(step));
            cs_s(&o, ").\n");
            cs_flush(&o);
            _exit(0);
        }
        buyback = cs_geti(store, "buyback", 0);
        if (buyback <= 0) { puts("The vendor is not buying."); _exit(0); }

        /* a priced item the player carries: moves into the store as stock */
        good = carried_priced_named(self, item);
        if (good > 0) {
            int r;

            if (sys_getprop(good, "slot", slot, sizeof(slot) - 1) > 0) {
                puts("You must remove it first.");
                _exit(0);
            }
            cs_getstr(good, "name", nm, sizeof(nm));
            base = cs_divmod(cs_geti(good, "price", 0) * buyback, 100, &r);
            price = priced_at(base, -step);   /* standing favors the seller */
            money = cs_geti(sh, "money", 0);
            /* the item becomes store-owned stock, so elevate for the move;
               credit the player's own sheet at caller authority. */
            grant_accept();
            j = sys_move(good, store);
            grant_release();
            if (j < 0) { puts("The vendor won't take it."); _exit(0); }
            cs_seti(sh, "money", money + price);
            cs_s(&o, "You sell the ");
            cs_s(&o, nm);
            cs_s(&o, " for ");
            cs_i(&o, price);
            cs_s(&o, " creds. Balance ");
            cs_i(&o, money + price);
            cs_s(&o, ".\n");
            cs_flush(&o);
            _exit(0);
        }

        /* fallback: a stim dose off the sheet counter, if the store buys stims */
        if ((cs_streq(item, "stim") || cs_streq(item, "stims")) &&
            cs_geti(store, "stim", 0) > 0) {
            int r, stims = cs_geti(sh, "stims", 0);

            if (stims <= 0) { puts("You have no stims to sell."); _exit(0); }
            base = cs_divmod(cs_geti(store, "stim", 0) * buyback, 100, &r);
            price = priced_at(base, -step);
            money = cs_geti(sh, "money", 0);
            cs_seti(sh, "stims", stims - 1);
            cs_seti(sh, "money", money + price);
            cs_s(&o, "You sell a stim for ");
            cs_i(&o, price);
            cs_s(&o, " creds. Balance ");
            cs_i(&o, money + price);
            cs_s(&o, ", stims ");
            cs_i(&o, stims - 1);
            cs_s(&o, ".\n");
            cs_flush(&o);
            _exit(0);
        }

        puts("You have no such thing to sell.");
        _exit(0);
    }

    /* buy */
    if (!item[0]) { puts("Buy what?"); _exit(0); }
    if (store <= 0) {
        puts(sname[0] ? "There is no such store here."
                      : "There is no one here to buy from.");
        _exit(0);
    }

    cs_getstr(store, "faction", faction, sizeof(faction));
    step = faction[0] ? cs_disposition(store, sh, faction) : 0;
    if (step <= -2) {
        cs_s(&o, "The vendor refuses to deal with you (");
        cs_s(&o, cs_standing_word(step));
        cs_s(&o, ").\n");
        cs_flush(&o);
        _exit(0);
    }

    /* generic path: a priced item object in the store, sold as one instance */
    good = priced_named(store, item);
    if (good > 0) {
        char nm[32];

        cs_getstr(good, "name", nm, sizeof(nm));
        price = priced_at(cs_geti(good, "price", 0), step);
        money = cs_geti(sh, "money", 0);
        if (money < price) {
            cs_s(&o, "You can't afford the ");
            cs_s(&o, nm);
            cs_s(&o, " (");
            cs_i(&o, price);
            cs_s(&o, " creds). You have ");
            cs_i(&o, money);
            cs_s(&o, ".\n");
            cs_flush(&o);
            _exit(0);
        }
        /* move the goods first; charge only once the item is in hand. The
           stock is store-owned, so elevate just for the transfer (buy is
           setuid); the buyer's own sheet is written at caller authority. */
        grant_accept();
        j = sys_move(good, self);
        grant_release();
        if (j < 0) {
            puts("You can't carry that away.");
            _exit(0);
        }
        cs_seti(sh, "money", money - price);
        cs_s(&o, "You buy the ");
        cs_s(&o, nm);
        cs_s(&o, " for ");
        cs_i(&o, price);
        cs_s(&o, " creds. Balance ");
        cs_i(&o, money - price);
        cs_s(&o, ".\n");
        cs_flush(&o);
        _exit(0);
    }

    /* fallback: the counter-based stim dose, for a store that sells it */
    if ((cs_streq(item, "stim") || cs_streq(item, "stims")) &&
        cs_geti(store, "stim", 0) > 0) {
        /* +3 Allied sells at 70 percent, -1 Watched at 110 percent */
        price = priced_at(cs_geti(store, "stim", 0), step);
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

    puts("There is no such thing for sale here.");
    _exit(0);
}
