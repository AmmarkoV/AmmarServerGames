#include "rules.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cards.h"
#include "json.h"

struct EventSink {
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    int count;
};

static void set_error(char *err, size_t err_size, const char *fmt, ...)
{
    va_list ap;

    if (err == NULL || err_size == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(err, err_size, fmt, ap);
    va_end(ap);
}

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    size_t length;

    if (dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    length = strlen(src);
    if (length >= dst_size) {
        length = dst_size - 1;
    }
    memcpy(dst, src, length);
    dst[length] = '\0';
}

static const char *seat_name(const struct Game *g, int seat)
{
    if (g == NULL || seat < 0 || seat >= g->player_count) {
        return "nobody";
    }
    return g->seats[seat].username;
}

static const char *territory_name(const struct Game *g, int territory)
{
    if (g == NULL || g->map == NULL || territory < 0 ||
        territory >= g->territory_count) {
        return "?";
    }
    return g->map->territories[territory].name;
}

static const char *card_name(const struct Game *g, int card)
{
    int territory;

    if (g == NULL || g->map == NULL || card < 0 ||
        card >= g->map->territoryCardCount) {
        return "?";
    }
    if (g->map->territoryCards[card].wild) {
        return "a wild card";
    }
    territory = g->map->territoryCards[card].territory;
    return territory_name(g, territory);
}

static struct Event *sink_new(struct EventSink *sink, int kind, int player)
{
    struct Event *event;

    if (sink == NULL || sink->count >= GAME_MAX_EVENTS_PER_ACTION) {
        return NULL;
    }
    event = &sink->events[sink->count++];
    memset(event, 0, sizeof(*event));
    event->kind = kind;
    event->player = player;
    return event;
}

static void event_text(struct Event *event, const char *fmt, ...)
{
    va_list ap;

    if (event == NULL) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(event->text, sizeof(event->text), fmt, ap);
    va_end(ap);
}

static void log_append(struct Game *g, const struct Event *event)
{
    int index;

    if (g == NULL || event == NULL) {
        return;
    }
    index = g->log.next;
    g->log.events[index] = *event;
    g->log.events[index].version = g->version;
    g->log.next = (index + 1) % GAME_LOG_RING;
    if (g->log.count < GAME_LOG_RING) {
        g->log.count++;
    }
}

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static int terrain_valid(const struct Game *g, int territory)
{
    return g != NULL && territory >= 0 &&
           territory < g->territory_count;
}

static int card_valid(const struct Game *g, int card)
{
    return g != NULL && g->map != NULL && card >= 0 &&
           card < g->map->territoryCardCount;
}

static int count_alive(const struct Game *g)
{
    int i, count = 0;

    for (i = 0; i < g->player_count; i++) {
        if (g->seats[i].alive) {
            count++;
        }
    }
    return count;
}

static int next_alive_player(const struct Game *g, int from)
{
    int offset;

    for (offset = 1; offset <= g->player_count; offset++) {
        int seat = (from + offset) % g->player_count;
        if (g->seats[seat].alive) {
            return seat;
        }
    }
    return -1;
}

static int first_alive_player(const struct Game *g)
{
    int i;

    for (i = 0; i < g->player_count; i++) {
        if (g->seats[i].alive) {
            return i;
        }
    }
    return -1;
}

static int seat_hand_has(const struct Game *g, int seat, int card)
{
    int i;

    for (i = 0; i < g->seats[seat].hand_count; i++) {
        if (g->seats[seat].hand[i] == card) {
            return 1;
        }
    }
    return 0;
}

static int card_symbol_at(const struct Game *g, int card)
{
    return g->map->territoryCards[card].symbol;
}

static int valid_set(const struct Game *g, int c0, int c1, int c2,
                     int *bonus)
{
    int cards[3];
    int wilds = 0;
    int count_a = 0, count_b = 0, count_c = 0;
    int i;
    int best;

    cards[0] = c0;
    cards[1] = c1;
    cards[2] = c2;
    if (!card_valid(g, c0) || !card_valid(g, c1) || !card_valid(g, c2) ||
        c0 == c1 || c0 == c2 || c1 == c2) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        if (g->map->territoryCards[cards[i]].wild) {
            wilds++;
        } else {
            switch (card_symbol_at(g, cards[i])) {
            case 'A': count_a++; break;
            case 'B': count_b++; break;
            case 'C': count_c++; break;
            default: return 0;
            }
        }
    }
    best = 0;
    if (wilds == 0) {
        if (count_a == 3) {
            best = g->map->rules.setBonusAAA;
        } else if (count_b == 3) {
            best = g->map->rules.setBonusBBB;
        } else if (count_c == 3) {
            best = g->map->rules.setBonusCCC;
        } else if (count_a == 1 && count_b == 1 && count_c == 1) {
            best = g->map->rules.setBonusABC;
        } else {
            return 0;
        }
    } else if (wilds == 1) {
        /* Two non-wild cards; equal symbols make three alike, different
         * symbols make one of each. */
        if (count_a == 2) {
            best = g->map->rules.setBonusAAA;
        } else if (count_b == 2) {
            best = g->map->rules.setBonusBBB;
        } else if (count_c == 2) {
            best = g->map->rules.setBonusCCC;
        } else if (count_a == 1 && count_b == 1) {
            best = g->map->rules.setBonusABC;
        } else if (count_a == 1 && count_c == 1) {
            best = g->map->rules.setBonusABC;
        } else if (count_b == 1 && count_c == 1) {
            best = g->map->rules.setBonusABC;
        } else {
            return 0;
        }
    } else if (wilds == 2) {
        /* One non-wild card: either three alike or one of each. */
        best = max_int(g->map->rules.setBonusAAA,
                       max_int(g->map->rules.setBonusBBB,
                               max_int(g->map->rules.setBonusCCC,
                                       g->map->rules.setBonusABC)));
        if (count_a + count_b + count_c != 1) {
            return 0;
        }
    } else {
        best = max_int(g->map->rules.setBonusAAA,
                       max_int(g->map->rules.setBonusBBB,
                               max_int(g->map->rules.setBonusCCC,
                                       g->map->rules.setBonusABC)));
    }
    if (bonus != NULL) {
        *bonus = best;
    }
    return 1;
}

int game_find_card_set(const struct Game *g, int player, int *a, int *b, int *c)
{
    int counts[3] = { 0, 0, 0 };
    int wilds = 0;
    int i, j;
    int h;

    if (g == NULL || player < 0 || player >= g->player_count) {
        return 0;
    }
    for (i = 0; i < g->seats[player].hand_count; i++) {
        int card = g->seats[player].hand[i];
        if (!card_valid(g, card)) {
            continue;
        }
        if (g->map->territoryCards[card].wild) {
            wilds++;
        } else {
            h = card_symbol_at(g, card);
            if (h == 'A') {
                counts[0]++;
            } else if (h == 'B') {
                counts[1]++;
            } else if (h == 'C') {
                counts[2]++;
            }
        }
    }
    if (wilds > 0 && g->seats[player].hand_count >= 3) {
        int wild_card = -1;
        int others[2];
        int other_count = 0;
        for (i = 0; i < g->seats[player].hand_count; i++) {
            int card = g->seats[player].hand[i];
            if (!card_valid(g, card)) {
                continue;
            }
            if (g->map->territoryCards[card].wild) {
                if (wild_card < 0) {
                    wild_card = card;
                }
            } else if (other_count < 2) {
                others[other_count++] = card;
            }
        }
        for (i = 0; i < g->seats[player].hand_count &&
                    other_count < 2; i++) {
            int card = g->seats[player].hand[i];
            if (card_valid(g, card) &&
                g->map->territoryCards[card].wild &&
                card != wild_card) {
                others[other_count++] = card;
            }
        }
        if (wild_card < 0 || other_count < 2) {
            return 0;
        }
        *a = wild_card;
        *b = others[0];
        *c = others[1];
        return 1;
    }
    for (h = 0; h < 3; h++) {
        if (counts[h] >= 3) {
            int picked = 0;
            for (i = 0; i < g->seats[player].hand_count && picked < 3; i++) {
                int card = g->seats[player].hand[i];
                if (!g->map->territoryCards[card].wild &&
                    ((card_symbol_at(g, card) == 'A' && h == 0) ||
                     (card_symbol_at(g, card) == 'B' && h == 1) ||
                     (card_symbol_at(g, card) == 'C' && h == 2))) {
                    if (picked == 0) {
                        *a = card;
                    } else if (picked == 1) {
                        *b = card;
                    } else {
                        *c = card;
                    }
                    picked++;
                }
            }
            return picked == 3;
        }
    }
    if (counts[0] > 0 && counts[1] > 0 && counts[2] > 0) {
        int picked = 0;
        int have[3] = { 0, 0, 0 };
        for (i = 0; i < g->seats[player].hand_count && picked < 3; i++) {
            int card = g->seats[player].hand[i];
            int s;
            if (g->map->territoryCards[card].wild) {
                continue;
            }
            s = card_symbol_at(g, card);
            j = (s == 'A') ? 0 : (s == 'B') ? 1 : 2;
            if (!have[j]) {
                have[j] = 1;
                if (picked == 0) {
                    *a = card;
                } else if (picked == 1) {
                    *b = card;
                } else {
                    *c = card;
                }
                picked++;
            }
        }
        return picked == 3;
    }
    return 0;
}

static int hand_has_set(const struct Game *g, int player)
{
    int a, b, c;
    return game_find_card_set(g, player, &a, &b, &c);
}

static int leaders_target(const struct Game *g, int player)
{
    int territories = game_count_territories(g, player);
    return min_int(g->map->rules.leadersPerPlayer, territories);
}

int game_find_seat(const struct Game *g, const char *username)
{
    int i;

    if (g == NULL || username == NULL) {
        return -1;
    }
    for (i = 0; i < g->player_count; i++) {
        if (strcmp(g->seats[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

int game_count_territories(const struct Game *g, int player)
{
    int i, count = 0;

    if (g == NULL) {
        return 0;
    }
    for (i = 0; i < g->territory_count; i++) {
        if (g->territories[i].owner == player) {
            count++;
        }
    }
    return count;
}


static void shuffle_array(struct DiceRng *rng, int *values, int count);

/* ------------------------------------------------------------------ */
/* Links, sites, adventure cards and missions (Phase E)               */
/* ------------------------------------------------------------------ */

#define MISSION_TRIGGER_START 0
#define MISSION_TRIGGER_CONQUER 1
#define MISSION_TRIGGER_LEADER 2

static int find_link(const struct Game *g, int a, int b)
{
    int i;

    if (g == NULL || g->map == NULL) {
        return -1;
    }
    for (i = 0; i < g->map->linkCount; i++) {
        if ((g->map->links[i].a == a && g->map->links[i].b == b) ||
            (g->map->links[i].a == b && g->map->links[i].b == a)) {
            return i;
        }
    }
    return -1;
}

static int link_is_closed(const struct Game *g, int link)
{
    if (link < 0 || link >= MAP_MAX_LINKS) {
        return 0;
    }
    return g->link_closed_by[link] != 0;
}

static int game_territories_connected(const struct Game *g, int a, int b)
{
    int link;

    if (g == NULL || !terrain_valid(g, a) || !terrain_valid(g, b)) {
        return 0;
    }
    {
        int i;
        for (i = 0; i < g->map->territories[a].adjacentCount; i++) {
            if (g->map->territories[a].adjacent[i] == b) {
                return 1;
            }
        }
    }
    link = find_link(g, a, b);
    return link >= 0 && !link_is_closed(g, link);
}

static void clear_closed_links_for(struct Game *g, int player)
{
    int i;

    for (i = 0; i < g->map->linkCount && i < MAP_MAX_LINKS; i++) {
        if (g->link_closed_by[i] == player + 1) {
            g->link_closed_by[i] = 0;
        }
    }
}

/* Effects that last "until the start of your next turn" end when their
 * owner leaves the game (otherwise a frozen territory stays frozen forever). */
static void clear_printed_effects_for(struct Game *g, int player)
{
    int i;

    if (g->toll_by == player + 1) {
        g->toll_by = 0;
    }
    for (i = 0; i < GAME_MAX_TERRITORIES; i++) {
        if (g->frozen_by[i] == player + 1) {
            g->frozen_by[i] = 0;
        }
    }
    for (i = 0; i < MAP_MAX_BRIDGES; i++) {
        if (g->bridge_blocked_by[i] == player + 1) {
            g->bridge_blocked_by[i] = 0;
        }
    }
}

static int site_is_claimed(const struct Game *g, int player, int territory)
{
    int word = territory / 32;
    unsigned int bit = 1u << (unsigned int)(territory % 32);

    if (g == NULL || player < 0 || player >= g->player_count ||
        word < 0 || word >= GAME_SITE_MASK_WORDS) {
        return 1;
    }
    return (g->seats[player].site_claimed[word] & bit) != 0;
}

static void site_mark_claimed(struct Game *g, int player, int territory)
{
    int word = territory / 32;
    unsigned int bit = 1u << (unsigned int)(territory % 32);

    if (g == NULL || player < 0 || player >= g->player_count ||
        word < 0 || word >= GAME_SITE_MASK_WORDS) {
        return;
    }
    g->seats[player].site_claimed[word] |= bit;
}

static const struct MapAdventureCard *adventure_card_at(const struct Game *g,
                                                        int card)
{
    if (g == NULL || g->map == NULL || card < 0 ||
        card >= g->map->adventureCardCount) {
        return NULL;
    }
    return &g->map->adventureCards[card];
}

static JsonValue *adventure_args(const struct MapAdventureCard *card)
{
    char error[128];

    if (card == NULL || card->args[0] == '\0') {
        return NULL;
    }
    error[0] = '\0';
    return json_parse(card->args, error, sizeof(error));
}

static int adventure_arg_int(const struct MapAdventureCard *card,
                             const char *key, int fallback)
{
    JsonValue *args = adventure_args(card);
    long long value;
    int result = fallback;

    if (args != NULL && json_get_int(json_get(args, key), &value) == 0 &&
        value >= -1000000 && value <= 1000000) {
        result = (int)value;
    }
    json_free(args);
    return result;
}

static int adventure_arg_territory(const struct Game *g,
                                   const struct MapAdventureCard *card,
                                   const char *key)
{
    JsonValue *args = adventure_args(card);
    const char *name;
    int result = -1;

    if (args != NULL) {
        name = json_get_string(json_get(args, key));
        if (name != NULL) {
            result = map_find_territory(g->map, name);
        }
    }
    json_free(args);
    return result;
}

static int adventure_arg_region(const struct Game *g,
                                const struct MapAdventureCard *card,
                                const char *key)
{
    JsonValue *args = adventure_args(card);
    const char *name;
    int result = -1;

    if (args != NULL) {
        name = json_get_string(json_get(args, key));
        if (name != NULL) {
            result = map_find_region(g->map, name);
        }
    }
    json_free(args);
    return result;
}

static int adventure_arg_territory_list(const struct Game *g,
                                        const struct MapAdventureCard *card,
                                        const char *key, int *out, int max)
{
    JsonValue *args = adventure_args(card);
    const JsonValue *list;
    int count = 0;
    size_t i;

    if (args == NULL || out == NULL || max <= 0) {
        json_free(args);
        return 0;
    }
    list = json_get(args, key);
    if (list != NULL && list->type == JSON_ARRAY) {
        for (i = 0; i < json_count(list) && count < max; i++) {
            const char *name = json_get_string(json_at(list, i));
            int territory = (name != NULL) ?
                map_find_territory(g->map, name) : -1;
            if (territory >= 0) {
                out[count++] = territory;
            }
        }
    }
    json_free(args);
    return count;
}

static void adventure_discard_push(struct Game *g, int card)
{
    if (g->adventure_discard_count < MAP_MAX_CARDS) {
        g->adventure_discard[g->adventure_discard_count++] = card;
    }
}

static int seat_remove_adventure(struct Game *g, int seat, int slot)
{
    struct Seat *s;
    int card;
    int i;

    if (g == NULL || seat < 0 || seat >= g->player_count) {
        return -1;
    }
    s = &g->seats[seat];
    if (slot < 0 || slot >= s->adventure_hand_count) {
        return -1;
    }
    card = s->adventure_hand[slot];
    for (i = slot; i + 1 < s->adventure_hand_count; i++) {
        s->adventure_hand[i] = s->adventure_hand[i + 1];
        s->adventure_auto[i] = s->adventure_auto[i + 1];
        s->adventure_auto_n[i] = s->adventure_auto_n[i + 1];
    }
    s->adventure_hand_count--;
    s->adventure_auto[s->adventure_hand_count] = 0;
    s->adventure_auto_n[s->adventure_hand_count] = 0;
    adventure_discard_push(g, card);
    if (g->must_discard_adventure &&
        g->seats[g->current_player].adventure_hand_count <=
        GAME_ADVENTURE_HAND_LIMIT) {
        g->must_discard_adventure = 0;
    }
    return card;
}

static int seat_add_adventure(struct Game *g, int seat, int card)
{
    struct Seat *s;
    int slot;

    if (g == NULL || seat < 0 || seat >= g->player_count) {
        return -1;
    }
    s = &g->seats[seat];
    if (s->adventure_hand_count >= GAME_ADVENTURE_HAND_MAX) {
        adventure_discard_push(g, card);
        return -1;
    }
    slot = s->adventure_hand_count++;
    s->adventure_hand[slot] = card;
    s->adventure_auto[slot] = 0;
    s->adventure_auto_n[slot] = 0;
    if (s->adventure_hand_count > GAME_ADVENTURE_HAND_LIMIT) {
        g->must_discard_adventure = 1;
    }
    return slot;
}

static int remove_one_territory_card(struct Game *g, int seat)
{
    struct Seat *s;
    int card;
    int i;

    if (g == NULL || seat < 0 || seat >= g->player_count) {
        return -1;
    }
    s = &g->seats[seat];
    if (s->hand_count <= 0) {
        return -1;
    }
    card = s->hand[0];
    for (i = 0; i + 1 < s->hand_count; i++) {
        s->hand[i] = s->hand[i + 1];
    }
    s->hand_count--;
    if (g->discard_count < GAME_MAX_CARDS) {
        g->discard[g->discard_count++] = card;
    }
    if (s->hand_count >= 5 && hand_has_set(g, seat)) {
        g->must_trade = 1;
    } else {
        g->must_trade = 0;
    }
    return card;
}

static int draw_adventure_index(struct Game *g)
{
    if (g->adventure_deck_count == 0 && g->adventure_discard_count > 0) {
        memcpy(g->adventure_deck, g->adventure_discard,
               sizeof(int) * (size_t)g->adventure_discard_count);
        g->adventure_deck_count = g->adventure_discard_count;
        g->adventure_discard_count = 0;
        shuffle_array(&g->rng, g->adventure_deck, g->adventure_deck_count);
    }
    if (g->adventure_deck_count <= 0) {
        return -1;
    }
    return g->adventure_deck[--g->adventure_deck_count];
}

static int card_has_effect(const struct MapAdventureCard *card,
                           const char *effect)
{
    return card != NULL && strcmp(card->effect, effect) == 0;
}

static void log_card_played(struct Game *g, int seat, int card,
                            struct EventSink *sink)
{
    const struct MapAdventureCard *mc = adventure_card_at(g, card);
    struct Event *event = sink_new(sink, EVENT_CARD_PLAYED, seat);

    if (event != NULL) {
        event->card_a = card;
        event_text(event, "%s played %s", seat_name(g, seat),
                   mc != NULL ? mc->title : "an adventure card");
    }
}

static int standing_matches(const struct Game *g, int seat, int slot,
                            int stronghold, int attacker_dice)
{
    const struct Seat *s = &g->seats[seat];
    int mode = s->adventure_auto[slot];

    if (mode == 0) {
        return 0;
    }
    if (mode == 1) {
        return stronghold;
    }
    return attacker_dice >= s->adventure_auto_n[slot];
}

static int take_reactive_card(struct Game *g, int seat, const char *effect,
                              int stronghold, int attacker_dice,
                              struct EventSink *sink, int *amount)
{
    struct Seat *s = &g->seats[seat];
    int slot;

    for (slot = 0; slot < s->adventure_hand_count; slot++) {
        const struct MapAdventureCard *mc =
            adventure_card_at(g, s->adventure_hand[slot]);
        if (!card_has_effect(mc, effect) ||
            !standing_matches(g, seat, slot, stronghold, attacker_dice)) {
            continue;
        }
        *amount = adventure_arg_int(mc, "n", 0);
        {
            int card = seat_remove_adventure(g, seat, slot);
            log_card_played(g, seat, card, sink);
        }
        return 1;
    }
    return 0;
}

static int cancel_event_auto(struct Game *g, struct EventSink *sink)
{
    int seat;
    int slot;

    for (seat = 0; seat < g->player_count; seat++) {
        struct Seat *s = &g->seats[seat];
        if (!s->alive) {
            continue;
        }
        for (slot = 0; slot < s->adventure_hand_count; slot++) {
            const struct MapAdventureCard *mc =
                adventure_card_at(g, s->adventure_hand[slot]);
            if (card_has_effect(mc, "CANCEL_EVENT") &&
                s->adventure_auto[slot] != 0) {
                int card = seat_remove_adventure(g, seat, slot);
                log_card_played(g, seat, card, sink);
                return 1;
            }
        }
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/* Printed adventure cards (PLAN.md 10b)                               */
/* ------------------------------------------------------------------ */

static void shuffle_array(struct DiceRng *rng, int *values, int count);
static int has_recruit_spot(const struct Game *g, int player);

static int adventure_arg_string(const struct MapAdventureCard *card,
                                const char *key, char *out, size_t out_size)
{
    JsonValue *args = adventure_args(card);
    const char *text = args != NULL ? json_get_string(json_get(args, key))
                                    : NULL;
    int ok = 0;

    if (out_size > 0) {
        out[0] = '\0';
    }
    if (text != NULL && out_size > 0) {
        snprintf(out, out_size, "%s", text);
        ok = 1;
    }
    json_free(args);
    return ok;
}

static int player_side(const struct Game *g, int seat)
{
    if (seat < 0 || seat >= g->player_count) {
        return MAP_SIDE_NONE;
    }
    return map_faction_side(g->map, g->seats[seat].faction);
}

static int land_adjacent(const struct Game *g, int a, int b)
{
    int i;

    for (i = 0; i < g->map->territories[a].adjacentCount; i++) {
        if (g->map->territories[a].adjacent[i] == b) {
            return 1;
        }
    }
    return 0;
}

/* An invasion along a sea-line (Black Sails, Corsairs, Violent Storms). */
static int is_seaborne(const struct Game *g, int from, int to)
{
    return terrain_valid(g, from) && terrain_valid(g, to) &&
           !land_adjacent(g, from, to) && find_link(g, from, to) >= 0;
}

static void reset_invasion(struct Game *g)
{
    g->inv_from = -1;
    g->inv_to = -1;
    g->inv_flags = 0;
    g->inv_attack_bonus = 0;
}

static void declare_invasion(struct Game *g, int from, int to)
{
    if (g->inv_from != from || g->inv_to != to) {
        reset_invasion(g);
        g->inv_from = from;
        g->inv_to = to;
    }
}

static int bridge_blocked(const struct Game *g, int a, int b)
{
    int bridge = map_bridge_index(g->map, a, b);

    return bridge >= 0 && bridge < MAP_MAX_BRIDGES &&
           g->bridge_blocked_by[bridge] != 0;
}

/* Can `player` invade from `from` into `to` right now?  Adds the printed
 * cards to plain adjacency: Winter Storms freezes territories, a blocked
 * bridge needs the player's Leader in the invading territory, and The Way Is
 * Under the Mountains opens one declared invasion across a range. */
int game_attack_route(const struct Game *g, int player, int from, int to)
{
    if (g == NULL || !terrain_valid(g, from) || !terrain_valid(g, to)) {
        return 0;
    }
    if (g->frozen_by[from] != 0 || g->frozen_by[to] != 0) {
        return 0;
    }
    if (game_territories_connected(g, from, to)) {
        return !bridge_blocked(g, from, to) ||
               g->territories[from].leader_of == player + 1;
    }
    return g->inv_from == from && g->inv_to == to &&
           (g->inv_flags & INV_MOUNTAIN) != 0;
}

static int draw_territory_card(struct Game *g)
{
    if (g->deck_count == 0 && g->discard_count > 0) {
        memcpy(g->deck, g->discard, sizeof(int) * (size_t)g->discard_count);
        g->deck_count = g->discard_count;
        g->discard_count = 0;
        shuffle_array(&g->rng, g->deck, g->deck_count);
    }
    if (g->deck_count <= 0) {
        return -1;
    }
    return g->deck[--g->deck_count];
}

static void discard_territory_card(struct Game *g, int card)
{
    if (card >= 0 && g->discard_count < GAME_MAX_CARDS) {
        g->discard[g->discard_count++] = card;
    }
}

static int card_territory(const struct Game *g, int card)
{
    if (!card_valid(g, card) || g->map->territoryCards[card].wild) {
        return -1;
    }
    return g->map->territoryCards[card].territory;
}

/* "Draw the top N Territory cards": territories (-1 for a wild) go to out,
 * the cards to the discard pile. */
static int draw_event_territories(struct Game *g, int n, int *out)
{
    int count = 0;

    while (count < n) {
        int card = draw_territory_card(g);
        if (card < 0) {
            break;
        }
        out[count++] = card_territory(g, card);
        discard_territory_card(g, card);
    }
    return count;
}

/* Local Uprising: "When you control that Territory, reveal the card and add
 * 4 battalions to that Territory." */
static void check_uprisings(struct Game *g, struct EventSink *sink)
{
    int seat;

    for (seat = 0; seat < g->player_count; seat++) {
        struct Seat *s = &g->seats[seat];
        int t;
        struct Event *event;
        if (!s->alive || s->uprising_card < 0) {
            continue;
        }
        t = card_territory(g, s->uprising_card);
        if (t < 0 || g->territories[t].owner != seat) {
            continue;
        }
        g->territories[t].armies += s->uprising_bonus;
        discard_territory_card(g, s->uprising_card);
        s->uprising_card = -1;
        event = sink_new(sink, EVENT_ADVENTURE, seat);
        if (event != NULL) {
            event->to = t;
            event->count = s->uprising_bonus;
            event_text(event, "Local Uprising: %s revealed %s and added %d "
                       "battalions", seat_name(g, seat), territory_name(g, t),
                       s->uprising_bonus);
        }
        s->uprising_bonus = 0;
    }
}

static void lose_armies(struct Game *g, int t, int loss)
{
    g->territories[t].armies -= loss;
    if (g->territories[t].armies < 1) {
        g->territories[t].armies = 1;   /* "1 battalion must always remain" */
    }
}

/* The printed events that "play immediately".  Returns 1 when handled. */
static int resolve_printed_event(struct Game *g, int drawer, int card,
                                 const struct MapAdventureCard *mc,
                                 struct EventSink *sink)
{
    struct Event *event = NULL;
    int list[8];
    int count, i, n;
    char text[192];
    size_t used;

    if (strcmp(mc->effect, "FREE_LEADER") == 0) {
        int limit = adventure_arg_int(mc, "max", 2);
        event = sink_new(sink, EVENT_ADVENTURE, drawer);
        if (g->seats[drawer].leaders_in_play < limit &&
            has_recruit_spot(g, drawer)) {
            g->pending_free_leader = 1;
            if (event != NULL) {
                event_text(event, "%s: %s places a second Leader",
                           mc->title, seat_name(g, drawer));
            }
        } else if (event != NULL) {
            event_text(event, "%s: %s already has %d Leaders; discarded",
                       mc->title, seat_name(g, drawer), limit);
        }
    } else if (strcmp(mc->effect, "SIDE_CONTROL") == 0) {
        int t = adventure_arg_territory(g, mc, "territory");
        char side_name[16];
        int side, owner;
        adventure_arg_string(mc, "side", side_name, sizeof(side_name));
        side = strcmp(side_name, "evil") == 0 ? MAP_SIDE_EVIL : MAP_SIDE_GOOD;
        if (t < 0) {
            return 1;
        }
        owner = g->territories[t].owner;
        event = sink_new(sink, EVENT_ADVENTURE, drawer);
        if (event != NULL) {
            event->to = t;
        }
        if (owner >= 0 && player_side(g, owner) == side) {
            n = adventure_arg_int(mc, "gain", 0);
            g->territories[t].armies += n;
            if (event != NULL) {
                event_text(event, "%s: %s receives %d battalions in %s",
                           mc->title, seat_name(g, owner), n,
                           territory_name(g, t));
            }
        } else if (owner >= 0 && player_side(g, owner) != MAP_SIDE_NONE) {
            n = adventure_arg_int(mc, "loss", 0);
            lose_armies(g, t, n);
            if (event != NULL) {
                event_text(event, "%s: %s loses %d battalions in %s",
                           mc->title, seat_name(g, owner), n,
                           territory_name(g, t));
            }
        } else if (event != NULL) {
            event_text(event, "%s: no effect on %s", mc->title,
                       territory_name(g, t));
        }
    } else if (strcmp(mc->effect, "SEABORNE_TOLL") == 0) {
        g->toll_by = drawer + 1;
        g->toll_loss = adventure_arg_int(mc, "loss", 3);
        event = sink_new(sink, EVENT_ADVENTURE, drawer);
        if (event != NULL) {
            event_text(event, "%s: until %s's next turn every seaborne "
                       "invasion loses %d battalions first", mc->title,
                       seat_name(g, drawer), g->toll_loss);
        }
    } else if (strcmp(mc->effect, "LOCAL_UPRISING") == 0) {
        struct Seat *seat = &g->seats[drawer];
        int drawn = -1;
        int tries = g->map->territoryCardCount;   /* only wilds may be left */
        while (seat->uprising_card < 0 && tries-- > 0) {
            drawn = draw_territory_card(g);
            if (drawn < 0) {
                break;
            }
            if (card_territory(g, drawn) >= 0) {
                seat->uprising_card = drawn;
                seat->uprising_bonus = adventure_arg_int(mc, "n", 4);
                break;
            }
            discard_territory_card(g, drawn);   /* a wild names no land */
        }
        event = sink_new(sink, EVENT_ADVENTURE, drawer);
        if (event != NULL) {
            event_text(event, "%s: %s secretly drew a Territory card",
                       mc->title, seat_name(g, drawer));
        }
    } else if (strcmp(mc->effect, "RAIDERS") == 0 ||
               strcmp(mc->effect, "MUSTERING") == 0 ||
               strcmp(mc->effect, "WINTER_STORMS") == 0) {
        int raid = strcmp(mc->effect, "RAIDERS") == 0;
        int muster = strcmp(mc->effect, "MUSTERING") == 0;
        n = adventure_arg_int(mc, "n", 3);
        count = draw_event_territories(g, min_int(8, adventure_arg_int(
                                           mc, "cards", 3)), list);
        used = (size_t)snprintf(text, sizeof(text), "%s:", mc->title);
        for (i = 0; i < count; i++) {
            int t = list[i];
            int change = 0;
            if (t < 0) {
                used += (size_t)snprintf(text + used, used < sizeof(text) ?
                                         sizeof(text) - used : 0,
                                         "%s a wild card", i ? "," : "");
                continue;
            }
            if (raid) {
                change = g->territories[t].armies / 2;
                lose_armies(g, t, change);
            } else if (muster) {
                change = n;
                g->territories[t].armies += n;
            } else {
                g->frozen_by[t] = drawer + 1;
            }
            used += (size_t)snprintf(text + used, used < sizeof(text) ?
                                     sizeof(text) - used : 0, "%s %s%s",
                                     i ? "," : "", territory_name(g, t),
                                     raid ? " (-" : muster ? " (+" : "");
            if (raid || muster) {
                used += (size_t)snprintf(text + used, used < sizeof(text) ?
                                         sizeof(text) - used : 0, "%d)",
                                         change);
            }
        }
        if (!raid && !muster && used < sizeof(text)) {
            snprintf(text + used, sizeof(text) - used,
                     " frozen until %s's next turn", seat_name(g, drawer));
        }
        event = sink_new(sink, EVENT_ADVENTURE, drawer);
        if (event != NULL) {
            event->to = count > 0 ? list[0] : -1;
            event_text(event, "%s", text);
        }
    } else {
        return 0;
    }
    if (event != NULL) {
        event->card_a = card;
    }
    return 1;
}

static void resolve_event_effect(struct Game *g, int drawer, int card,
                                 struct EventSink *sink)
{
    const struct MapAdventureCard *mc = adventure_card_at(g, card);
    struct Event *event;

    if (mc == NULL) {
        return;
    }
    if (resolve_printed_event(g, drawer, card, mc, sink)) {
        return;
    }
    if (strcmp(mc->effect, "REGION_ATTRITION") == 0) {
        int region = adventure_arg_region(g, mc, "region");
        int loss = adventure_arg_int(mc, "loss", 1);
        int i;
        if (region >= 0) {
            for (i = 0; i < g->territory_count; i++) {
                if (g->territories[i].owner >= 0 &&
                    g->map->territories[i].region == region &&
                    g->territories[i].armies > 1) {
                    g->territories[i].armies -= loss;
                    if (g->territories[i].armies < 1) {
                        g->territories[i].armies = 1;
                    }
                }
            }
            event = sink_new(sink, EVENT_ADVENTURE, drawer);
            if (event != NULL) {
                event->card_a = card;
                event_text(event, "%s: %s loses %d army per territory",
                           mc->title,
                           g->map->regions[region].name, loss);
            }
        }
    } else if (strcmp(mc->effect, "REINFORCE_ALL") == 0) {
        int n = adventure_arg_int(mc, "n", 1);
        int seat;
        for (seat = 0; seat < g->player_count; seat++) {
            int t;
            if (!g->seats[seat].alive) {
                continue;
            }
            for (t = 0; t < g->territory_count; t++) {
                if (g->territories[t].owner == seat &&
                    g->map->territories[t].stronghold) {
                    g->territories[t].armies += n;
                    break;
                }
            }
        }
        event = sink_new(sink, EVENT_ADVENTURE, drawer);
        if (event != NULL) {
            event->card_a = card;
            event_text(event, "%s: every stronghold got %d armies",
                       mc->title, n);
        }
    } else if (strcmp(mc->effect, "LOSE_LEADER_UNLESS") == 0) {
        int card_index = remove_one_territory_card(g, drawer);
        int lost = 0;
        int i;
        if (card_index < 0) {
            for (i = 0; i < g->territory_count; i++) {
                if (g->territories[i].leader_of == drawer + 1) {
                    g->territories[i].leader_of = 0;
                    if (g->seats[drawer].leaders_in_play > 0) {
                        g->seats[drawer].leaders_in_play--;
                    }
                    lost = 1;
                    break;
                }
            }
        }
        event = sink_new(sink, EVENT_ADVENTURE, drawer);
        if (event != NULL) {
            event->card_a = card;
            if (card_index >= 0) {
                event_text(event, "%s: %s discarded %s", mc->title,
                           seat_name(g, drawer), card_name(g, card_index));
            } else if (lost) {
                event_text(event, "%s: %s lost a leader", mc->title,
                           seat_name(g, drawer));
            } else {
                event_text(event, "%s: no effect", mc->title);
            }
        }
    } else if (strcmp(mc->effect, "SEA_CLOSED") == 0) {
        int a = adventure_arg_territory(g, mc, "a");
        int b = adventure_arg_territory(g, mc, "b");
        int link = find_link(g, a, b);
        if (link >= 0 && link < MAP_MAX_LINKS) {
            g->link_closed_by[link] = drawer + 1;
            event = sink_new(sink, EVENT_SEA_CLOSED, drawer);
            if (event != NULL) {
                event->card_a = card;
                event->from = a;
                event->to = b;
                event_text(event, "%s: the sea route between %s and %s "
                           "is closed until %s's next turn", mc->title,
                           territory_name(g, a), territory_name(g, b),
                           seat_name(g, drawer));
            }
        }
    }
}

static void draw_adventure(struct Game *g, int player, int source,
                           struct EventSink *sink)
{
    int card = draw_adventure_index(g);
    const struct MapAdventureCard *mc;
    struct Event *event;

    if (card < 0) {
        event = sink_new(sink, EVENT_ADVENTURE, player);
        if (event != NULL) {
            event_text(event, "The adventure deck is empty");
        }
        return;
    }
    mc = adventure_card_at(g, card);
    if (mc == NULL) {
        adventure_discard_push(g, card);
        return;
    }
    if (strcmp(mc->kind, "event") == 0) {
        int cancelled = cancel_event_auto(g, sink);
        if (!cancelled) {
            resolve_event_effect(g, player, card, sink);
        }
        adventure_discard_push(g, card);
        event = sink_new(sink, EVENT_ADVENTURE, player);
        if (event != NULL) {
            event->card_a = card;
            event->to = source;
            event_text(event, "%s drew %s%s", seat_name(g, player),
                       mc->title, cancelled ? " (cancelled)" : "");
        }
    } else {
        int slot = seat_add_adventure(g, player, card);
        event = sink_new(sink, EVENT_ADVENTURE, player);
        if (event != NULL) {
            event->card_a = card;
            event->to = source;
            if (slot < 0) {
                event_text(event, "%s drew %s but the hand is full",
                           seat_name(g, player), mc->title);
            } else {
                event_text(event, "%s drew %s%s", seat_name(g, player),
                           mc->title,
                           g->must_discard_adventure ?
                           " (discard down to four)" : "");
            }
        }
    }
}

/* Rulebook step 5: draw 1 adventure card; Events play immediately and the
 * drawing continues until a Mission or Power card is drawn.  Returns 1 while
 * the chain waits for the player (a free leader to place). */
static int run_rulebook_draws(struct Game *g, int player,
                              struct EventSink *sink)
{
    int draws = 0;

    while (g->draw_pending) {
        int card;
        const struct MapAdventureCard *mc;
        struct Event *event;

        if (g->pending_free_leader) {
            return 1;
        }
        /* If every Mission and Power card is in someone's hand, only events
         * are left to draw: stop instead of reshuffling them forever. */
        card = ++draws > g->map->adventureCardCount ? -1
                                                     : draw_adventure_index(g);
        if (card < 0) {
            g->draw_pending = 0;
            event = sink_new(sink, EVENT_ADVENTURE, player);
            if (event != NULL) {
                event_text(event, "The adventure deck is empty");
            }
            break;
        }
        mc = adventure_card_at(g, card);
        if (mc == NULL) {
            adventure_discard_push(g, card);
            continue;
        }
        event = sink_new(sink, EVENT_ADVENTURE, player);
        if (event != NULL) {
            event->card_a = card;
            event_text(event, "%s drew %s", seat_name(g, player), mc->title);
        }
        if (strcmp(mc->kind, "event") == 0) {
            if (!cancel_event_auto(g, sink)) {
                resolve_event_effect(g, player, card, sink);
            }
            adventure_discard_push(g, card);
            continue;
        }
        seat_add_adventure(g, player, card);
        g->draw_pending = 0;
    }
    return g->pending_free_leader ? 1 : 0;
}

static int mission_reward(const struct MapAdventureCard *mc)
{
    return adventure_arg_int(mc, "reward", 1);
}

static int mission_met(const struct Game *g, int player,
                       const struct MapAdventureCard *mc, int trigger)
{
    int i;

    if (mc == NULL || strcmp(mc->kind, "mission") != 0) {
        return 0;
    }
    if (strcmp(mc->effect, "HOLD_TERRITORIES") == 0) {
        int list[MAP_MAX_TERRITORIES];
        int count;
        if (trigger != MISSION_TRIGGER_START &&
            trigger != MISSION_TRIGGER_LEADER) {
            return 0;
        }
        count = adventure_arg_territory_list(g, mc, "territories",
                                              list, MAP_MAX_TERRITORIES);
        if (count == 0) {
            return 0;
        }
        for (i = 0; i < count; i++) {
            if (g->territories[list[i]].owner != player) {
                return 0;
            }
        }
        return 1;
    }
    if (strcmp(mc->effect, "HOLD_REGION") == 0) {
        int region = adventure_arg_region(g, mc, "region");
        int owned = 0;
        int total = 0;
        if (region < 0 || trigger != MISSION_TRIGGER_START) {
            return 0;
        }
        for (i = 0; i < g->territory_count; i++) {
            if (g->map->territories[i].region == region) {
                total++;
                if (g->territories[i].owner == player) {
                    owned++;
                }
            }
        }
        return total > 0 && owned == total;
    }
    if (strcmp(mc->effect, "CONQUER_N_THIS_TURN") == 0) {
        int n = adventure_arg_int(mc, "n", 1);
        return (trigger == MISSION_TRIGGER_CONQUER ||
                trigger == MISSION_TRIGGER_START) &&
            g->territories_conquered_this_turn >= n;
    }
    if (strcmp(mc->effect, "LEADER_REACH") == 0) {
        int target = adventure_arg_territory(g, mc, "territory");
        if (target < 0) {
            return 0;
        }
        return g->territories[target].owner == player &&
            g->territories[target].leader_of == player + 1;
    }
    if (strcmp(mc->effect, "HOLD_STRONGHOLDS") == 0) {
        int n = adventure_arg_int(mc, "n", 1);
        int strongholds = 0;
        if (trigger != MISSION_TRIGGER_START) {
            return 0;
        }
        for (i = 0; i < g->territory_count; i++) {
            if (g->territories[i].owner == player &&
                g->map->territories[i].stronghold) {
                strongholds++;
            }
        }
        return strongholds >= n;
    }
    return 0;
}

static void check_missions(struct Game *g, int player, int trigger,
                           struct EventSink *sink)
{
    int slot = 0;

    while (slot < g->seats[player].adventure_hand_count) {
        int card = g->seats[player].adventure_hand[slot];
        const struct MapAdventureCard *mc = adventure_card_at(g, card);
        if (mission_met(g, player, mc, trigger)) {
            int reward = mission_reward(mc);
            struct Event *event;
            seat_remove_adventure(g, player, slot);
            g->seats[player].missions_done++;
            if (reward > 0) {
                if (g->phase == PHASE_REINFORCE &&
                    g->current_player == player) {
                    g->reinforcements_pending += reward;
                    g->reinforce_info.card_bonus += reward;
                    g->reinforce_info.total += reward;
                } else {
                    g->deferred_card_bonus[player] += reward;
                }
            }
            event = sink_new(sink, EVENT_MISSION_COMPLETE, player);
            if (event != NULL) {
                event->card_a = card;
                event->count = reward;
                event_text(event, "%s completed %s (+%d armies%s)",
                           seat_name(g, player),
                           mc != NULL ? mc->title : "a mission", reward,
                           (g->phase == PHASE_REINFORCE &&
                            g->current_player == player) ?
                           "" : " next turn");
            }
        } else {
            slot++;
        }
    }
}

static void grant_site_draw(struct Game *g, int player, int territory,
                            struct EventSink *sink)
{
    if (!terrain_valid(g, territory) ||
        !g->map->territories[territory].site ||
        g->territories[territory].owner != player ||
        site_is_claimed(g, player, territory)) {
        return;
    }
    site_mark_claimed(g, player, territory);
    draw_adventure(g, player, territory, sink);
}

static void init_territories(struct Game *g)
{
    int i;

    for (i = 0; i < g->territory_count; i++) {
        g->territories[i].owner = -1;
        g->territories[i].armies = 0;
        g->territories[i].leader_of = 0;
        g->territories[i].neutral = 0;
    }
}

static void shuffle_array(struct DiceRng *rng, int *values, int count)
{
    int i;

    for (i = count - 1; i > 0; i--) {
        int j = dice_range(rng, i + 1);
        int tmp = values[i];
        values[i] = values[j];
        values[j] = tmp;
    }
}

static void compute_reinforce_base(const struct Game *g, int player,
                                   struct ReinforcementInfo *info)
{
    int region_total[MAP_MAX_REGIONS];
    int region_owned[MAP_MAX_REGIONS];
    int i;

    memset(info, 0, sizeof(*info));
    memset(region_total, 0, sizeof(region_total));
    memset(region_owned, 0, sizeof(region_owned));
    if (g == NULL || g->map == NULL || player < 0 ||
        player >= g->player_count) {
        return;
    }
    for (i = 0; i < g->territory_count; i++) {
        int region = g->map->territories[i].region;
        if (region >= 0 && region < g->map->regionCount) {
            region_total[region]++;
            if (g->territories[i].owner == player) {
                region_owned[region]++;
            }
        }
        if (g->territories[i].owner == player) {
            info->territories++;
            if (g->map->territories[i].stronghold) {
                info->strongholds_held++;
            }
        }
    }
    info->base = info->territories / g->map->rules.territoriesPerArmy;
    if (info->base < g->map->rules.minReinforce) {
        info->base = g->map->rules.minReinforce;
    }
    for (i = 0; i < g->map->regionCount; i++) {
        if (region_total[i] > 0 && region_owned[i] == region_total[i]) {
            info->region_bonus += g->map->regions[i].bonus;
            info->regions_held++;
        }
    }
    info->stronghold_bonus =
        info->strongholds_held * g->map->rules.strongholdReinforce;
    info->card_bonus = 0;
    info->total = info->base + info->region_bonus + info->stronghold_bonus;
}

int game_reinforcements(const struct Game *g, int player,
                        struct ReinforcementInfo *out)
{
    struct ReinforcementInfo info;

    if (g == NULL || out == NULL || player < 0 ||
        player >= g->player_count) {
        return -1;
    }
    compute_reinforce_base(g, player, &info);
    if (g->phase == PHASE_REINFORCE && g->current_player == player) {
        info.card_bonus = g->reinforce_info.card_bonus;
    } else {
        info.card_bonus = 0;
    }
    info.total = info.base + info.region_bonus + info.stronghold_bonus +
                 info.card_bonus;
    *out = info;
    return 0;
}

static void begin_turn_state(struct Game *g, int player,
                             struct EventSink *sink, struct Event *event)
{
    struct ReinforcementInfo info;

    clear_closed_links_for(g, player);
    g->seats[player].attack_bonus = 0;
    clear_printed_effects_for(g, player);   /* "until your next turn" */
    reset_invasion(g);
    g->leader_conquered_site = 0;
    g->draw_pending = 0;
    g->pending_free_leader = 0;
    g->bonus_pending = 0;
    g->bonus_area = BONUS_NONE;
    compute_reinforce_base(g, player, &info);
    info.card_bonus = g->deferred_card_bonus[player];
    info.total = info.base + info.region_bonus + info.stronghold_bonus +
                 info.card_bonus;
    g->deferred_card_bonus[player] = 0;
    g->phase = PHASE_REINFORCE;
    g->current_player = player;
    g->conquered_this_turn = 0;
    g->territories_conquered_this_turn = 0;
    g->cards_traded_this_turn = 0;
    g->fortified_this_turn = 0;
    g->extra_fortifies = 0;
    g->pending_from = -1;
    g->pending_to = -1;
    g->pending_min = 0;
    g->pending_max = 0;
    g->reinforce_info = info;
    g->reinforcements_pending = info.total;
    check_missions(g, player, MISSION_TRIGGER_START, sink);
    g->must_trade = (g->seats[player].hand_count >= 5 &&
                     hand_has_set(g, player)) ? 1 : 0;
    if (event != NULL) {
        memset(event, 0, sizeof(*event));
        event->kind = EVENT_TURN_BEGIN;
        event->player = player;
        event->count = info.total;
        event_text(event, "%s begins turn %d with %d reinforcements",
                   seat_name(g, player), g->turn, info.total);
    }
}

static void finish_setup_state(struct Game *g, struct EventSink *sink)
{
    int i;
    struct Event *event;

    g->pending_from = -1;
    g->pending_to = -1;
    if (g->map->rules.leadersPerPlayer > 0) {
        g->phase = PHASE_SETUP_LEADERS;
        g->current_player = 0;
        for (i = 0; i < g->player_count; i++) {
            g->seats[i].leaders_in_play = 0;
        }
        event = sink_new(sink, EVENT_SETUP_DONE, -1);
        event_text(event, "Armies placed; leaders are placed next");
        return;
    }
    event = sink_new(sink, EVENT_SETUP_DONE, -1);
    event_text(event, "Setup complete");
    {
        struct Event turn_event;
        begin_turn_state(g, 0, sink, &turn_event);
        if (sink != NULL && sink->count < GAME_MAX_EVENTS_PER_ACTION) {
            sink->events[sink->count++] = turn_event;
        }
        g->phase = PHASE_REINFORCE;
    }
}

static void advance_setup_place(struct Game *g, struct EventSink *sink)
{
    int i;
    struct Event *event;

    for (i = 1; i <= g->player_count; i++) {
        int seat = (g->current_player + i) % g->player_count;
        if (g->setup_pool[seat] > 0) {
            g->current_player = seat;
            g->setup_batch_remaining =
                min_int(g->setup_batch_size, g->setup_pool[seat]);
            event = sink_new(sink, EVENT_PHASE_CHANGE, seat);
            event_text(event, "%s now places reinforcements",
                       seat_name(g, seat));
            return;
        }
    }
    finish_setup_state(g, sink);
}

static int count_unclaimed(const struct Game *g)
{
    int i, count = 0;

    for (i = 0; i < g->territory_count; i++) {
        if (g->territories[i].owner == -1 &&
            !g->territories[i].neutral &&
            g->territories[i].armies == 0) {
            count++;
        }
    }
    return count;
}

static void start_placement_phase(struct Game *g, struct EventSink *sink)
{
    int i;
    struct Event *event;

    for (i = 0; i < g->player_count; i++) {
        if (g->setup_pool[i] > 0) {
            g->phase = PHASE_SETUP_PLACE;
            g->current_player = i;
            g->setup_batch_remaining =
                min_int(g->setup_batch_size, g->setup_pool[i]);
            event = sink_new(sink, EVENT_PHASE_CHANGE, i);
            event_text(event, "%s begins placing the remaining armies",
                       seat_name(g, i));
            return;
        }
    }
    finish_setup_state(g, sink);
}

static void advance_setup_claim(struct Game *g, struct EventSink *sink)
{
    int i;

    if (count_unclaimed(g) == 0) {
        start_placement_phase(g, sink);
        return;
    }
    for (i = 1; i <= g->player_count; i++) {
        int seat = (g->current_player + i) % g->player_count;
        if (g->setup_pool[seat] > 0) {
            g->current_player = seat;
            return;
        }
    }
}

static int first_pool_player(const struct Game *g)
{
    int i;
    for (i = 0; i < g->player_count; i++) {
        if (g->setup_pool[i] > 0) {
            return i;
        }
    }
    return -1;
}

/* Rulebook: "Remove the Event cards from the Adventure card deck and set
 * these aside. Then deal 4 Adventure cards, facedown, to each player.
 * Return the Event cards to the Adventure card deck, shuffle." */
static void deal_start_adventures(struct Game *g)
{
    int hand = g->map->rules.adventureStartHand;
    int keep[GAME_MAX_CARDS];
    int keep_count = 0;
    int i, seat, dealt;

    if (hand <= 0) {
        return;
    }
    for (i = 0; i < g->adventure_deck_count; i++) {
        const struct MapAdventureCard *mc =
            adventure_card_at(g, g->adventure_deck[i]);
        if (mc != NULL && strcmp(mc->kind, "event") == 0) {
            keep[keep_count++] = g->adventure_deck[i];   /* set aside */
        }
    }
    dealt = 0;
    for (i = 0; i < g->adventure_deck_count; i++) {
        int card = g->adventure_deck[i];
        const struct MapAdventureCard *mc = adventure_card_at(g, card);
        if (mc == NULL || strcmp(mc->kind, "event") == 0) {
            continue;
        }
        seat = dealt / hand;
        if (seat < g->player_count) {
            struct Seat *s = &g->seats[seat];
            s->adventure_hand[s->adventure_hand_count] = card;
            s->adventure_auto[s->adventure_hand_count] = 0;
            s->adventure_auto_n[s->adventure_hand_count] = 0;
            s->adventure_hand_count++;
            dealt++;
        } else {
            keep[keep_count++] = card;
        }
    }
    memcpy(g->adventure_deck, keep, sizeof(int) * (size_t)keep_count);
    g->adventure_deck_count = keep_count;
    shuffle_array(&g->rng, g->adventure_deck, g->adventure_deck_count);
}

void game_rebuild_adventure_deck(struct Game *g)
{
    int i;

    if (g == NULL || g->map == NULL) {
        return;
    }
    for (i = 0; i < g->player_count; i++) {
        g->seats[i].adventure_hand_count = 0;
        memset(g->seats[i].adventure_auto, 0,
               sizeof(g->seats[i].adventure_auto));
        memset(g->seats[i].adventure_auto_n, 0,
               sizeof(g->seats[i].adventure_auto_n));
    }
    g->adventure_deck_count = g->map->adventureCardCount;
    for (i = 0; i < g->adventure_deck_count; i++) {
        g->adventure_deck[i] = i;
    }
    g->adventure_discard_count = 0;
    shuffle_array(&g->rng, g->adventure_deck, g->adventure_deck_count);
    if (g->status == GAME_RUNNING) {
        deal_start_adventures(g);
    }
}

static int seat_has_faction(const struct Game *g, int faction)
{
    int i;
    for (i = 0; i < g->player_count; i++) {
        if (g->seats[i].faction == faction) {
            return 1;
        }
    }
    return 0;
}

void game_init(struct Game *g, const struct Map *map, int id,
               const char *name, const char *host, uint64_t seed)
{
    const char *base;

    if (g == NULL) {
        return;
    }
    memset(g, 0, sizeof(*g));
    g->map = map;
    g->id = id;
    g->status = GAME_OPEN;
    g->phase = PHASE_LOBBY;
    g->version = 1;
    g->max_players = GAME_MAX_PLAYERS;
    g->winner = -1;
    g->current_player = -1;
    g->pending_from = -1;
    g->pending_to = -1;
    g->kick_target = -1;
    g->inv_from = -1;
    g->inv_to = -1;
    g->start_mode = GAME_START_DEAL;
    g->setup_batch_size = 5;
    copy_text(g->name, sizeof(g->name), name);
    copy_text(g->host, sizeof(g->host), host);
    if (map != NULL) {
        base = strrchr(map->dir, '/');
        base = (base != NULL) ? base + 1 : map->dir;
        copy_text(g->map_pack, sizeof(g->map_pack), base);
        g->territory_count = map->territoryCount;
        init_territories(g);
    }
    g->created = time(NULL);
    g->updated = g->created;
    if (seed == 0) {
        dice_seed_system(&g->rng);
    } else {
        dice_seed(&g->rng, seed);
    }
}

int game_join(struct Game *g, const char *username, int faction,
              char *err, size_t err_size)
{
    struct Seat *seat;
    int i;

    if (g == NULL || username == NULL || username[0] == '\0') {
        set_error(err, err_size, "invalid player name");
        return -1;
    }
    if (g->status != GAME_OPEN || g->phase != PHASE_LOBBY) {
        set_error(err, err_size, "game is not open");
        return -1;
    }
    if (g->max_players < 2 || g->max_players > GAME_MAX_PLAYERS) {
        g->max_players = GAME_MAX_PLAYERS;
    }
    if (g->player_count >= g->max_players) {
        set_error(err, err_size, "game is full");
        return -1;
    }
    if (strlen(username) >= sizeof(g->seats[0].username)) {
        set_error(err, err_size, "player name is too long");
        return -1;
    }
    if (game_find_seat(g, username) >= 0) {
        set_error(err, err_size, "player already joined");
        return -1;
    }
    if (g->map == NULL || g->map->factionCount < 1) {
        set_error(err, err_size, "map has no factions");
        return -1;
    }
    if (faction < 0) {
        int chosen = -1;
        for (i = 0; i < g->map->factionCount; i++) {
            if (!seat_has_faction(g, i)) {
                chosen = i;
                break;
            }
        }
        if (chosen < 0) {
            set_error(err, err_size, "no faction is available");
            return -1;
        }
        faction = chosen;
    } else {
        if (faction >= g->map->factionCount) {
            set_error(err, err_size, "faction is out of range");
            return -1;
        }
        if (seat_has_faction(g, faction)) {
            set_error(err, err_size, "faction is already taken");
            return -1;
        }
    }
    seat = &g->seats[g->player_count];
    memset(seat, 0, sizeof(*seat));
    copy_text(seat->username, sizeof(seat->username), username);
    seat->uprising_card = -1;
    seat->faction = faction;
    seat->alive = 0;
    g->player_count++;
    g->version++;
    g->updated = time(NULL);
    return 0;
}

static int start_deal(struct Game *g, const struct MapSetup *setup,
                      int neutral_count)
{
    int *ids;
    int remaining;
    int *pool;
    int i;
    int assigned[GAME_MAX_PLAYERS];

    remaining = g->territory_count - neutral_count;
    if (remaining < g->player_count) {
        return -1;
    }
    if (setup->armies * g->player_count < remaining) {
        return -1;
    }
    if (setup->armies < (remaining + g->player_count - 1) /
                        g->player_count) {
        return -1;
    }
    ids = (int *)malloc(sizeof(int) * (size_t)remaining);
    pool = (int *)malloc(sizeof(int) * (size_t)g->player_count);
    if (ids == NULL || pool == NULL) {
        free(ids);
        free(pool);
        return -1;
    }
    remaining = 0;
    for (i = 0; i < g->territory_count; i++) {
        if (!g->territories[i].neutral) {
            ids[remaining++] = i;
        }
    }
    shuffle_array(&g->rng, ids, remaining);
    memset(assigned, 0, sizeof(assigned));
    for (i = 0; i < remaining; i++) {
        int seat = i % g->player_count;
        g->territories[ids[i]].owner = seat;
        g->territories[ids[i]].armies = 1;
        g->territories[ids[i]].neutral = 0;
        assigned[seat]++;
    }
    for (i = 0; i < g->player_count; i++) {
        pool[i] = setup->armies - assigned[i];
        if (pool[i] < 0) {
            free(ids);
            free(pool);
            return -1;
        }
    }
    for (i = 0; i < g->player_count; i++) {
        g->setup_pool[i] = pool[i];
    }
    free(ids);
    free(pool);
    g->phase = PHASE_SETUP_PLACE;
    g->current_player = first_pool_player(g);
    if (g->current_player < 0) {
        return 1; /* no placement needed */
    }
    g->setup_batch_remaining =
        min_int(g->setup_batch_size, g->setup_pool[g->current_player]);
    return 0;
}

int game_start(struct Game *g, int start_mode, int placement_batch,
               char *err, size_t err_size)
{
    struct EventSink sink;
    struct Event *event;
    const struct MapSetup *setup;
    int neutral_count = 0;
    int i;
    int neutral_armies;
    int remaining;

    if (g == NULL || g->map == NULL) {
        set_error(err, err_size, "game has no map");
        return -1;
    }
    if (g->status != GAME_OPEN || g->phase != PHASE_LOBBY) {
        set_error(err, err_size, "game is not open");
        return -1;
    }
    if (g->max_players < 2 || g->max_players > GAME_MAX_PLAYERS) {
        set_error(err, err_size, "invalid maximum player count");
        return -1;
    }
    if (g->player_count < 2 || g->player_count > g->max_players) {
        set_error(err, err_size, "games need 2 to %d players", g->max_players);
        return -1;
    }
    if (g->map->factionCount < g->player_count) {
        set_error(err, err_size, "map has fewer factions than players");
        return -1;
    }
    if (start_mode != GAME_START_DEAL && start_mode != GAME_START_DRAFT) {
        set_error(err, err_size, "invalid start mode");
        return -1;
    }
    setup = &g->map->setup[g->player_count];
    if (!setup->present || setup->armies < 1) {
        set_error(err, err_size, "map has no setup for this player count");
        return -1;
    }
    if (placement_batch <= 0) {
        placement_batch = 5;
    }
    if (placement_batch > 1000) {
        placement_batch = 1000;
    }

    /* Validate the starting armies before mutating the game.  A failed
     * start used to leave status=RUNNING/phase=LOBBY behind. */
    if (setup->neutral) {
        int share = setup->neutralSharePercent;
        share = max_int(0, min_int(100, share));
        neutral_count = (g->map->territoryCount * share + 50) / 100;
        neutral_count = min_int(neutral_count,
                                g->map->territoryCount - g->player_count);
        if (neutral_count < 0) {
            neutral_count = 0;
        }
    }
    remaining = g->map->territoryCount - neutral_count;
    if (remaining < g->player_count ||
        (long long)setup->armies * g->player_count < remaining ||
        (start_mode == GAME_START_DEAL &&
         setup->armies < (remaining + g->player_count - 1) /
                          g->player_count)) {
        set_error(err, err_size,
                  "not enough starting armies to set up the board");
        return -1;
    }

    /* Shuffle seat order before the board is dealt. */
    for (i = g->player_count - 1; i > 0; i--) {
        int j = dice_range(&g->rng, i + 1);
        struct Seat tmp = g->seats[i];
        g->seats[i] = g->seats[j];
        g->seats[j] = tmp;
    }
    for (i = 0; i < g->player_count; i++) {
        memset(&g->seats[i].stats, 0, sizeof(g->seats[i].stats));
        g->seats[i].alive = 1;
        g->seats[i].hand_count = 0;
        g->seats[i].adventure_hand_count = 0;
        memset(g->seats[i].adventure_auto, 0,
               sizeof(g->seats[i].adventure_auto));
        memset(g->seats[i].adventure_auto_n, 0,
               sizeof(g->seats[i].adventure_auto_n));
        memset(g->seats[i].site_claimed, 0,
               sizeof(g->seats[i].site_claimed));
        g->seats[i].attack_bonus = 0;
        g->seats[i].leaders_in_play = 0;
        g->seats[i].missions_done = 0;
        g->seats[i].last_seen_version = g->version;
    }
    memset(&sink, 0, sizeof(sink));
    g->status = GAME_RUNNING;
    g->phase = PHASE_LOBBY;
    g->start_mode = start_mode;
    g->setup_batch_size = placement_batch;
    g->turn = 1;
    g->winner = -1;
    g->kick_target = -1;
    g->kick_votes = 0;
    g->kick_started = 0;
    memset(g->deferred_card_bonus, 0, sizeof(g->deferred_card_bonus));
    g->conquered_this_turn = 0;
    g->cards_traded_this_turn = 0;
    g->fortified_this_turn = 0;
    g->must_trade = 0;
    g->reinforcements_pending = 0;
    memset(&g->reinforce_info, 0, sizeof(g->reinforce_info));
    memset(&g->log, 0, sizeof(g->log));
    g->territory_count = g->map->territoryCount;
    init_territories(g);

    /* Territory deck starts complete and shuffled. */
    g->deck_count = g->map->territoryCardCount;
    for (i = 0; i < g->deck_count; i++) {
        g->deck[i] = i;
    }
    shuffle_array(&g->rng, g->deck, g->deck_count);
    g->discard_count = 0;
    g->adventure_deck_count = g->map->adventureCardCount;
    for (i = 0; i < g->adventure_deck_count; i++) {
        g->adventure_deck[i] = i;
    }
    shuffle_array(&g->rng, g->adventure_deck, g->adventure_deck_count);
    g->adventure_discard_count = 0;
    for (i = 0; i < g->player_count; i++) {
        g->seats[i].uprising_card = -1;
        g->seats[i].uprising_bonus = 0;
        g->seats[i].oathbreakers = 0;
        g->seats[i].extra_card_draws = 0;
    }
    reset_invasion(g);
    deal_start_adventures(g);

    if (setup->neutral) {
        int share = setup->neutralSharePercent;
        share = max_int(0, min_int(100, share));
        neutral_count = (g->territory_count * share + 50) / 100;
        neutral_count = min_int(neutral_count,
                                g->territory_count - g->player_count);
        if (neutral_count < 0) {
            neutral_count = 0;
        }
        neutral_armies = max_int(1, setup->neutralArmies);
        if (neutral_count > 0) {
            int *ids = (int *)malloc(sizeof(int) *
                                     (size_t)g->territory_count);
            if (ids == NULL) {
                g->status = GAME_OPEN;
                g->phase = PHASE_LOBBY;
                set_error(err, err_size, "out of memory");
                return -1;
            }
            for (i = 0; i < g->territory_count; i++) {
                ids[i] = i;
            }
            shuffle_array(&g->rng, ids, g->territory_count);
            for (i = 0; i < neutral_count; i++) {
                g->territories[ids[i]].owner = -1;
                g->territories[ids[i]].armies = neutral_armies;
                g->territories[ids[i]].neutral = 1;
            }
            free(ids);
        }
    }
    g->neutral_enabled = neutral_count > 0;

    if (start_mode == GAME_START_DEAL) {
        int rc = start_deal(g, setup, neutral_count);
        if (rc < 0) {
            g->status = GAME_OPEN;
            g->phase = PHASE_LOBBY;
            set_error(err, err_size,
                      "not enough starting armies to deal the board");
            return -1;
        }
        if (rc == 1) {
            start_placement_phase(g, &sink);
        } else {
            event = sink_new(&sink, EVENT_START, -1);
            event_text(event, "Game started: territories dealt");
        }
    } else {
        int remaining = g->territory_count - neutral_count;
        if (remaining < g->player_count ||
            (long long)setup->armies * g->player_count < remaining) {
            g->status = GAME_OPEN;
            g->phase = PHASE_LOBBY;
            set_error(err, err_size,
                      "not enough starting armies for a draft");
            return -1;
        }
        for (i = 0; i < g->player_count; i++) {
            g->setup_pool[i] = setup->armies;
        }
        g->phase = PHASE_SETUP_CLAIM;
        g->current_player = 0;
        g->setup_batch_remaining = 0;
        event = sink_new(&sink, EVENT_START, -1);
        event_text(event, "Game started: claim a territory");
    }
    if (g->phase == PHASE_SETUP_PLACE) {
        if (g->current_player < 0) {
            start_placement_phase(g, &sink);
        } else {
            event = sink_new(&sink, EVENT_PHASE_CHANGE,
                             g->current_player);
            event_text(event, "%s begins placing armies",
                       seat_name(g, g->current_player));
        }
    }
    g->version++;
    g->updated = time(NULL);
    for (i = 0; i < sink.count; i++) {
        log_append(g, &sink.events[i]);
    }
    return 0;
}

static void finish_game(struct Game *g, int winner, struct EventSink *sink)
{
    int i;
    struct Event *event;

    g->status = GAME_FINISHED;
    g->phase = PHASE_FINISHED;
    g->winner = winner;
    g->pending_from = -1;
    g->pending_to = -1;
    g->pending_min = 0;
    g->pending_max = 0;
    g->must_trade = 0;
    g->kick_target = -1;
    g->kick_votes = 0;
    g->kick_started = 0;
    g->reinforcements_pending = 0;
    for (i = 0; i < g->player_count; i++) {
        g->seats[i].stats.games_played++;
        if (i == winner) {
            g->seats[i].stats.wins++;
        } else {
            g->seats[i].stats.losses++;
        }
    }
    event = sink_new(sink, EVENT_GAME_OVER, winner);
    if (event != NULL) {
        event_text(event, "%s wins the game",
                   winner >= 0 ? seat_name(g, winner) : "nobody");
    }
    if (g->end_hook != NULL) {
        g->end_hook(g, winner, g->end_hook_user);
    }
}

static void clear_kick_vote_for(struct Game *g, int player)
{
    if (g->kick_target == player) {
        g->kick_target = -1;
        g->kick_votes = 0;
        g->kick_started = 0;
    }
    if (player >= 0 && player < 32) {
        g->kick_votes &= ~(1u << (unsigned int)player);
    }
}

static void eliminate_player(struct Game *g, int player, int conqueror,
                             struct EventSink *sink)
{
    int i;
    int living;
    struct Event *event;

    if (player < 0 || player >= g->player_count ||
        !g->seats[player].alive) {
        return;
    }
    g->seats[player].alive = 0;
    clear_kick_vote_for(g, player);
    clear_closed_links_for(g, player);
    clear_printed_effects_for(g, player);
    /* Territory cards go to the conqueror. */
    for (i = 0; i < g->seats[player].hand_count; i++) {
        int card = g->seats[player].hand[i];
        if (conqueror >= 0 &&
            g->seats[conqueror].hand_count < GAME_MAX_CARDS) {
            g->seats[conqueror].hand[g->seats[conqueror].hand_count++] =
                card;
        } else if (g->discard_count < GAME_MAX_CARDS) {
            g->discard[g->discard_count++] = card;
        }
    }
    g->seats[player].hand_count = 0;
    for (i = 0; i < g->seats[player].adventure_hand_count; i++) {
        if (g->adventure_discard_count < GAME_MAX_CARDS) {
            g->adventure_discard[g->adventure_discard_count++] =
                g->seats[player].adventure_hand[i];
        }
    }
    g->seats[player].adventure_hand_count = 0;
    discard_territory_card(g, g->seats[player].uprising_card);
    g->seats[player].uprising_card = -1;
    g->seats[player].oathbreakers = 0;
    for (i = 0; i < g->territory_count; i++) {
        if (g->territories[i].leader_of == player + 1) {
            g->territories[i].leader_of = 0;
        }
    }
    g->seats[player].leaders_in_play = 0;
    event = sink_new(sink, EVENT_ELIMINATED, player);
    if (event != NULL) {
        event_text(event, "%s has been eliminated", seat_name(g, player));
    }
    if (conqueror >= 0 && g->seats[conqueror].hand_count >= 5 &&
        hand_has_set(g, conqueror)) {
        g->must_trade = 1;
    } else if (conqueror >= 0) {
        g->must_trade = 0;
    }
    living = count_alive(g);
    if (living <= 1) {
        finish_game(g, first_alive_player(g), sink);
    }
}

static int apply_claim(struct Game *g, int player, const struct Action *a,
                       struct EventSink *sink, char *err, size_t err_size)
{
    int t = a->territory;
    struct Event *event;

    if (g->phase != PHASE_SETUP_CLAIM) {
        set_error(err, err_size, "not in the claim phase");
        return -1;
    }
    if (!terrain_valid(g, t) || g->territories[t].owner != -1 ||
        g->territories[t].neutral || g->territories[t].armies != 0) {
        set_error(err, err_size, "territory is not claimable");
        return -1;
    }
    if (g->setup_pool[player] < 1) {
        set_error(err, err_size, "you have no armies left to claim with");
        return -1;
    }
    g->territories[t].owner = player;
    g->territories[t].armies = 1;
    g->territories[t].neutral = 0;
    g->setup_pool[player]--;
    event = sink_new(sink, EVENT_CLAIM, player);
    if (event != NULL) {
        event->to = t;
        event_text(event, "%s claimed %s", seat_name(g, player),
                   territory_name(g, t));
    }
    advance_setup_claim(g, sink);
    return 0;
}

static int area_target_ok(const struct Game *g, int player, int t,
                          int area, int area_id)
{
    if (!terrain_valid(g, t) || g->territories[t].owner != player) {
        return 0;
    }
    switch (area) {
    case BONUS_ANY:
        return 1;
    case BONUS_REGION:
        return g->map->territories[t].region == area_id;
    case BONUS_TERRITORY:
        return t == area_id;
    case BONUS_PORT:
        return g->map->territories[t].port;
    default:
        return 0;
    }
}

static int bonus_target_ok(const struct Game *g, int player, int t)
{
    return area_target_ok(g, player, t, g->bonus_area, g->bonus_area_id);
}

static int apply_place(struct Game *g, int player, const struct Action *a,
                       struct EventSink *sink, char *err, size_t err_size)
{
    int t = a->territory;
    int amount = a->count;
    struct Event *event;

    if (!terrain_valid(g, t)) {
        set_error(err, err_size, "invalid territory");
        return -1;
    }
    if (g->territories[t].owner != player) {
        set_error(err, err_size, "you do not own this territory");
        return -1;
    }
    if (amount < 1) {
        set_error(err, err_size, "must place at least one army");
        return -1;
    }
    if (g->bonus_pending > 0) {
        if (!bonus_target_ok(g, player, t)) {
            set_error(err, err_size, "mission battalions cannot go there");
            return -1;
        }
        if (amount > g->bonus_pending ||
            ((g->bonus_area == BONUS_ANY || g->bonus_area == BONUS_PORT) &&
             amount != g->bonus_pending)) {
            set_error(err, err_size, g->bonus_area == BONUS_ANY ||
                      g->bonus_area == BONUS_PORT
                      ? "place all mission battalions in one territory"
                      : "not that many mission battalions");
            return -1;
        }
        g->territories[t].armies += amount;
        g->bonus_pending -= amount;
        if (g->bonus_pending == 0) {
            g->bonus_area = BONUS_NONE;
        }
        event = sink_new(sink, EVENT_PLACE, player);
        if (event != NULL) {
            event->to = t;
            event->count = amount;
            event_text(event, "%s placed %d mission battalions in %s",
                       seat_name(g, player), amount, territory_name(g, t));
        }
        return 0;
    }
    if (g->phase == PHASE_SETUP_PLACE) {
        if (amount > g->setup_pool[player] ||
            amount > g->setup_batch_remaining) {
            set_error(err, err_size, "not that many armies available");
            return -1;
        }
        g->territories[t].armies += amount;
        g->setup_pool[player] -= amount;
        g->setup_batch_remaining -= amount;
        event = sink_new(sink, EVENT_PLACE, player);
        if (event != NULL) {
            event->to = t;
            event->count = amount;
            event_text(event, "%s placed %d armies in %s",
                       seat_name(g, player), amount, territory_name(g, t));
        }
        if (g->setup_batch_remaining == 0 || g->setup_pool[player] == 0) {
            advance_setup_place(g, sink);
        }
        return 0;
    }
    if (g->phase == PHASE_REINFORCE) {
        if (g->must_trade) {
            set_error(err, err_size,
                      "you must trade a card set before placing armies");
            return -1;
        }
        if (amount > g->reinforcements_pending) {
            set_error(err, err_size, "not that many reinforcements left");
            return -1;
        }
        g->territories[t].armies += amount;
        g->reinforcements_pending -= amount;
        event = sink_new(sink, EVENT_PLACE, player);
        if (event != NULL) {
            event->to = t;
            event->count = amount;
            event_text(event, "%s placed %d armies in %s",
                       seat_name(g, player), amount, territory_name(g, t));
        }
        return 0;
    }
    set_error(err, err_size, "armies cannot be placed now");
    return -1;
}

static int apply_place_leader(struct Game *g, int player,
                              const struct Action *a, struct EventSink *sink,
                              char *err, size_t err_size)
{
    int t = a->territory;
    int i;
    int target;
    struct Event *event;

    if (g->pending_free_leader) {
        if (!terrain_valid(g, t) || g->territories[t].owner != player ||
            g->territories[t].armies < 1 ||
            g->territories[t].leader_of != 0) {
            set_error(err, err_size,
                      "choose your own territory without a leader");
            return -1;
        }
        g->territories[t].leader_of = player + 1;
        g->seats[player].leaders_in_play++;
        g->pending_free_leader = 0;
        event = sink_new(sink, EVENT_PLACE_LEADER, player);
        if (event != NULL) {
            event->to = t;
            event_text(event, "%s appointed a second Leader in %s",
                       seat_name(g, player), territory_name(g, t));
        }
        return 0;
    }
    if (g->phase != PHASE_SETUP_LEADERS) {
        set_error(err, err_size, "not in the leader placement phase");
        return -1;
    }
    if (!terrain_valid(g, t) || g->territories[t].owner != player ||
        g->territories[t].armies < 1) {
        set_error(err, err_size, "place leaders in your own territory");
        return -1;
    }
    if (g->territories[t].leader_of != 0) {
        set_error(err, err_size, "that territory already has a leader");
        return -1;
    }
    target = leaders_target(g, player);
    if (g->seats[player].leaders_in_play >= target) {
        set_error(err, err_size, "you have placed all your leaders");
        return -1;
    }
    g->territories[t].leader_of = player + 1;
    g->seats[player].leaders_in_play++;
    event = sink_new(sink, EVENT_PLACE_LEADER, player);
    if (event != NULL) {
        event->to = t;
        event_text(event, "%s placed a leader in %s", seat_name(g, player),
                   territory_name(g, t));
    }
    for (i = 0; i < g->player_count; i++) {
        int seat = (g->current_player + 1 + i) % g->player_count;
        if (g->seats[seat].leaders_in_play < leaders_target(g, seat)) {
            g->current_player = seat;
            return 0;
        }
    }
    {
        struct Event turn_event;
        begin_turn_state(g, 0, sink, &turn_event);
        event = sink_new(sink, EVENT_SETUP_DONE, -1);
        event_text(event, "Leader placement complete");
        if (sink != NULL && sink->count < GAME_MAX_EVENTS_PER_ACTION) {
            event = &sink->events[sink->count++];
            *event = turn_event;
        }
    }
    return 0;
}

static int apply_trade(struct Game *g, int player, const struct Action *a,
                       struct EventSink *sink, char *err, size_t err_size)
{
    int cards[3];
    int bonus = 0;
    int new_hand[GAME_MAX_CARDS];
    int new_count = 0;
    int i;
    int extra = 0;
    struct Event *event;

    if (g->phase != PHASE_REINFORCE && !g->must_trade) {
        set_error(err, err_size, "cards can only be traded in the reinforce phase");
        return -1;
    }
    if (g->must_trade && g->phase != PHASE_REINFORCE &&
        g->phase != PHASE_ATTACK && g->phase != PHASE_CONQUEST_MOVE &&
        g->phase != PHASE_FORTIFY) {
        set_error(err, err_size, "cards cannot be traded right now");
        return -1;
    }
    cards[0] = a->card_a;
    cards[1] = a->card_b;
    cards[2] = a->card_c;
    for (i = 0; i < 3; i++) {
        if (!card_valid(g, cards[i]) ||
            !seat_hand_has(g, player, cards[i])) {
            set_error(err, err_size, "card is not in your hand");
            return -1;
        }
    }
    if (!valid_set(g, cards[0], cards[1], cards[2], &bonus)) {
        set_error(err, err_size, "those three cards are not a set");
        return -1;
    }
    for (i = 0; i < g->seats[player].hand_count; i++) {
        int card = g->seats[player].hand[i];
        if (card != cards[0] && card != cards[1] && card != cards[2]) {
            new_hand[new_count++] = card;
        }
    }
    if (g->discard_count + 3 > GAME_MAX_CARDS) {
        set_error(err, err_size, "discard pile is full");
        return -1;
    }
    g->seats[player].hand_count = new_count;
    memcpy(g->seats[player].hand, new_hand,
           sizeof(int) * (size_t)new_count);
    for (i = 0; i < 3; i++) {
        g->discard[g->discard_count++] = cards[i];
    }
    for (i = 0; i < 3; i++) {
        int card = cards[i];
        int territory;
        if (g->map->territoryCards[card].wild) {
            continue;
        }
        territory = g->map->territoryCards[card].territory;
        if (terrain_valid(g, territory) &&
            g->territories[territory].owner == player) {
            g->territories[territory].armies += 2;
            extra += 2;
        }
    }
    if (g->phase == PHASE_REINFORCE) {
        g->reinforce_info.card_bonus += bonus;
        g->reinforce_info.total += bonus;
        g->reinforcements_pending += bonus;
    } else {
        g->deferred_card_bonus[player] += bonus;
    }
    g->cards_traded_this_turn = 1;
    if (g->seats[player].hand_count >= 5 && hand_has_set(g, player)) {
        g->must_trade = 1;
    } else {
        g->must_trade = 0;
    }
    event = sink_new(sink, EVENT_TRADE_CARDS, player);
    if (event != NULL) {
        event->card_a = cards[0];
        event->card_b = cards[1];
        event->card_c = cards[2];
        event->set_bonus = bonus;
        event->count = extra;
        event_text(event, "%s traded %s, %s and %s for %d armies%s",
                   seat_name(g, player), card_name(g, cards[0]),
                   card_name(g, cards[1]), card_name(g, cards[2]), bonus,
                   extra > 0 ? " plus 2 per owned territory" : "");
    }
    return 0;
}

static int apply_done_reinforce(struct Game *g, struct EventSink *sink,
                                char *err, size_t err_size)
{
    struct Event *event;

    if (g->phase != PHASE_REINFORCE) {
        set_error(err, err_size, "not in the reinforce phase");
        return -1;
    }
    if (g->must_trade) {
        set_error(err, err_size, "you must trade a card set first");
        return -1;
    }
    if (g->reinforcements_pending != 0) {
        set_error(err, err_size, "place all reinforcements first");
        return -1;
    }
    g->phase = PHASE_ATTACK;
    event = sink_new(sink, EVENT_PHASE_CHANGE, g->current_player);
    event_text(event, "%s enters the combat phase",
               seat_name(g, g->current_player));
    return 0;
}

static void sort_desc(int *values, int count)
{
    int i, j;
    for (i = 1; i < count; i++) {
        int value = values[i];
        for (j = i - 1; j >= 0 && values[j] < value; j--) {
            values[j + 1] = values[j];
        }
        values[j + 1] = value;
    }
}

static void resolve_battle_stats(struct Game *g, int attacker, int defender,
                                 int attacker_losses, int defender_losses)
{
    if (attacker >= 0 && attacker < g->player_count) {
        g->seats[attacker].stats.armies_lost += attacker_losses;
        g->seats[attacker].stats.armies_killed += defender_losses;
    }
    if (defender >= 0 && defender < g->player_count) {
        g->seats[defender].stats.armies_lost += defender_losses;
        g->seats[defender].stats.armies_killed += attacker_losses;
    }
}

static int modified_die(int value, int bonus, int cap)
{
    int result = value + bonus;
    if (cap > 0 && result > cap) {
        result = cap;
    }
    return result;
}

static void reroll_dice(struct DiceRng *rng, int *dice, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        dice[i] = dice_roll(rng);
    }
    sort_desc(dice, count);
}

static void apply_battle_modifiers(struct Game *g, int player, int defender,
                                   int from, int to, int round,
                                   struct DiceBattle *battle,
                                   struct EventSink *sink)
{
    int stronghold = g->map->territories[to].stronghold;
    int stronghold_counts = stronghold &&
        !(g->inv_from == from && g->inv_to == to &&
          (g->inv_flags & INV_NO_STRONGHOLD));
    int attacker_bonus = 0;
    int defender_bonus = 0;
    int amount = 0;

    if (round == 0) {
        if (take_reactive_card(g, player, "REROLL", stronghold,
                               battle->attacker_count, sink, &amount)) {
            reroll_dice(&g->rng, battle->attacker,
                        battle->attacker_count);
        }
        if (defender >= 0 &&
            take_reactive_card(g, defender, "REROLL", stronghold,
                               battle->attacker_count, sink, &amount)) {
            reroll_dice(&g->rng, battle->defender,
                        battle->defender_count);
        }
    }
    if (g->territories[from].leader_of == player + 1) {
        attacker_bonus++;
    }
    if (round == 0 && g->seats[player].attack_bonus > 0) {
        attacker_bonus += g->seats[player].attack_bonus;
        g->seats[player].attack_bonus = 0;
    }
    if (defender >= 0 &&
        g->territories[to].leader_of == defender + 1) {
        defender_bonus++;
    }
    if (stronghold_counts) {
        defender_bonus++;
    }
    if (g->inv_from == from && g->inv_to == to) {
        attacker_bonus += g->inv_attack_bonus;   /* Black Sails */
    }
    if (round == 0 && defender >= 0 &&
        take_reactive_card(g, defender, "DEFENSE_BONUS", stronghold,
                           battle->attacker_count, sink, &amount)) {
        defender_bonus += amount;
    }
    battle->attacker_bonus = attacker_bonus;
    battle->defender_bonus = defender_bonus;
}

static int apply_attack(struct Game *g, int player, const struct Action *a,
                        struct EventSink *sink, char *err, size_t err_size)
{
    int from = a->from;
    int to = a->to;
    int attacker_dice = a->dice;
    int rounds = 0;
    int defender_seat;
    int old_owner;
    struct Event event;

    if (g->phase != PHASE_ATTACK) {
        set_error(err, err_size, "not in the attack phase");
        return -1;
    }
    if (!terrain_valid(g, from) || !terrain_valid(g, to)) {
        set_error(err, err_size, "invalid territory");
        return -1;
    }
    if (g->territories[from].owner != player) {
        set_error(err, err_size, "you do not own the attacking territory");
        return -1;
    }
    if (g->territories[to].owner == player) {
        set_error(err, err_size, "you cannot attack your own territory");
        return -1;
    }
    if (!game_attack_route(g, player, from, to)) {
        set_error(err, err_size, g->frozen_by[from] || g->frozen_by[to]
                  ? "Winter Storms: no invasions into or out of that "
                    "territory"
                  : bridge_blocked(g, from, to)
                  ? "the bridge is blocked; only a Leader can lead "
                    "battalions across"
                  : "territories are not adjacent");
        return -1;
    }
    if (g->territories[from].armies < 2) {
        set_error(err, err_size, "you need at least two armies to attack");
        return -1;
    }
    if (attacker_dice < 1 || attacker_dice > 3 ||
        attacker_dice > g->territories[from].armies - 1) {
        set_error(err, err_size, "invalid number of attacking dice");
        return -1;
    }
    if (g->territories[to].armies < 1) {
        set_error(err, err_size, "the target has no defenders");
        return -1;
    }
    if (a->repeat &&
        (a->stop_at < 1 || a->stop_at >= g->territories[from].armies)) {
        set_error(err, err_size, "invalid blitz stop count");
        return -1;
    }

    defender_seat = g->territories[to].owner;
    old_owner = defender_seat;
    declare_invasion(g, from, to);
    if (!(g->inv_flags & INV_STARTED)) {
        int seaborne = is_seaborne(g, from, to);
        int stronghold = g->map->territories[to].stronghold;
        int amount = 0;
        struct Event *note;

        g->inv_flags |= INV_STARTED;
        if (seaborne && g->toll_by != 0) {
            /* Corsairs of Umbar: lose 3 before invading (1 remains). */
            lose_armies(g, from, g->toll_loss);
            note = sink_new(sink, EVENT_ADVENTURE, player);
            if (note != NULL) {
                note->from = from;
                note->to = to;
                event_text(note, "Corsairs of Umbar: the seaborne invasion "
                           "from %s lost %d battalions", territory_name(g, from),
                           g->toll_loss);
            }
            if (g->territories[from].armies < 2) {
                reset_invasion(g);
                return 0;
            }
        }
        if (defender_seat >= 0 && g->seats[defender_seat].alive) {
            if (take_reactive_card(g, defender_seat, "INVADED_REINFORCE",
                                   stronghold, attacker_dice, sink,
                                   &amount)) {
                g->territories[to].armies += amount;
            }
            if (g->territories[to].armies > 1) {
                int best = -1;
                int i;
                for (i = 0; i < g->map->territories[to].adjacentCount; i++) {
                    int n = g->map->territories[to].adjacent[i];
                    if (g->territories[n].owner == defender_seat &&
                        (best < 0 || g->territories[n].armies >
                         g->territories[best].armies)) {
                        best = n;
                    }
                }
                if (best >= 0 &&
                    take_reactive_card(g, defender_seat, "WITHDRAW",
                                       stronghold, attacker_dice, sink,
                                       &amount)) {
                    int moved = g->territories[to].armies - 1;
                    g->territories[to].armies = 1;
                    g->territories[best].armies += moved;
                    note = sink_new(sink, EVENT_ADVENTURE, defender_seat);
                    if (note != NULL) {
                        note->from = to;
                        note->to = best;
                        note->count = moved;
                        event_text(note, "Strategic Withdrawal: %s moved %d "
                                   "battalions from %s to %s",
                                   seat_name(g, defender_seat), moved,
                                   territory_name(g, to),
                                   territory_name(g, best));
                    }
                }
            }
        }
        if (attacker_dice > g->territories[from].armies - 1) {
            attacker_dice = g->territories[from].armies - 1;
        }
    }

    memset(&event, 0, sizeof(event));
    event.kind = EVENT_ATTACK;
    event.player = player;
    event.from = from;
    event.to = to;

    while (rounds < GAME_MAX_BLITZ_ROUNDS) {
        struct DiceBattle battle;
        int a_count = attacker_dice;
        int d_count;
        int pairs;
        int i;
        int attacker_losses = 0;
        int defender_losses = 0;

        if (a_count > g->territories[from].armies - 1) {
            a_count = g->territories[from].armies - 1;
        }
        if (a_count < 1) {
            break;
        }
        if (g->territories[to].armies >= 2) {
            d_count = 2;
        } else {
            d_count = 1;
        }
        if (d_count > g->territories[to].armies) {
            d_count = g->territories[to].armies;
        }
        if (d_count < 1) {
            break;
        }
        memset(&battle, 0, sizeof(battle));
        battle.attacker_count = a_count;
        battle.defender_count = d_count;
        for (i = 0; i < a_count; i++) {
            battle.attacker[i] = dice_roll(&g->rng);
        }
        for (i = 0; i < d_count; i++) {
            battle.defender[i] = dice_roll(&g->rng);
        }
        sort_desc(battle.attacker, a_count);
        sort_desc(battle.defender, d_count);
        apply_battle_modifiers(g, player, defender_seat, from, to, rounds,
                               &battle, sink);
        pairs = min_int(a_count, d_count);
        if (!(g->inv_flags & INV_STORM_DONE) && defender_seat >= 0 &&
            g->seats[defender_seat].alive && is_seaborne(g, from, to) &&
            g->map->territories[to].port) {
            int unused = 0;
            if (take_reactive_card(g, defender_seat, "STORM_REPEL",
                                   g->map->territories[to].stronghold,
                                   a_count, sink, &unused)) {
                /* Violent Storms: all attacking battalions in this battle
                 * are destroyed. */
                g->inv_flags |= INV_STORM_DONE;
                pairs = 0;
                attacker_losses = a_count;
            }
        }
        for (i = 0; i < pairs; i++) {
            int a_value = battle.attacker[i] +
                (i == 0 ? battle.attacker_bonus : 0);
            int d_value = battle.defender[i] +
                (i == 0 ? battle.defender_bonus : 0);
            a_value = modified_die(a_value, 0, g->map->rules.dieCap);
            d_value = modified_die(d_value, 0, g->map->rules.dieCap);
            if (a_value > d_value) {
                defender_losses++;
            } else {
                attacker_losses++;
            }
        }
        battle.attacker_losses = attacker_losses;
        battle.defender_losses = defender_losses;
        event.battles[rounds++] = battle;
        g->territories[from].armies -= attacker_losses;
        g->territories[to].armies -= defender_losses;
        resolve_battle_stats(g, player, defender_seat, attacker_losses,
                             defender_losses);
        if (g->territories[to].armies == 0) {
            break;
        }
        if (!a->repeat || g->territories[from].armies <= a->stop_at) {
            break;
        }
    }
    if (rounds == 0) {
        set_error(err, err_size, "the attack could not be resolved");
        return -1;
    }
    if (rounds == GAME_MAX_BLITZ_ROUNDS &&
        g->territories[to].armies > 0 && a->repeat) {
        event.truncated = 1;
    }
    event.battle_count = rounds;
    event_text(&event, "%s attacked %s from %s with %d dice (%d rounds)",
               seat_name(g, player), territory_name(g, to),
               territory_name(g, from), attacker_dice, rounds);
    {
        struct Event *out = sink_new(sink, EVENT_ATTACK, player);
        if (out != NULL) {
            *out = event;
        }
    }

    if (g->territories[to].armies == 0) {
        int defender_leader = g->territories[to].leader_of;
        g->territories[to].owner = player;
        g->territories[to].neutral = 0;
        g->territories[to].armies = 0;
        g->territories[to].leader_of = 0;
        if (defender_leader > 0 && defender_leader - 1 < g->player_count) {
            int seat = defender_leader - 1;
            if (g->seats[seat].leaders_in_play > 0) {
                g->seats[seat].leaders_in_play--;
            }
        }
        g->seats[player].stats.territories_taken++;
        g->conquered_this_turn = 1;
        g->territories_conquered_this_turn++;
        check_missions(g, player, MISSION_TRIGGER_CONQUER, sink);
        g->pending_from = from;
        g->pending_to = to;
        g->pending_min = event.battles[rounds - 1].attacker_count;
        g->pending_max = g->territories[from].armies - 1;
        if (g->pending_max < 1) {
            g->pending_max = 1;
        }
        if (g->pending_min > g->pending_max) {
            g->pending_min = g->pending_max;
        }
        g->phase = PHASE_CONQUEST_MOVE;
        {
            struct Event *conquest = sink_new(sink, EVENT_CONQUEST, player);
            if (conquest != NULL) {
                conquest->from = from;
                conquest->to = to;
                event_text(conquest, "%s conquered %s", seat_name(g, player),
                           territory_name(g, to));
            }
        }
        if (old_owner >= 0 && g->seats[old_owner].alive &&
            game_count_territories(g, old_owner) == 0) {
            eliminate_player(g, old_owner, player, sink);
        }
    }
    return 0;
}

static int apply_move_in(struct Game *g, int player, const struct Action *a,
                         struct EventSink *sink, char *err, size_t err_size)
{
    int from = a->from;
    int to = a->to;
    int amount = a->count;
    int leader_from = 0;
    struct Event *event;

    if (g->phase != PHASE_CONQUEST_MOVE) {
        set_error(err, err_size, "there is no conquest to resolve");
        return -1;
    }
    if (from != g->pending_from || to != g->pending_to) {
        set_error(err, err_size, "this is not the pending conquest");
        return -1;
    }
    if (amount < g->pending_min || amount > g->pending_max) {
        set_error(err, err_size, "move-in count is out of range");
        return -1;
    }
    if (!terrain_valid(g, from) || !terrain_valid(g, to) ||
        g->territories[from].owner != player ||
        g->territories[to].owner != player ||
        g->territories[to].armies < 0) {
        set_error(err, err_size, "invalid conquest state");
        return -1;
    }
    if (g->territories[from].armies - amount < 1) {
        set_error(err, err_size, "at least one army must stay behind");
        return -1;
    }
    if (a->leader) {
        if (g->territories[from].leader_of != player + 1) {
            set_error(err, err_size, "there is no leader to move");
            return -1;
        }
        if (g->territories[to].leader_of != 0) {
            set_error(err, err_size, "the destination already has a leader");
            return -1;
        }
        leader_from = 1;
    }
    g->territories[from].armies -= amount;
    g->territories[to].armies += amount;
    if (leader_from) {
        g->territories[from].leader_of = 0;
        g->territories[to].leader_of = player + 1;
        check_missions(g, player, MISSION_TRIGGER_LEADER, sink);
        if (g->map->rules.drawOnLeaderConquest) {
            if (g->map->territories[to].site) {
                g->leader_conquered_site = 1;
            }
        } else {
            grant_site_draw(g, player, to, sink);
        }
    }
    reset_invasion(g);
    event = sink_new(sink, EVENT_MOVE_IN, player);
    if (event != NULL) {
        event->from = from;
        event->to = to;
        event->count = amount;
        event_text(event, "%s moved %d armies into %s%s",
                   seat_name(g, player), amount, territory_name(g, to),
                   leader_from ? " with a leader" : "");
    }
    g->pending_from = -1;
    g->pending_to = -1;
    g->pending_min = 0;
    g->pending_max = 0;
    g->phase = PHASE_ATTACK;
    return 0;
}

static int apply_done_attack(struct Game *g, struct EventSink *sink,
                             char *err, size_t err_size)
{
    struct Event *event;

    if (g->phase != PHASE_ATTACK) {
        set_error(err, err_size, "not in the attack phase");
        return -1;
    }
    g->phase = PHASE_FORTIFY;
    g->fortified_this_turn = 0;
    reset_invasion(g);
    event = sink_new(sink, EVENT_PHASE_CHANGE, g->current_player);
    event_text(event, "%s enters the fortify phase",
               seat_name(g, g->current_player));
    return 0;
}

static int fortify_path(const struct Game *g, int player, int from, int to,
                        int with_leader);

int game_can_fortify(const struct Game *g, int player, int from, int to)
{
    if (g == NULL || !terrain_valid(g, from)) {
        return 0;
    }
    return fortify_path(g, player, from, to,
                        g->territories[from].leader_of == player + 1);
}

static int fortify_path(const struct Game *g, int player, int from, int to,
                        int with_leader)
{
    int queue[GAME_MAX_TERRITORIES];
    int seen[GAME_MAX_TERRITORIES];
    int head = 0, tail = 0;

    if (g == NULL || !terrain_valid(g, from) || !terrain_valid(g, to) ||
        from == to) {
        return 0;
    }
    if (g->territories[from].owner != player ||
        g->territories[to].owner != player ||
        g->territories[from].armies < 2) {
        return 0;
    }
    memset(seen, 0, sizeof(seen));
    seen[from] = 1;
    queue[tail++] = from;
    while (head < tail) {
        int current = queue[head++];
        int neighbor;
        if (current == to) {
            return 1;
        }
        for (neighbor = 0; neighbor < g->territory_count; neighbor++) {
            if (!seen[neighbor] &&
                game_territories_connected(g, current, neighbor) &&
                g->territories[neighbor].owner == player &&
                (with_leader || !bridge_blocked(g, current, neighbor))) {
                seen[neighbor] = 1;
                queue[tail++] = neighbor;
            }
        }
    }
    return seen[to];
}

static int apply_fortify(struct Game *g, int player, const struct Action *a,
                         struct EventSink *sink, char *err, size_t err_size)
{
    int from = a->from;
    int to = a->to;
    int amount = a->count;
    int leader_from = 0;
    struct Event *event;

    if (g->phase != PHASE_FORTIFY) {
        set_error(err, err_size, "not in the fortify phase");
        return -1;
    }
    if (g->fortified_this_turn && g->extra_fortifies <= 0) {
        set_error(err, err_size, "you have already fortified this turn");
        return -1;
    }
    if (!terrain_valid(g, from) || !terrain_valid(g, to) || from == to) {
        set_error(err, err_size, "invalid fortify move");
        return -1;
    }
    if (g->territories[from].owner != player ||
        g->territories[to].owner != player) {
        set_error(err, err_size, "you must own both territories");
        return -1;
    }
    if (amount < 1 || g->territories[from].armies - amount < 1) {
        set_error(err, err_size, "invalid number of armies");
        return -1;
    }
    if (!fortify_path(g, player, from, to, a->leader)) {
        set_error(err, err_size, !a->leader &&
                  g->territories[from].leader_of == player + 1 &&
                  fortify_path(g, player, from, to, 1)
                  ? "a blocked bridge is on the way: bring the Leader along"
                  : "no path of your own territories connects them");
        return -1;
    }
    if (a->leader) {
        if (g->territories[from].leader_of != player + 1) {
            set_error(err, err_size, "there is no leader to move");
            return -1;
        }
        if (g->territories[to].leader_of != 0) {
            set_error(err, err_size, "the destination already has a leader");
            return -1;
        }
        leader_from = 1;
    }
    g->territories[from].armies -= amount;
    g->territories[to].armies += amount;
    if (leader_from) {
        g->territories[from].leader_of = 0;
        g->territories[to].leader_of = player + 1;
        check_missions(g, player, MISSION_TRIGGER_LEADER, sink);
        if (!g->map->rules.drawOnLeaderConquest) {
            grant_site_draw(g, player, to, sink);
        }
    }
    if (g->fortified_this_turn) {
        if (g->extra_fortifies > 0) {
            g->extra_fortifies--;
        }
    } else {
        g->fortified_this_turn = 1;
    }
    event = sink_new(sink, EVENT_FORTIFY, player);
    if (event != NULL) {
        event->from = from;
        event->to = to;
        event->count = amount;
        event_text(event, "%s fortified %s with %d armies from %s%s",
                   seat_name(g, player), territory_name(g, to), amount,
                   territory_name(g, from),
                   leader_from ? " and a leader" : "");
    }
    return 0;
}

static void discard_hand(struct Game *g, int player)
{
    int i;

    for (i = 0; i < g->seats[player].hand_count; i++) {
        if (g->discard_count < GAME_MAX_CARDS) {
            g->discard[g->discard_count++] = g->seats[player].hand[i];
        }
    }
    g->seats[player].hand_count = 0;
    for (i = 0; i < g->seats[player].adventure_hand_count; i++) {
        if (g->adventure_discard_count < GAME_MAX_CARDS) {
            g->adventure_discard[g->adventure_discard_count++] =
                g->seats[player].adventure_hand[i];
        }
    }
    g->seats[player].adventure_hand_count = 0;
    discard_territory_card(g, g->seats[player].uprising_card);
    g->seats[player].uprising_card = -1;
    g->seats[player].oathbreakers = 0;
}

static int apply_end_turn(struct Game *g, struct EventSink *sink,
                          char *err, size_t err_size)
{
    int next;
    int drawn = -1;
    struct Event *event;
    struct Event turn_event;

    if (g->phase != PHASE_FORTIFY) {
        set_error(err, err_size, "not in the fortify phase");
        return -1;
    }
    if (g->must_trade) {
        set_error(err, err_size, "you must trade a card set first");
        return -1;
    }
    if (g->reinforcements_pending != 0) {
        set_error(err, err_size, "place all reinforcements first");
        return -1;
    }
    if (g->must_discard_adventure) {
        set_error(err, err_size, "discard down to four adventure cards first");
        return -1;
    }
    if (g->pending_free_leader) {
        set_error(err, err_size, "place your free Leader first");
        return -1;
    }
    if (g->bonus_pending > 0) {
        set_error(err, err_size, "place your mission battalions first");
        return -1;
    }
    {
        int i;
        if (g->map->rules.drawOnLeaderConquest) {
            if (g->leader_conquered_site) {
                g->leader_conquered_site = 0;
                g->draw_pending = 1;
            }
            if (run_rulebook_draws(g, g->current_player, sink)) {
                event = sink_new(sink, EVENT_END_TURN, g->current_player);
                if (event != NULL) {
                    event_text(event, "%s must place a Leader before ending "
                               "the turn", seat_name(g, g->current_player));
                }
                return 0;
            }
        } else {
            for (i = 0; i < g->territory_count; i++) {
                if (g->territories[i].owner == g->current_player &&
                    g->territories[i].leader_of == g->current_player + 1 &&
                    g->map->territories[i].site) {
                    grant_site_draw(g, g->current_player, i, sink);
                }
            }
        }
        if (g->must_discard_adventure) {
            event = sink_new(sink, EVENT_END_TURN, g->current_player);
            if (event != NULL) {
                event_text(event, "%s must discard adventure cards before "
                           "ending the turn", seat_name(g,
                           g->current_player));
            }
            return 0;
        }
    }
    if (g->conquered_this_turn) {
        if (g->deck_count == 0 && g->discard_count > 0) {
            memcpy(g->deck, g->discard,
                   sizeof(int) * (size_t)g->discard_count);
            g->deck_count = g->discard_count;
            g->discard_count = 0;
            shuffle_array(&g->rng, g->deck, g->deck_count);
        }
        if (g->deck_count > 0 &&
            g->seats[g->current_player].hand_count < GAME_MAX_CARDS) {
            drawn = g->deck[--g->deck_count];
            g->seats[g->current_player].hand[
                g->seats[g->current_player].hand_count++] = drawn;
            event = sink_new(sink, EVENT_DRAW_CARD, g->current_player);
            if (event != NULL) {
                event->card_a = drawn;
                event_text(event, "%s drew %s",
                           seat_name(g, g->current_player),
                           card_name(g, drawn));
            }
        }
    }
    {
        /* Edoras, Harlond, Lothlórien: an extra Territory card at the end of
         * the turn, up to a maximum of 5 in hand. */
        struct Seat *seat = &g->seats[g->current_player];
        while (seat->extra_card_draws > 0) {
            int card;
            seat->extra_card_draws--;
            if (seat->hand_count >= 5) {
                continue;
            }
            card = draw_territory_card(g);
            if (card < 0) {
                continue;
            }
            seat->hand[seat->hand_count++] = card;
            event = sink_new(sink, EVENT_DRAW_CARD, g->current_player);
            if (event != NULL) {
                event->card_a = card;
                event_text(event, "%s drew an extra card (%s)",
                           seat_name(g, g->current_player),
                           card_name(g, card));
            }
        }
    }
    reset_invasion(g);
    g->phase = PHASE_DRAW;
    event = sink_new(sink, EVENT_END_TURN, g->current_player);
    if (event != NULL) {
        event_text(event, "%s ended turn %d",
                   seat_name(g, g->current_player), g->turn);
    }
    next = next_alive_player(g, g->current_player);
    if (next < 0) {
        finish_game(g, g->current_player, sink);
        return 0;
    }
    g->turn++;
    begin_turn_state(g, next, sink, &turn_event);
    if (sink != NULL && sink->count < GAME_MAX_EVENTS_PER_ACTION) {
        sink->events[sink->count++] = turn_event;
    }
    return 0;
}

static int remove_player(struct Game *g, int player, int kind,
                         struct EventSink *sink, char *err, size_t err_size,
                         const char *phase_error, const char *reason_fmt, ...)
{
    int i;
    int next;
    struct Event *event;
    struct Event turn_event;
    char reason[192];
    va_list ap;

    if (g->status == GAME_FINISHED) {
        set_error(err, err_size, "the game is already over");
        return -1;
    }
    if (player < 0 || player >= g->player_count ||
        !g->seats[player].alive) {
        set_error(err, err_size, "not a living player");
        return -1;
    }
    if (g->phase != PHASE_REINFORCE && g->phase != PHASE_ATTACK &&
        g->phase != PHASE_FORTIFY) {
        set_error(err, err_size, "%s", phase_error);
        return -1;
    }
    va_start(ap, reason_fmt);
    vsnprintf(reason, sizeof(reason), reason_fmt, ap);
    va_end(ap);
    for (i = 0; i < g->territory_count; i++) {
        if (g->territories[i].owner == player) {
            g->territories[i].owner = -1;
            g->territories[i].neutral = 1;
            g->territories[i].leader_of = 0;
        }
    }
    discard_hand(g, player);
    g->seats[player].leaders_in_play = 0;
    g->seats[player].alive = 0;
    clear_kick_vote_for(g, player);
    clear_closed_links_for(g, player);
    clear_printed_effects_for(g, player);
    event = sink_new(sink, kind, player);
    if (event != NULL) {
        event_text(event, "%s", reason);
    }
    if (count_alive(g) <= 1) {
        finish_game(g, first_alive_player(g), sink);
        return 0;
    }
    if (player == g->current_player) {
        next = next_alive_player(g, player);
        if (next < 0) {
            finish_game(g, -1, sink);
            return 0;
        }
        g->turn++;
        begin_turn_state(g, next, sink, &turn_event);
        if (sink != NULL && sink->count < GAME_MAX_EVENTS_PER_ACTION) {
            sink->events[sink->count++] = turn_event;
        }
    }
    return 0;
}

static int apply_resign(struct Game *g, int player, struct EventSink *sink,
                        char *err, size_t err_size)
{
    return remove_player(g, player, EVENT_RESIGN, sink, err, err_size,
                         "you cannot resign during setup",
                         "%s resigned; their lands became neutral",
                         seat_name(g, player));
}

static int apply_kick(struct Game *g, int player, struct EventSink *sink,
                      char *err, size_t err_size)
{
    return remove_player(g, player, EVENT_KICK, sink, err, err_size,
                         "a player cannot be kicked during setup",
                         "%s was removed by an inactivity vote",
                         seat_name(g, player));
}

static int apply_recruit_leader(struct Game *g, int player,
                                const struct Action *a,
                                struct EventSink *sink,
                                char *err, size_t err_size)
{
    int t = a->territory;
    int cost = g->map->rules.leaderCost;
    struct Event *event;

    if (g->phase != PHASE_REINFORCE) {
        set_error(err, err_size,
                  "leaders can be recruited during reinforcement");
        return -1;
    }
    if (g->must_trade) {
        set_error(err, err_size, "you must trade a card set first");
        return -1;
    }
    if (g->seats[player].leaders_in_play > 0) {
        set_error(err, err_size,
                  "a replacement leader is only available when you have none");
        return -1;
    }
    if (cost > g->reinforcements_pending) {
        set_error(err, err_size, "not enough reinforcements for a leader");
        return -1;
    }
    if (!terrain_valid(g, t) || g->territories[t].owner != player ||
        g->territories[t].leader_of != 0 ||
        g->territories[t].armies < 1) {
        set_error(err, err_size,
                  "choose your own territory without a leader");
        return -1;
    }
    g->reinforcements_pending -= cost;
    g->territories[t].leader_of = player + 1;
    g->seats[player].leaders_in_play++;
    event = sink_new(sink, EVENT_PLACE_LEADER, player);
    if (event != NULL) {
        event->to = t;
        event->count = cost;
        event_text(event, "%s recruited a leader in %s%s",
                   seat_name(g, player), territory_name(g, t),
                   cost > 0 ? " (armies spent)" : "");
    }
    return 0;
}

static int attack_pair_ok(const struct Game *g, int player, int from, int to,
                          char *err, size_t err_size)
{
    if (g->phase != PHASE_ATTACK) {
        set_error(err, err_size, "this card is played during your invasions");
        return 0;
    }
    if (!terrain_valid(g, from) || !terrain_valid(g, to) ||
        g->territories[from].owner != player ||
        g->territories[to].owner == player ||
        g->territories[from].armies < 2 || g->territories[to].armies < 1 ||
        !game_attack_route(g, player, from, to)) {
        set_error(err, err_size, "choose one of your possible invasions");
        return 0;
    }
    return 1;
}

static void log_power(int player, int card, int from, int to,
                      struct EventSink *sink, const char *fmt, ...)
{
    struct Event *event = sink_new(sink, EVENT_CARD_PLAYED, player);
    va_list ap;

    if (event == NULL) {
        return;
    }
    event->card_a = card;
    event->from = from;
    event->to = to;
    va_start(ap, fmt);
    vsnprintf(event->text, sizeof(event->text), fmt, ap);
    va_end(ap);
}

/* The printed power cards.  Returns 0 when played, -1 on a refused play and
 * 1 when the card is not one of them. */
static int play_printed_power(struct Game *g, int player,
                              const struct Action *a, int slot, int card,
                              const struct MapAdventureCard *mc,
                              struct EventSink *sink,
                              char *err, size_t err_size)
{
    int from = a->from;
    int to = a->to;
    int n;

    if (strcmp(mc->effect, "AMBUSH") == 0) {
        if (!attack_pair_ok(g, player, from, to, err, err_size)) {
            return -1;
        }
        n = adventure_arg_int(mc, "n", 3);
        g->territories[from].armies += n;
        declare_invasion(g, from, to);
        seat_remove_adventure(g, player, slot);
        log_power(player, card, from, to, sink, "%s played %s: +%d "
                  "battalions in %s", seat_name(g, player), mc->title, n,
                  territory_name(g, from));
        return 0;
    }
    if (strcmp(mc->effect, "SEABORNE_BONUS") == 0) {
        if (!attack_pair_ok(g, player, from, to, err, err_size)) {
            return -1;
        }
        if (!is_seaborne(g, from, to)) {
            set_error(err, err_size, "choose an invasion across a sea-line");
            return -1;
        }
        declare_invasion(g, from, to);
        g->inv_attack_bonus += adventure_arg_int(mc, "n", 1);
        seat_remove_adventure(g, player, slot);
        log_power(player, card, from, to, sink, "%s played %s on the "
                  "invasion of %s", seat_name(g, player), mc->title,
                  territory_name(g, to));
        return 0;
    }
    if (strcmp(mc->effect, "GRIMA") == 0) {
        if (!attack_pair_ok(g, player, from, to, err, err_size)) {
            return -1;
        }
        n = min_int(adventure_arg_int(mc, "n", 2),
                    g->territories[to].armies - 1);
        if (n < 1) {
            set_error(err, err_size, "only 1 defending battalion is left");
            return -1;
        }
        g->territories[to].armies -= n;
        g->territories[from].armies += n;
        declare_invasion(g, from, to);
        seat_remove_adventure(g, player, slot);
        log_power(player, card, from, to, sink, "%s played %s: %d "
                  "battalions of %s turned to %s", seat_name(g, player),
                  mc->title, n, territory_name(g, to),
                  territory_name(g, from));
        return 0;
    }
    if (strcmp(mc->effect, "NO_STRONGHOLD") == 0) {
        if (!attack_pair_ok(g, player, from, to, err, err_size)) {
            return -1;
        }
        if (!g->map->territories[to].stronghold) {
            set_error(err, err_size, "choose an invasion of a Stronghold");
            return -1;
        }
        declare_invasion(g, from, to);
        g->inv_flags |= INV_NO_STRONGHOLD;
        seat_remove_adventure(g, player, slot);
        log_power(player, card, from, to, sink, "%s played %s: %s loses "
                  "its Stronghold advantage", seat_name(g, player),
                  mc->title, territory_name(g, to));
        return 0;
    }
    if (strcmp(mc->effect, "MOUNTAIN_PASS") == 0) {
        if (g->phase != PHASE_ATTACK) {
            set_error(err, err_size,
                      "this card is played during your invasions");
            return -1;
        }
        if (!terrain_valid(g, from) || !terrain_valid(g, to) ||
            g->territories[from].owner != player ||
            g->territories[to].owner == player ||
            g->territories[from].armies < 2 ||
            g->territories[to].armies < 1 ||
            !map_same_range(g->map, from, to) ||
            g->frozen_by[from] != 0 || g->frozen_by[to] != 0) {
            set_error(err, err_size, "choose an enemy territory next to the "
                      "same mountain range");
            return -1;
        }
        declare_invasion(g, from, to);
        g->inv_flags |= INV_MOUNTAIN;
        seat_remove_adventure(g, player, slot);
        log_power(player, card, from, to, sink, "%s played %s: invading "
                  "%s from %s under the mountains", seat_name(g, player),
                  mc->title, territory_name(g, to), territory_name(g, from));
        return 0;
    }
    if (strcmp(mc->effect, "LEADER_MOVE") == 0) {
        if (g->phase != PHASE_REINFORCE) {
            set_error(err, err_size, "play this at the start of your turn");
            return -1;
        }
        if (!terrain_valid(g, from) || !terrain_valid(g, to) || from == to ||
            g->territories[from].leader_of != player + 1 ||
            g->territories[to].owner != player ||
            g->territories[to].leader_of != 0) {
            set_error(err, err_size, "move one of your Leaders to your own "
                      "territory without a Leader");
            return -1;
        }
        g->territories[from].leader_of = 0;
        g->territories[to].leader_of = player + 1;
        seat_remove_adventure(g, player, slot);
        log_power(player, card, from, to, sink, "%s played %s: a Leader "
                  "moved from %s to %s", seat_name(g, player), mc->title,
                  territory_name(g, from), territory_name(g, to));
        return 0;
    }
    if (strcmp(mc->effect, "BRIDGE_BLOCK") == 0) {
        const struct MapBridge *bridge;
        if (g->phase != PHASE_REINFORCE) {
            set_error(err, err_size, "play this at the start of your turn");
            return -1;
        }
        if (a->bridge < 0 || a->bridge >= g->map->bridgeCount ||
            a->bridge >= MAP_MAX_BRIDGES) {
            set_error(err, err_size, "choose a bridge");
            return -1;
        }
        bridge = &g->map->bridges[a->bridge];
        g->bridge_blocked_by[a->bridge] = player + 1;
        seat_remove_adventure(g, player, slot);
        log_power(player, card, bridge->a, bridge->b, sink, "%s played "
                  "%s: the bridge between %s and %s is blocked until their "
                  "next turn", seat_name(g, player), mc->title,
                  territory_name(g, bridge->a), territory_name(g, bridge->b));
        return 0;
    }
    if (strcmp(mc->effect, "EXTRA_REDEPLOY") == 0) {
        if (g->phase != PHASE_FORTIFY) {
            set_error(err, err_size, "play this after your last invasion, "
                      "in the fortify phase");
            return -1;
        }
        n = adventure_arg_int(mc, "n", 3);
        g->extra_fortifies += n;
        seat_remove_adventure(g, player, slot);
        log_power(player, card, -1, -1, sink, "%s played %s: up to %d "
                  "extra redeployments", seat_name(g, player), mc->title, n);
        return 0;
    }
    return 1;
}

/* Site missions: "If, during your turn, one of your Leaders moves into (or is
 * already in) a Site of Power listed on one of your Mission cards, you may
 * declare that mission complete." */
static int mission_area(const struct Game *g,
                        const struct MapAdventureCard *mc,
                        int *area, int *area_id)
{
    char reward[24];

    adventure_arg_string(mc, "reward", reward, sizeof(reward));
    *area_id = -1;
    if (strcmp(reward, "region") == 0) {
        *area = BONUS_REGION;
        *area_id = adventure_arg_region(g, mc, "region");
        return *area_id >= 0;
    }
    if (strcmp(reward, "territory") == 0) {
        *area = BONUS_TERRITORY;
        *area_id = adventure_arg_territory(g, mc, "territory");
        return *area_id >= 0;
    }
    if (strcmp(reward, "port") == 0) {
        *area = BONUS_PORT;
        return 1;
    }
    if (strcmp(reward, "any") == 0) {
        *area = BONUS_ANY;
        return 1;
    }
    *area = BONUS_NONE;
    return strcmp(reward, "card") == 0 || strcmp(reward, "oathbreakers") == 0;
}

static int mission_claimable(const struct Game *g, int player, int slot)
{
    const struct MapAdventureCard *mc;
    int site, area, area_id, t;

    if (slot < 0 || slot >= g->seats[player].adventure_hand_count) {
        return 0;
    }
    mc = adventure_card_at(g, g->seats[player].adventure_hand[slot]);
    if (mc == NULL || strcmp(mc->effect, "SITE_MISSION") != 0 ||
        (g->phase != PHASE_REINFORCE && g->phase != PHASE_ATTACK &&
         g->phase != PHASE_FORTIFY)) {
        return 0;
    }
    site = adventure_arg_territory(g, mc, "site");
    if (site < 0 || g->territories[site].owner != player ||
        g->territories[site].leader_of != player + 1 ||
        !mission_area(g, mc, &area, &area_id)) {
        return 0;
    }
    if (area == BONUS_NONE) {
        return 1;
    }
    for (t = 0; t < g->territory_count; t++) {
        if (area_target_ok(g, player, t, area, area_id)) {
            return 1;   /* somewhere to place the battalions */
        }
    }
    return 0;
}

static int apply_claim_mission(struct Game *g, int player,
                               const struct Action *a,
                               struct EventSink *sink,
                               char *err, size_t err_size)
{
    int slot = a->card_a;
    int card, area, area_id, amount;
    const struct MapAdventureCard *mc;
    char reward[24];
    struct Event *event;

    if (!mission_claimable(g, player, slot)) {
        set_error(err, err_size, "bring one of your Leaders to that "
                  "mission's Site of Power first");
        return -1;
    }
    card = g->seats[player].adventure_hand[slot];
    mc = adventure_card_at(g, card);
    mission_area(g, mc, &area, &area_id);
    adventure_arg_string(mc, "reward", reward, sizeof(reward));
    amount = adventure_arg_int(mc, player_side(g, player) == MAP_SIDE_EVIL
                               ? "evil" : "good", 0);
    seat_remove_adventure(g, player, slot);
    g->seats[player].missions_done++;
    event = sink_new(sink, EVENT_MISSION_COMPLETE, player);
    if (event != NULL) {
        event->card_a = card;
        event->to = adventure_arg_territory(g, mc, "site");
        event->count = amount;
    }
    if (strcmp(reward, "card") == 0) {
        g->seats[player].extra_card_draws++;
        if (event != NULL) {
            event_text(event, "%s completed %s: an extra Territory card at "
                       "the end of the turn", seat_name(g, player),
                       mc->title);
        }
    } else if (strcmp(reward, "oathbreakers") == 0) {
        g->seats[player].oathbreakers += amount;
        if (event != NULL) {
            event_text(event, "%s completed %s: %d Oathbreakers wait to "
                       "join an invasion", seat_name(g, player), mc->title,
                       amount);
        }
    } else {
        g->bonus_pending = amount;
        g->bonus_area = amount > 0 ? area : BONUS_NONE;
        g->bonus_area_id = area_id;
        if (event != NULL) {
            event_text(event, "%s completed %s: %d battalions to place",
                       seat_name(g, player), mc->title, amount);
        }
    }
    return 0;
}

static int apply_oathbreakers(struct Game *g, int player,
                              const struct Action *a,
                              struct EventSink *sink,
                              char *err, size_t err_size)
{
    int t = a->territory;
    int j, can_invade = 0;
    struct Event *event;

    if (g->phase != PHASE_ATTACK || g->seats[player].oathbreakers <= 0) {
        set_error(err, err_size, "no Oathbreakers to send now");
        return -1;
    }
    if (!terrain_valid(g, t) || g->territories[t].owner != player) {
        set_error(err, err_size, "choose a territory you invade from");
        return -1;
    }
    for (j = 0; j < g->territory_count && !can_invade; j++) {
        can_invade = g->territories[j].owner != player &&
                     g->territories[j].armies > 0 &&
                     game_attack_route(g, player, t, j);
    }
    if (!can_invade) {
        set_error(err, err_size, "choose a territory you invade from");
        return -1;
    }
    g->territories[t].armies += g->seats[player].oathbreakers;
    event = sink_new(sink, EVENT_CARD_PLAYED, player);
    if (event != NULL) {
        event->to = t;
        event->count = g->seats[player].oathbreakers;
        event_text(event, "%s: %d Oathbreakers joined the invasion from %s",
                   seat_name(g, player), g->seats[player].oathbreakers,
                   territory_name(g, t));
    }
    g->seats[player].oathbreakers = 0;
    return 0;
}

static int apply_play_card(struct Game *g, int player,
                           const struct Action *a, struct EventSink *sink,
                           char *err, size_t err_size)
{
    int slot = a->card_a;
    const struct MapAdventureCard *mc;
    int played_card;
    int amount;
    int t;
    struct Event *event;

    if (slot < 0 || slot >= g->seats[player].adventure_hand_count) {
        set_error(err, err_size, "invalid adventure card");
        return -1;
    }
    played_card = g->seats[player].adventure_hand[slot];
    mc = adventure_card_at(g, played_card);
    if (mc == NULL || strcmp(mc->kind, "power") != 0) {
        set_error(err, err_size, "that card cannot be played");
        return -1;
    }
    if (strcmp(mc->effect, "ATTACK_BONUS") == 0) {
        if (g->phase != PHASE_ATTACK) {
            set_error(err, err_size,
                      "attack bonuses are played during the attack phase");
            return -1;
        }
        amount = adventure_arg_int(mc, "n", 1);
        g->seats[player].attack_bonus += amount;
        seat_remove_adventure(g, player, slot);
        event = sink_new(sink, EVENT_CARD_PLAYED, player);
        if (event != NULL) {
            event->card_a = played_card;
            event_text(event, "%s prepared +%d on the next attack",
                       seat_name(g, player), amount);
        }
        return 0;
    }
    if (strcmp(mc->effect, "EXTRA_REINFORCE") == 0) {
        if (g->phase != PHASE_REINFORCE) {
            set_error(err, err_size,
                      "extra reinforcements are played during reinforce");
            return -1;
        }
        amount = adventure_arg_int(mc, "n", 1);
        g->reinforcements_pending += amount;
        g->reinforce_info.card_bonus += amount;
        g->reinforce_info.total += amount;
        seat_remove_adventure(g, player, slot);
        event = sink_new(sink, EVENT_CARD_PLAYED, player);
        if (event != NULL) {
            event_text(event, "%s gained %d extra reinforcements",
                       seat_name(g, player), amount);
        }
        return 0;
    }
    if (strcmp(mc->effect, "FREE_FORTIFY") == 0) {
        if (g->phase != PHASE_ATTACK && g->phase != PHASE_FORTIFY) {
            set_error(err, err_size,
                      "free fortify is played during attack or fortify");
            return -1;
        }
        g->extra_fortifies++;
        seat_remove_adventure(g, player, slot);
        event = sink_new(sink, EVENT_CARD_PLAYED, player);
        if (event != NULL) {
            event_text(event, "%s may make another fortify move",
                       seat_name(g, player));
        }
        return 0;
    }
    if (strcmp(mc->effect, "RECRUIT_LEADER") == 0) {
        if (g->phase != PHASE_REINFORCE) {
            set_error(err, err_size,
                      "leaders are placed during the reinforce phase");
            return -1;
        }
        t = a->territory;
        if (!terrain_valid(g, t) || g->territories[t].owner != player ||
            g->territories[t].leader_of != 0 ||
            g->territories[t].armies < 1) {
            set_error(err, err_size,
                      "choose your own territory without a leader");
            return -1;
        }
        g->territories[t].leader_of = player + 1;
        g->seats[player].leaders_in_play++;
        seat_remove_adventure(g, player, slot);
        event = sink_new(sink, EVENT_CARD_PLAYED, player);
        if (event != NULL) {
            event->to = t;
            event_text(event, "%s placed a leader in %s for free",
                       seat_name(g, player), territory_name(g, t));
        }
        return 0;
    }
    {
        int rc = play_printed_power(g, player, a, slot, played_card, mc,
                                    sink, err, err_size);
        if (rc != 1) {
            return rc;
        }
    }
    if (strcmp(mc->effect, "DEFENSE_BONUS") == 0 ||
        strcmp(mc->effect, "REROLL") == 0 ||
        strcmp(mc->effect, "CANCEL_EVENT") == 0 ||
        strcmp(mc->effect, "INVADED_REINFORCE") == 0 ||
        strcmp(mc->effect, "WITHDRAW") == 0 ||
        strcmp(mc->effect, "STORM_REPEL") == 0) {
        set_error(err, err_size,
                  "that reactive card uses a standing order");
        return -1;
    }
    set_error(err, err_size, "that power card cannot be played now");
    return -1;
}

static int apply_discard_adventure(struct Game *g, int player,
                                   const struct Action *a,
                                   struct EventSink *sink,
                                   char *err, size_t err_size)
{
    int slot = a->card_a;
    const struct MapAdventureCard *mc;
    int discarded_card;
    struct Event *event;

    if (!g->must_discard_adventure) {
        set_error(err, err_size, "no adventure discard is required");
        return -1;
    }
    if (slot < 0 || slot >= g->seats[player].adventure_hand_count) {
        set_error(err, err_size, "invalid adventure card");
        return -1;
    }
    discarded_card = g->seats[player].adventure_hand[slot];
    mc = adventure_card_at(g, discarded_card);
    seat_remove_adventure(g, player, slot);
    event = sink_new(sink, EVENT_ADVENTURE, player);
    if (event != NULL) {
        event->card_a = discarded_card;
        event_text(event, "%s discarded %s", seat_name(g, player),
                   mc != NULL ? mc->title : "an adventure card");
    }
    return 0;
}

int game_apply(struct Game *g, int player, const struct Action *action,
               struct Event *out, int *out_count,
               char *err, size_t err_size)
{
    struct EventSink sink;
    struct Game *snapshot;
    int rc = -1;
    int i;

    if (err != NULL && err_size > 0) {
        err[0] = '\0';
    }
    if (out_count != NULL) {
        *out_count = 0;
    }
    if (g == NULL || action == NULL || g->map == NULL) {
        set_error(err, err_size, "invalid game or action");
        return -1;
    }
    if (g->status != GAME_RUNNING || g->phase == PHASE_FINISHED) {
        set_error(err, err_size, "the game is not running");
        return -1;
    }
    if (player < 0 || player >= g->player_count) {
        set_error(err, err_size, "invalid player");
        return -1;
    }
    if (action->type < ACT_CLAIM || action->type > ACT_OATHBREAKERS) {
        set_error(err, err_size, "unknown action");
        return -1;
    }
    if (action->type == ACT_RESIGN || action->type == ACT_KICK) {
        /* Resign is player-initiated; kick is applied by the server when an
         * inactivity vote passes.  Both are only allowed in stable phases. */
    } else {
        if (player != g->current_player) {
            set_error(err, err_size, "it is not your turn");
            return -1;
        }
        if (!g->seats[player].alive) {
            set_error(err, err_size, "you are not alive");
            return -1;
        }
        if (g->must_trade && action->type != ACT_TRADE_CARDS &&
            action->type != ACT_DISCARD_ADVENTURE) {
            set_error(err, err_size, "you must trade a card set first");
            return -1;
        }
    }
    if (g->must_discard_adventure &&
        action->type != ACT_DISCARD_ADVENTURE &&
        action->type != ACT_RESIGN && action->type != ACT_KICK) {
        set_error(err, err_size,
                  "discard down to four adventure cards first");
        return -1;
    }
    if (g->pending_free_leader && action->type != ACT_PLACE_LEADER &&
        action->type != ACT_RESIGN && action->type != ACT_KICK) {
        set_error(err, err_size, "place your free Leader first");
        return -1;
    }
    if (g->bonus_pending > 0 && action->type != ACT_PLACE &&
        action->type != ACT_RESIGN && action->type != ACT_KICK) {
        set_error(err, err_size, "place your mission battalions first");
        return -1;
    }
    /* A rejected action must leave the game untouched; the printed cards
     * chain several changes, so restore a snapshot on failure. */
    snapshot = (struct Game *)malloc(sizeof(*snapshot));
    if (snapshot == NULL) {
        set_error(err, err_size, "out of memory");
        return -1;
    }
    memcpy(snapshot, g, sizeof(*snapshot));
    memset(&sink, 0, sizeof(sink));
    switch (action->type) {
    case ACT_CLAIM:
        rc = apply_claim(g, player, action, &sink, err, err_size);
        break;
    case ACT_PLACE:
        rc = apply_place(g, player, action, &sink, err, err_size);
        break;
    case ACT_PLACE_LEADER:
        rc = apply_place_leader(g, player, action, &sink, err, err_size);
        break;
    case ACT_TRADE_CARDS:
        rc = apply_trade(g, player, action, &sink, err, err_size);
        break;
    case ACT_DONE_REINFORCE:
        rc = apply_done_reinforce(g, &sink, err, err_size);
        break;
    case ACT_ATTACK:
        rc = apply_attack(g, player, action, &sink, err, err_size);
        break;
    case ACT_MOVE_IN:
        rc = apply_move_in(g, player, action, &sink, err, err_size);
        break;
    case ACT_DONE_ATTACK:
        rc = apply_done_attack(g, &sink, err, err_size);
        break;
    case ACT_FORTIFY:
        rc = apply_fortify(g, player, action, &sink, err, err_size);
        break;
    case ACT_END_TURN:
        rc = apply_end_turn(g, &sink, err, err_size);
        break;
    case ACT_RESIGN:
        rc = apply_resign(g, player, &sink, err, err_size);
        break;
    case ACT_KICK:
        rc = apply_kick(g, player, &sink, err, err_size);
        break;
    case ACT_RECRUIT_LEADER:
        rc = apply_recruit_leader(g, player, action, &sink, err, err_size);
        break;
    case ACT_PLAY_CARD:
        rc = apply_play_card(g, player, action, &sink, err, err_size);
        break;
    case ACT_DISCARD_ADVENTURE:
        rc = apply_discard_adventure(g, player, action, &sink, err, err_size);
        break;
    case ACT_CLAIM_MISSION:
        rc = apply_claim_mission(g, player, action, &sink, err, err_size);
        break;
    case ACT_OATHBREAKERS:
        rc = apply_oathbreakers(g, player, action, &sink, err, err_size);
        break;
    default:
        set_error(err, err_size, "unknown action");
        rc = -1;
        break;
    }
    if (rc != 0) {
        memcpy(g, snapshot, sizeof(*g));
        free(snapshot);
        return -1;
    }
    free(snapshot);
    if (g->status == GAME_RUNNING) {
        check_uprisings(g, &sink);
    }
    g->version++;
    g->updated = time(NULL);
    for (i = 0; i < sink.count; i++) {
        sink.events[i].version = g->version;
        log_append(g, &sink.events[i]);
    }
    if (out != NULL) {
        memcpy(out, sink.events,
               sizeof(struct Event) * (size_t)sink.count);
    }
    if (out_count != NULL) {
        *out_count = sink.count;
    }
    return 0;
}

static int has_recruit_spot(const struct Game *g, int player)
{
    int i;

    for (i = 0; i < g->territory_count; i++) {
        if (g->territories[i].owner == player &&
            g->territories[i].leader_of == 0 &&
            g->territories[i].armies >= 1) {
            return 1;
        }
    }
    return 0;
}

void game_legal_actions(const struct Game *g, struct LegalActions *out)
{
    int i, j;
    int current;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (g == NULL || g->map == NULL) {
        return;
    }
    out->phase = g->phase;
    out->current_player = g->current_player;
    if (g->status != GAME_RUNNING || g->phase == PHASE_FINISHED) {
        return;
    }
    current = g->current_player;
    if (current < 0 || current >= g->player_count ||
        !g->seats[current].alive) {
        return;
    }
    out->must_discard_adventure = g->must_discard_adventure;
    out->recruit_cost = g->map->rules.leaderCost;
    if (g->must_discard_adventure) {
        return;
    }
    if (g->pending_free_leader) {
        out->free_leader_pending = 1;
        for (i = 0; i < g->territory_count; i++) {
            out->leader_placeable[i] = g->territories[i].owner == current &&
                g->territories[i].armies >= 1 &&
                g->territories[i].leader_of == 0;
        }
        return;
    }
    if (g->bonus_pending > 0) {
        out->bonus_pending = g->bonus_pending;
        for (i = 0; i < g->territory_count; i++) {
            out->bonus_placeable[i] = bonus_target_ok(g, current, i);
        }
        return;
    }
    if (g->must_trade) {
        out->can_trade = hand_has_set(g, current);
        return;
    }
    for (i = 0; i < g->seats[current].adventure_hand_count; i++) {
        const struct MapAdventureCard *mc =
            adventure_card_at(g, g->seats[current].adventure_hand[i]);
        if (mc == NULL) {
            continue;
        }
        if (strcmp(mc->effect, "EXTRA_REINFORCE") == 0 &&
            g->phase == PHASE_REINFORCE) {
            out->playable_adventure[i] = 1;
        } else if (strcmp(mc->effect, "ATTACK_BONUS") == 0 &&
                   g->phase == PHASE_ATTACK) {
            out->playable_adventure[i] = 1;
        } else if (strcmp(mc->effect, "FREE_FORTIFY") == 0 &&
                   (g->phase == PHASE_ATTACK ||
                    g->phase == PHASE_FORTIFY)) {
            out->playable_adventure[i] = 1;
        } else if (strcmp(mc->effect, "RECRUIT_LEADER") == 0 &&
                   g->phase == PHASE_REINFORCE &&
                   has_recruit_spot(g, current)) {
            out->playable_adventure[i] = 1;
            out->has_recruit_card = 1;
        } else if ((strcmp(mc->effect, "AMBUSH") == 0 ||
                    strcmp(mc->effect, "GRIMA") == 0 ||
                    strcmp(mc->effect, "NO_STRONGHOLD") == 0 ||
                    strcmp(mc->effect, "SEABORNE_BONUS") == 0 ||
                    strcmp(mc->effect, "MOUNTAIN_PASS") == 0) &&
                   g->phase == PHASE_ATTACK) {
            out->playable_adventure[i] = 1;   /* the client picks the pair */
        } else if ((strcmp(mc->effect, "LEADER_MOVE") == 0 &&
                    g->seats[current].leaders_in_play > 0) ||
                   (strcmp(mc->effect, "BRIDGE_BLOCK") == 0 &&
                    g->map->bridgeCount > 0)) {
            out->playable_adventure[i] = g->phase == PHASE_REINFORCE;
        } else if (strcmp(mc->effect, "EXTRA_REDEPLOY") == 0) {
            out->playable_adventure[i] = g->phase == PHASE_FORTIFY;
        }
        out->claimable_mission[i] = mission_claimable(g, current, i);
    }
    switch (g->phase) {
    case PHASE_SETUP_CLAIM:
        out->setup_pool = g->setup_pool[current];
        for (i = 0; i < g->territory_count; i++) {
            if (g->territories[i].owner == -1 &&
                !g->territories[i].neutral &&
                g->territories[i].armies == 0) {
                out->claimable[i] = 1;
            }
        }
        break;
    case PHASE_SETUP_PLACE:
        out->setup_pool = g->setup_pool[current];
        out->setup_batch_remaining = g->setup_batch_remaining;
        for (i = 0; i < g->territory_count; i++) {
            if (g->territories[i].owner == current) {
                out->placeable[i] = 1;
            }
        }
        break;
    case PHASE_SETUP_LEADERS:
        out->leaders_remaining =
            leaders_target(g, current) -
            g->seats[current].leaders_in_play;
        for (i = 0; i < g->territory_count; i++) {
            if (g->territories[i].owner == current &&
                g->territories[i].armies >= 1 &&
                g->territories[i].leader_of == 0) {
                out->leader_placeable[i] = 1;
            }
        }
        break;
    case PHASE_REINFORCE:
        out->reinforcements_pending = g->reinforcements_pending;
        out->can_trade = hand_has_set(g, current);
        out->can_done_reinforce =
            (g->reinforcements_pending == 0 && !g->must_trade) ? 1 : 0;
        out->can_recruit_leader =
            (g->seats[current].leaders_in_play == 0 &&
             g->reinforcements_pending >= g->map->rules.leaderCost &&
             has_recruit_spot(g, current)) ? 1 : 0;
        for (i = 0; i < g->territory_count; i++) {
            if (g->territories[i].owner == current) {
                out->placeable[i] = 1;
            }
            if (g->territories[i].owner == current &&
                g->territories[i].leader_of == 0 &&
                g->territories[i].armies >= 1 &&
                (out->can_recruit_leader || out->has_recruit_card)) {
                out->recruit_placeable[i] = 1;
            }
        }
        break;
    case PHASE_ATTACK:
        out->can_done_attack = 1;
        out->can_oathbreakers = g->seats[current].oathbreakers > 0;
        for (i = 0; i < g->territory_count; i++) {
            if (g->territories[i].owner != current ||
                g->territories[i].armies < 2) {
                continue;
            }
            out->max_attack_dice[i] =
                min_int(3, g->territories[i].armies - 1);
            for (j = 0; j < g->territory_count; j++) {
                if (j == i || g->territories[j].owner == current) {
                    continue;
                }
                if (game_attack_route(g, current, i, j) &&
                    g->territories[j].armies > 0) {
                    out->attack[i][j] = 1;
                }
            }
        }
        break;
    case PHASE_CONQUEST_MOVE:
        out->move_in_min = g->pending_min;
        out->move_in_max = g->pending_max;
        break;
    case PHASE_FORTIFY:
        out->can_end_turn = 1;
        if (!g->fortified_this_turn || g->extra_fortifies > 0) {
            for (i = 0; i < g->territory_count; i++) {
                if (g->territories[i].owner != current ||
                    g->territories[i].armies < 2) {
                    continue;
                }
                for (j = 0; j < g->territory_count; j++) {
                    if (i == j || g->territories[j].owner != current) {
                        continue;
                    }
                    /* 1 = possible; 2 = only with the Leader going along
                     * (a bridge on the way is blocked). */
                    if (fortify_path(g, current, i, j, 0)) {
                        out->fortify[i][j] = 1;
                    } else if (g->territories[i].leader_of == current + 1 &&
                               g->territories[j].leader_of == 0 &&
                               fortify_path(g, current, i, j, 1)) {
                        out->fortify[i][j] = 2;
                    }
                }
            }
        }
        break;
    default:
        break;
    }
    out->can_resign = (g->phase == PHASE_REINFORCE ||
                       g->phase == PHASE_ATTACK ||
                       g->phase == PHASE_FORTIFY) ? 1 : 0;
}

int game_log_count(const struct Game *g)
{
    return g != NULL ? g->log.count : 0;
}

const struct Event *game_log_event(const struct Game *g, int index)
{
    int slot;

    if (g == NULL || index < 0 || index >= g->log.count) {
        return NULL;
    }
    slot = (g->log.next - g->log.count + index + GAME_LOG_RING) %
           GAME_LOG_RING;
    return &g->log.events[slot];
}

const char *game_phase_name(int phase)
{
    switch (phase) {
    case PHASE_LOBBY: return "lobby";
    case PHASE_SETUP_CLAIM: return "setup_claim";
    case PHASE_SETUP_PLACE: return "setup_place";
    case PHASE_SETUP_LEADERS: return "setup_leaders";
    case PHASE_REINFORCE: return "reinforce";
    case PHASE_ATTACK: return "attack";
    case PHASE_CONQUEST_MOVE: return "conquest_move";
    case PHASE_FORTIFY: return "fortify";
    case PHASE_DRAW: return "draw";
    case PHASE_FINISHED: return "finished";
    default: return "unknown";
    }
}

const char *game_status_name(int status)
{
    switch (status) {
    case GAME_OPEN: return "open";
    case GAME_RUNNING: return "running";
    case GAME_FINISHED: return "finished";
    default: return "unknown";
    }
}

const char *game_event_name(int kind)
{
    switch (kind) {
    case EVENT_NONE: return "none";
    case EVENT_START: return "start";
    case EVENT_CLAIM: return "claim";
    case EVENT_PLACE: return "place";
    case EVENT_PLACE_LEADER: return "place_leader";
    case EVENT_TRADE_CARDS: return "trade_cards";
    case EVENT_ATTACK: return "attack";
    case EVENT_CONQUEST: return "conquest";
    case EVENT_MOVE_IN: return "move_in";
    case EVENT_FORTIFY: return "fortify";
    case EVENT_DRAW_CARD: return "draw_card";
    case EVENT_END_TURN: return "end_turn";
    case EVENT_ELIMINATED: return "eliminated";
    case EVENT_GAME_OVER: return "game_over";
    case EVENT_RESIGN: return "resign";
    case EVENT_SETUP_DONE: return "setup_done";
    case EVENT_TURN_BEGIN: return "turn_begin";
    case EVENT_PHASE_CHANGE: return "phase_change";
    case EVENT_KICK: return "kick";
    case EVENT_ADVENTURE: return "adventure";
    case EVENT_CARD_PLAYED: return "card_played";
    case EVENT_MISSION_COMPLETE: return "mission";
    case EVENT_SEA_CLOSED: return "sea_closed";
    default: return "unknown";
    }
}

struct ViewWriter {
    char *out;
    size_t capacity;
    size_t used;
    int overflow;
};

static void view_char(struct ViewWriter *w, char c)
{
    if (w->used + 2 > w->capacity) {
        w->overflow = 1;
        return;
    }
    w->out[w->used++] = c;
}

static void view_raw(struct ViewWriter *w, const char *text)
{
    while (text != NULL && *text != '\0' && !w->overflow) {
        view_char(w, *text++);
    }
}

static void view_fmt(struct ViewWriter *w, const char *fmt, ...)
{
    char buffer[128];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    view_raw(w, buffer);
}

static void view_string(struct ViewWriter *w, const char *text)
{
    const unsigned char *p = (const unsigned char *)
                             (text != NULL ? text : "");
    static const char hex[] = "0123456789abcdef";

    view_char(w, '"');
    while (*p != '\0' && !w->overflow) {
        unsigned char c = *p++;
        switch (c) {
        case '"': view_raw(w, "\\\""); break;
        case '\\': view_raw(w, "\\\\"); break;
        case '\b': view_raw(w, "\\b"); break;
        case '\f': view_raw(w, "\\f"); break;
        case '\n': view_raw(w, "\\n"); break;
        case '\r': view_raw(w, "\\r"); break;
        case '\t': view_raw(w, "\\t"); break;
        default:
            if (c < 0x20) {
                char escape[7] = { '\\', 'u', '0', '0',
                                   hex[c >> 4], hex[c & 0xf], '\0' };
                view_raw(w, escape);
            } else {
                view_char(w, (char)c);
            }
            break;
        }
    }
    view_char(w, '"');
}

static void view_key(struct ViewWriter *w, int *first, const char *key)
{
    if (!*first) {
        view_char(w, ',');
    }
    *first = 0;
    view_string(w, key);
    view_char(w, ':');
}

static void view_sep(struct ViewWriter *w, int *first)
{
    if (!*first) {
        view_char(w, ',');
    }
    *first = 0;
}

static void view_bool(struct ViewWriter *w, int value)
{
    view_raw(w, value ? "true" : "false");
}

int game_view(const struct Game *g, int viewer_seat, char *out,
              size_t out_size)
{
    struct ViewWriter w;
    struct LegalActions legal;
    int first, pfirst, tfirst, ofirst, afirst, bfirst;
    int i, j, index;
    int show_legal;

    if (g == NULL || g->map == NULL || out == NULL || out_size == 0) {
        return -1;
    }
    memset(&w, 0, sizeof(w));
    w.out = out;
    w.capacity = out_size;
    if (viewer_seat < 0 || viewer_seat >= g->player_count) {
        viewer_seat = -1;
    }
    show_legal = (viewer_seat >= 0 &&
                  viewer_seat == g->current_player &&
                  g->status == GAME_RUNNING &&
                  g->seats[viewer_seat].alive);

    view_char(&w, '{');
    first = 1;
    view_key(&w, &first, "format");
    view_raw(&w, "1");
    view_key(&w, &first, "id");
    view_fmt(&w, "%d", g->id);
    view_key(&w, &first, "name");
    view_string(&w, g->name);
    view_key(&w, &first, "map");
    view_string(&w, g->map_pack);
    view_key(&w, &first, "status");
    view_string(&w, game_status_name(g->status));
    view_key(&w, &first, "phase");
    view_string(&w, game_phase_name(g->phase));
    view_key(&w, &first, "version");
    view_fmt(&w, "%d", g->version);
    view_key(&w, &first, "turn");
    view_fmt(&w, "%d", g->turn);
    view_key(&w, &first, "conqueredThisTurn");
    view_fmt(&w, "%d", g->territories_conquered_this_turn);
    view_key(&w, &first, "current");
    view_fmt(&w, "%d", g->current_player);
    view_key(&w, &first, "winner");
    view_fmt(&w, "%d", g->winner);
    view_key(&w, &first, "you");
    view_fmt(&w, "%d", viewer_seat);
    view_key(&w, &first, "playerCount");
    view_fmt(&w, "%d", g->player_count);
    view_key(&w, &first, "maxPlayers");
    view_fmt(&w, "%d", g->max_players);
    view_key(&w, &first, "host");
    view_string(&w, g->host);
    view_key(&w, &first, "hasPassword");
    view_bool(&w, g->password_hash[0] != '\0');
    view_key(&w, &first, "batchRemaining");
    view_fmt(&w, "%d", g->setup_batch_remaining);

    view_key(&w, &first, "players");
    view_raw(&w, "[");
    pfirst = 1;
    for (i = 0; i < g->player_count; i++) {
        view_sep(&w, &pfirst);
        view_raw(&w, "{");
        ofirst = 1;
        view_key(&w, &ofirst, "seat");
        view_fmt(&w, "%d", i);
        view_key(&w, &ofirst, "user");
        view_string(&w, g->seats[i].username);
        view_key(&w, &ofirst, "faction");
        view_fmt(&w, "%d", g->seats[i].faction);
        view_key(&w, &ofirst, "alive");
        view_bool(&w, g->seats[i].alive);
        view_key(&w, &ofirst, "handCount");
        view_fmt(&w, "%d", g->seats[i].hand_count);
        view_key(&w, &ofirst, "adventureCount");
        view_fmt(&w, "%d", g->seats[i].adventure_hand_count);
        view_key(&w, &ofirst, "leaders");
        view_fmt(&w, "%d", g->seats[i].leaders_in_play);
        view_key(&w, &ofirst, "missions");
        view_fmt(&w, "%d", g->seats[i].missions_done);
        view_key(&w, &ofirst, "lastSeen");
        view_fmt(&w, "%d", g->seats[i].last_seen_version);
        view_key(&w, &ofirst, "side");
        view_string(&w, player_side(g, i) == MAP_SIDE_EVIL ? "evil"
                    : player_side(g, i) == MAP_SIDE_GOOD ? "good" : "");
        view_key(&w, &ofirst, "oathbreakers");
        view_fmt(&w, "%d", g->seats[i].oathbreakers);
        view_key(&w, &ofirst, "uprising");
        view_bool(&w, g->seats[i].uprising_card >= 0);
        view_key(&w, &ofirst, "setupPool");
        view_fmt(&w, "%d", g->setup_pool[i]);
        view_key(&w, &ofirst, "stats");
        view_raw(&w, "{");
        bfirst = 1;
        view_key(&w, &bfirst, "gamesPlayed");
        view_fmt(&w, "%d", g->seats[i].stats.games_played);
        view_key(&w, &bfirst, "wins");
        view_fmt(&w, "%d", g->seats[i].stats.wins);
        view_key(&w, &bfirst, "losses");
        view_fmt(&w, "%d", g->seats[i].stats.losses);
        view_key(&w, &bfirst, "territoriesTaken");
        view_fmt(&w, "%d", g->seats[i].stats.territories_taken);
        view_key(&w, &bfirst, "armiesKilled");
        view_fmt(&w, "%d", g->seats[i].stats.armies_killed);
        view_key(&w, &bfirst, "armiesLost");
        view_fmt(&w, "%d", g->seats[i].stats.armies_lost);
        view_raw(&w, "}");
        view_raw(&w, "}");
    }
    view_raw(&w, "]");

    view_key(&w, &first, "territories");
    view_raw(&w, "[");
    tfirst = 1;
    for (i = 0; i < g->territory_count; i++) {
        view_sep(&w, &tfirst);
        view_raw(&w, "{");
        ofirst = 1;
        view_key(&w, &ofirst, "owner");
        view_fmt(&w, "%d", g->territories[i].owner);
        view_key(&w, &ofirst, "armies");
        view_fmt(&w, "%d", g->territories[i].armies);
        view_key(&w, &ofirst, "leader");
        view_fmt(&w, "%d", g->territories[i].leader_of);
        view_key(&w, &ofirst, "neutral");
        view_bool(&w, g->territories[i].neutral);
        view_raw(&w, "}");
    }
    view_raw(&w, "]");

    view_key(&w, &first, "deck");
    view_fmt(&w, "%d", g->deck_count);
    view_key(&w, &first, "discard");
    view_fmt(&w, "%d", g->discard_count);
    view_key(&w, &first, "adventureDeck");
    view_fmt(&w, "%d", g->adventure_deck_count);
    view_key(&w, &first, "adventureDiscard");
    view_fmt(&w, "%d", g->adventure_discard_count);
    view_key(&w, &first, "logCount");
    view_fmt(&w, "%d", g->log.count);

    view_key(&w, &first, "flags");
    view_raw(&w, "{");
    ofirst = 1;
    view_key(&w, &ofirst, "conquered");
    view_bool(&w, g->conquered_this_turn);
    view_key(&w, &ofirst, "cardsTraded");
    view_bool(&w, g->cards_traded_this_turn);
    view_key(&w, &ofirst, "fortified");
    view_bool(&w, g->fortified_this_turn && g->extra_fortifies <= 0);
    view_key(&w, &ofirst, "mustTrade");
    view_bool(&w, g->must_trade);
    view_key(&w, &ofirst, "mustDiscardAdventure");
    view_bool(&w, g->must_discard_adventure);
    view_key(&w, &ofirst, "extraFortify");
    view_fmt(&w, "%d", g->extra_fortifies);
    view_raw(&w, "}");

    view_key(&w, &first, "kick");
    if (g->kick_target >= 0 && g->kick_target < g->player_count) {
        int alive = 0;
        int needed;
        int af = 1;
        for (i = 0; i < g->player_count; i++) {
            if (g->seats[i].alive && i != g->kick_target) {
                alive++;
            }
        }
        needed = alive / 2 + 1;
        view_raw(&w, "{");
        ofirst = 1;
        view_key(&w, &ofirst, "target");
        view_fmt(&w, "%d", g->kick_target);
        view_key(&w, &ofirst, "needed");
        view_fmt(&w, "%d", needed);
        view_key(&w, &ofirst, "started");
        view_fmt(&w, "%ld", (long)g->kick_started);
        view_key(&w, &ofirst, "votes");
        view_raw(&w, "[");
        for (i = 0; i < g->player_count && i < 32; i++) {
            if (g->kick_votes & (1u << (unsigned int)i)) {
                view_sep(&w, &af);
                view_fmt(&w, "%d", i);
            }
        }
        view_raw(&w, "]");
        view_raw(&w, "}");
    } else {
        view_raw(&w, "null");
    }

    view_key(&w, &first, "reinforce");
    view_raw(&w, "{");
    ofirst = 1;
    view_key(&w, &ofirst, "territories");
    view_fmt(&w, "%d", g->reinforce_info.territories);
    view_key(&w, &ofirst, "base");
    view_fmt(&w, "%d", g->reinforce_info.base);
    view_key(&w, &ofirst, "regions");
    view_fmt(&w, "%d", g->reinforce_info.region_bonus);
    view_key(&w, &ofirst, "strongholds");
    view_fmt(&w, "%d", g->reinforce_info.stronghold_bonus);
    view_key(&w, &ofirst, "cards");
    view_fmt(&w, "%d", g->reinforce_info.card_bonus);
    view_key(&w, &ofirst, "total");
    view_fmt(&w, "%d", g->reinforce_info.total);
    view_key(&w, &ofirst, "pending");
    view_fmt(&w, "%d", g->reinforcements_pending);
    view_raw(&w, "}");

    view_key(&w, &first, "pending");
    if (g->phase == PHASE_CONQUEST_MOVE) {
        view_raw(&w, "{");
        ofirst = 1;
        view_key(&w, &ofirst, "from");
        view_fmt(&w, "%d", g->pending_from);
        view_key(&w, &ofirst, "to");
        view_fmt(&w, "%d", g->pending_to);
        view_key(&w, &ofirst, "min");
        view_fmt(&w, "%d", g->pending_min);
        view_key(&w, &ofirst, "max");
        view_fmt(&w, "%d", g->pending_max);
        view_raw(&w, "}");
    } else {
        view_raw(&w, "null");
    }

    /* printed adventure cards: active effects and pending choices */
    view_key(&w, &first, "printed");
    view_raw(&w, "{");
    ofirst = 1;
    view_key(&w, &ofirst, "toll");
    view_fmt(&w, "%d", g->toll_by - 1);
    view_key(&w, &ofirst, "frozen");
    view_raw(&w, "[");
    afirst = 1;
    for (i = 0; i < g->territory_count; i++) {
        if (g->frozen_by[i] != 0) {
            view_sep(&w, &afirst);
            view_fmt(&w, "[%d,%d]", i, g->frozen_by[i] - 1);
        }
    }
    view_raw(&w, "]");
    view_key(&w, &ofirst, "blockedBridges");
    view_raw(&w, "[");
    afirst = 1;
    for (i = 0; i < g->map->bridgeCount && i < MAP_MAX_BRIDGES; i++) {
        if (g->bridge_blocked_by[i] != 0) {
            view_sep(&w, &afirst);
            view_fmt(&w, "[%d,%d]", i, g->bridge_blocked_by[i] - 1);
        }
    }
    view_raw(&w, "]");
    view_key(&w, &ofirst, "invasion");
    if (g->inv_from >= 0) {
        view_fmt(&w, "{\"from\":%d,\"to\":%d,\"noStronghold\":%s,"
                 "\"mountain\":%s,\"bonus\":%d}", g->inv_from, g->inv_to,
                 (g->inv_flags & INV_NO_STRONGHOLD) ? "true" : "false",
                 (g->inv_flags & INV_MOUNTAIN) ? "true" : "false",
                 g->inv_attack_bonus);
    } else {
        view_raw(&w, "null");
    }
    view_key(&w, &ofirst, "freeLeader");
    view_bool(&w, g->pending_free_leader);
    view_key(&w, &ofirst, "bonus");
    view_fmt(&w, "%d", g->bonus_pending);
    view_key(&w, &ofirst, "bonusSplit");   /* may the battalions be split? */
    view_bool(&w, g->bonus_area == BONUS_REGION);
    view_key(&w, &ofirst, "uprising");
    if (viewer_seat >= 0 && g->seats[viewer_seat].uprising_card >= 0) {
        view_fmt(&w, "%d", card_territory(g, g->seats[viewer_seat].uprising_card));
    } else {
        view_raw(&w, "-1");   /* secret from everyone else */
    }
    view_raw(&w, "}");

    view_key(&w, &first, "closedLinks");
    view_raw(&w, "[");
    afirst = 1;
    for (i = 0; i < g->map->linkCount && i < MAP_MAX_LINKS; i++) {
        if (g->link_closed_by[i] != 0) {
            view_sep(&w, &afirst);
            view_raw(&w, "{");
            ofirst = 1;
            view_key(&w, &ofirst, "a");
            view_fmt(&w, "%d", g->map->links[i].a);
            view_key(&w, &ofirst, "b");
            view_fmt(&w, "%d", g->map->links[i].b);
            view_raw(&w, "}");
        }
    }
    view_raw(&w, "]");

    view_key(&w, &first, "hand");
    if (viewer_seat >= 0) {
        view_raw(&w, "[");
        afirst = 1;
        for (i = 0; i < g->seats[viewer_seat].hand_count; i++) {
            int card = g->seats[viewer_seat].hand[i];
            view_sep(&w, &afirst);
            view_raw(&w, "{");
            ofirst = 1;
            view_key(&w, &ofirst, "card");
            view_fmt(&w, "%d", card);
            if (card_valid(g, card)) {
                const struct MapTerritoryCard *mc =
                    &g->map->territoryCards[card];
                view_key(&w, &ofirst, "territory");
                view_fmt(&w, "%d", mc->territory);
                view_key(&w, &ofirst, "wild");
                view_bool(&w, mc->wild);
                view_key(&w, &ofirst, "symbol");
                if (mc->wild) {
                    view_string(&w, "");
                } else {
                    char symbol[2] = { mc->symbol, '\0' };
                    view_string(&w, symbol);
                }
            } else {
                view_key(&w, &ofirst, "territory");
                view_fmt(&w, "%d", -1);
                view_key(&w, &ofirst, "wild");
                view_bool(&w, 0);
                view_key(&w, &ofirst, "symbol");
                view_string(&w, "");
            }
            view_raw(&w, "}");
        }
        view_raw(&w, "]");
    } else {
        view_raw(&w, "null");
    }

    view_key(&w, &first, "adventureHand");
    if (viewer_seat >= 0) {
        view_raw(&w, "[");
        afirst = 1;
        for (i = 0; i < g->seats[viewer_seat].adventure_hand_count; i++) {
            int card = g->seats[viewer_seat].adventure_hand[i];
            const struct MapAdventureCard *mc;
            if (card < 0 || card >= g->map->adventureCardCount) {
                continue;
            }
            mc = &g->map->adventureCards[card];
            view_sep(&w, &afirst);
            view_raw(&w, "{");
            ofirst = 1;
            view_key(&w, &ofirst, "id");
            view_string(&w, mc->id);
            view_key(&w, &ofirst, "kind");
            view_string(&w, mc->kind);
            view_key(&w, &ofirst, "title");
            view_string(&w, mc->title);
            view_key(&w, &ofirst, "text");
            view_string(&w, mc->text);
            view_key(&w, &ofirst, "effect");
            view_string(&w, mc->effect);
            view_key(&w, &ofirst, "args");
            view_raw(&w, mc->args);
            view_key(&w, &ofirst, "auto");
            view_fmt(&w, "%d", g->seats[viewer_seat].adventure_auto[i]);
            view_key(&w, &ofirst, "autoN");
            view_fmt(&w, "%d",
                     g->seats[viewer_seat].adventure_auto_n[i]);
            view_raw(&w, "}");
        }
        view_raw(&w, "]");
    } else {
        view_raw(&w, "null");
    }

    view_key(&w, &first, "legal");
    if (show_legal) {
        game_legal_actions(g, &legal);
        view_raw(&w, "{");
        ofirst = 1;
        view_key(&w, &ofirst, "canTrade");
        view_bool(&w, legal.can_trade);
        view_key(&w, &ofirst, "canDoneReinforce");
        view_bool(&w, legal.can_done_reinforce);
        view_key(&w, &ofirst, "canDoneAttack");
        view_bool(&w, legal.can_done_attack);
        view_key(&w, &ofirst, "canEndTurn");
        view_bool(&w, legal.can_end_turn);
        view_key(&w, &ofirst, "canResign");
        view_bool(&w, legal.can_resign);
        view_key(&w, &ofirst, "mustDiscardAdventure");
        view_bool(&w, legal.must_discard_adventure);
        view_key(&w, &ofirst, "canRecruitLeader");
        view_bool(&w, legal.can_recruit_leader);
        view_key(&w, &ofirst, "recruitCost");
        view_fmt(&w, "%d", legal.recruit_cost);
        view_key(&w, &ofirst, "hasRecruitCard");
        view_bool(&w, legal.has_recruit_card);
        view_key(&w, &ofirst, "reinforcementsPending");
        view_fmt(&w, "%d", legal.reinforcements_pending);
        view_key(&w, &ofirst, "setupPool");
        view_fmt(&w, "%d", legal.setup_pool);
        view_key(&w, &ofirst, "batchRemaining");
        view_fmt(&w, "%d", legal.setup_batch_remaining);
        view_key(&w, &ofirst, "leadersRemaining");
        view_fmt(&w, "%d", legal.leaders_remaining);
        view_key(&w, &ofirst, "moveInMin");
        view_fmt(&w, "%d", legal.move_in_min);
        view_key(&w, &ofirst, "moveInMax");
        view_fmt(&w, "%d", legal.move_in_max);

        view_key(&w, &ofirst, "claimable");
        view_raw(&w, "[");
        afirst = 1;
        for (i = 0; i < g->territory_count; i++) {
            if (legal.claimable[i]) {
                view_sep(&w, &afirst);
                view_fmt(&w, "%d", i);
            }
        }
        view_raw(&w, "]");

        view_key(&w, &ofirst, "placeable");
        view_raw(&w, "[");
        afirst = 1;
        for (i = 0; i < g->territory_count; i++) {
            if (legal.placeable[i]) {
                view_sep(&w, &afirst);
                view_fmt(&w, "%d", i);
            }
        }
        view_raw(&w, "]");

        view_key(&w, &ofirst, "leaderPlaceable");
        view_raw(&w, "[");
        afirst = 1;
        for (i = 0; i < g->territory_count; i++) {
            if (legal.leader_placeable[i]) {
                view_sep(&w, &afirst);
                view_fmt(&w, "%d", i);
            }
        }
        view_raw(&w, "]");

        view_key(&w, &ofirst, "recruitPlaceable");
        view_raw(&w, "[");
        afirst = 1;
        for (i = 0; i < g->territory_count; i++) {
            if (legal.recruit_placeable[i]) {
                view_sep(&w, &afirst);
                view_fmt(&w, "%d", i);
            }
        }
        view_raw(&w, "]");

        view_key(&w, &ofirst, "freeLeaderPending");
        view_bool(&w, legal.free_leader_pending);
        view_key(&w, &ofirst, "canOathbreakers");
        view_bool(&w, legal.can_oathbreakers);
        view_key(&w, &ofirst, "bonusPlaceable");
        view_raw(&w, "[");
        afirst = 1;
        for (i = 0; i < g->territory_count; i++) {
            if (legal.bonus_placeable[i]) {
                view_sep(&w, &afirst);
                view_fmt(&w, "%d", i);
            }
        }
        view_raw(&w, "]");
        view_key(&w, &ofirst, "claimableMissions");
        view_raw(&w, "[");
        afirst = 1;
        for (i = 0; viewer_seat >= 0 &&
                    i < g->seats[viewer_seat].adventure_hand_count; i++) {
            if (legal.claimable_mission[i]) {
                view_sep(&w, &afirst);
                view_fmt(&w, "%d", i);
            }
        }
        view_raw(&w, "]");

        view_key(&w, &ofirst, "playableAdventure");
        view_raw(&w, "[");
        afirst = 1;
        if (viewer_seat >= 0) {
            for (i = 0; i < g->seats[viewer_seat].adventure_hand_count;
                 i++) {
                if (legal.playable_adventure[i]) {
                    view_sep(&w, &afirst);
                    view_fmt(&w, "%d", i);
                }
            }
        }
        view_raw(&w, "]");

        view_key(&w, &ofirst, "attacks");
        view_raw(&w, "[");
        afirst = 1;
        for (i = 0; i < g->territory_count; i++) {
            for (j = 0; j < g->territory_count; j++) {
                if (legal.attack[i][j]) {
                    view_sep(&w, &afirst);
                    view_raw(&w, "{");
                    bfirst = 1;
                    view_key(&w, &bfirst, "from");
                    view_fmt(&w, "%d", i);
                    view_key(&w, &bfirst, "to");
                    view_fmt(&w, "%d", j);
                    view_key(&w, &bfirst, "maxDice");
                    view_fmt(&w, "%d", legal.max_attack_dice[i]);
                    view_raw(&w, "}");
                }
            }
        }
        view_raw(&w, "]");

        view_key(&w, &ofirst, "fortifies");
        view_raw(&w, "[");
        afirst = 1;
        for (i = 0; i < g->territory_count; i++) {
            for (j = 0; j < g->territory_count; j++) {
                if (legal.fortify[i][j]) {
                    view_sep(&w, &afirst);
                    view_raw(&w, "{");
                    bfirst = 1;
                    view_key(&w, &bfirst, "from");
                    view_fmt(&w, "%d", i);
                    view_key(&w, &bfirst, "to");
                    view_fmt(&w, "%d", j);
                    if (legal.fortify[i][j] == 2) {
                        view_key(&w, &bfirst, "needsLeader");
                        view_bool(&w, 1);
                    }
                    view_raw(&w, "}");
                }
            }
        }
        view_raw(&w, "]");
        view_raw(&w, "}");
    } else {
        view_raw(&w, "null");
    }

    view_key(&w, &first, "log");
    view_raw(&w, "[");
    afirst = 1;
    for (i = 0; i < g->log.count; i++) {
        const struct Event *event;
        index = (g->log.next - g->log.count + i + GAME_LOG_RING) %
                GAME_LOG_RING;
        event = &g->log.events[index];
        view_sep(&w, &afirst);
        view_raw(&w, "{");
        ofirst = 1;
        view_key(&w, &ofirst, "kind");
        view_string(&w, game_event_name(event->kind));
        view_key(&w, &ofirst, "version");
        view_fmt(&w, "%d", event->version);
        view_key(&w, &ofirst, "player");
        view_fmt(&w, "%d", event->player);
        view_key(&w, &ofirst, "from");
        view_fmt(&w, "%d", event->from);
        view_key(&w, &ofirst, "to");
        view_fmt(&w, "%d", event->to);
        view_key(&w, &ofirst, "count");
        view_fmt(&w, "%d", event->count);
        if (event->kind == EVENT_TRADE_CARDS) {
            view_key(&w, &ofirst, "cardA");
            view_fmt(&w, "%d", event->card_a);
            view_key(&w, &ofirst, "cardB");
            view_fmt(&w, "%d", event->card_b);
            view_key(&w, &ofirst, "cardC");
            view_fmt(&w, "%d", event->card_c);
            view_key(&w, &ofirst, "setBonus");
            view_fmt(&w, "%d", event->set_bonus);
        }
        view_key(&w, &ofirst, "text");
        view_string(&w, event->text);
        if (event->battle_count > 0) {
            int b;
            view_key(&w, &ofirst, "battles");
            view_raw(&w, "[");
            bfirst = 1;
            for (b = 0; b < event->battle_count &&
                        b < GAME_MAX_BLITZ_ROUNDS; b++) {
                int k;
                const struct DiceBattle *battle = &event->battles[b];
                view_sep(&w, &bfirst);
                view_raw(&w, "{");
                {
                    int cf = 1;
                    view_key(&w, &cf, "attacker");
                    view_raw(&w, "[");
                    for (k = 0; k < battle->attacker_count; k++) {
                        if (k > 0) {
                            view_char(&w, ',');
                        }
                        view_fmt(&w, "%d", battle->attacker[k]);
                    }
                    view_raw(&w, "]");
                    view_key(&w, &cf, "defender");
                    view_raw(&w, "[");
                    for (k = 0; k < battle->defender_count; k++) {
                        if (k > 0) {
                            view_char(&w, ',');
                        }
                        view_fmt(&w, "%d", battle->defender[k]);
                    }
                    view_raw(&w, "]");
                    view_key(&w, &cf, "attackerBonus");
                    view_fmt(&w, "%d", battle->attacker_bonus);
                    view_key(&w, &cf, "defenderBonus");
                    view_fmt(&w, "%d", battle->defender_bonus);
                    view_key(&w, &cf, "attackerLosses");
                    view_fmt(&w, "%d", battle->attacker_losses);
                    view_key(&w, &cf, "defenderLosses");
                    view_fmt(&w, "%d", battle->defender_losses);
                }
                view_raw(&w, "}");
            }
            view_raw(&w, "]");
            if (event->truncated) {
                view_key(&w, &ofirst, "truncated");
                view_bool(&w, 1);
            }
        }
        view_raw(&w, "}");
    }
    view_raw(&w, "]");

    view_raw(&w, "}");
    if (w.overflow) {
        out[out_size - 1] = '\0';
        return -1;
    }
    out[w.used] = '\0';
    return 0;
}
