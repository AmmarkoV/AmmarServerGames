#include "serialize.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../json.h"

struct SW {
    char *out;
    size_t capacity;
    size_t used;
    int overflow;
};

static void sw_char(struct SW *w, char c)
{
    if (w->used + 2 > w->capacity) {
        w->overflow = 1;
        return;
    }
    w->out[w->used++] = c;
}

static void sw_raw(struct SW *w, const char *text)
{
    while (text != NULL && *text != '\0' && !w->overflow) {
        sw_char(w, *text++);
    }
}

static void sw_fmt(struct SW *w, const char *fmt, ...)
{
    char buffer[128];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    sw_raw(w, buffer);
}

static void sw_string(struct SW *w, const char *text)
{
    const unsigned char *p = (const unsigned char *)
                             (text != NULL ? text : "");
    static const char hex[] = "0123456789abcdef";

    sw_char(w, '"');
    while (*p != '\0' && !w->overflow) {
        unsigned char c = *p++;
        switch (c) {
        case '"': sw_raw(w, "\\\""); break;
        case '\\': sw_raw(w, "\\\\"); break;
        case '\b': sw_raw(w, "\\b"); break;
        case '\f': sw_raw(w, "\\f"); break;
        case '\n': sw_raw(w, "\\n"); break;
        case '\r': sw_raw(w, "\\r"); break;
        case '\t': sw_raw(w, "\\t"); break;
        default:
            if (c < 0x20) {
                char escape[7] = { '\\', 'u', '0', '0',
                                   hex[c >> 4], hex[c & 0xf], '\0' };
                sw_raw(w, escape);
            } else {
                sw_char(w, (char)c);
            }
            break;
        }
    }
    sw_char(w, '"');
}

static void sw_key(struct SW *w, int *first, const char *key)
{
    if (!*first) {
        sw_char(w, ',');
    }
    *first = 0;
    sw_string(w, key);
    sw_char(w, ':');
}

static void sw_bool(struct SW *w, int value)
{
    sw_raw(w, value ? "true" : "false");
}

static void sw_int_array(struct SW *w, const int *values, int count)
{
    int i;

    sw_raw(w, "[");
    for (i = 0; i < count; i++) {
        if (i > 0) {
            sw_char(w, ',');
        }
        sw_fmt(w, "%d", values[i]);
    }
    sw_raw(w, "]");
}

static void write_stats(struct SW *w, int *first,
                        const struct PlayerStats *stats)
{
    sw_key(w, first, "stats");
    sw_raw(w, "{");
    {
        int f = 1;
        sw_key(w, &f, "gamesPlayed");
        sw_fmt(w, "%d", stats->games_played);
        sw_key(w, &f, "wins");
        sw_fmt(w, "%d", stats->wins);
        sw_key(w, &f, "losses");
        sw_fmt(w, "%d", stats->losses);
        sw_key(w, &f, "territoriesTaken");
        sw_fmt(w, "%d", stats->territories_taken);
        sw_key(w, &f, "armiesKilled");
        sw_fmt(w, "%d", stats->armies_killed);
        sw_key(w, &f, "armiesLost");
        sw_fmt(w, "%d", stats->armies_lost);
    }
    sw_raw(w, "}");
}

static void write_event(struct SW *w, const struct Event *event)
{
    int f = 1;
    int i;

    sw_raw(w, "{");
    sw_key(w, &f, "kind");
    sw_fmt(w, "%d", event->kind);
    sw_key(w, &f, "version");
    sw_fmt(w, "%d", event->version);
    sw_key(w, &f, "player");
    sw_fmt(w, "%d", event->player);
    sw_key(w, &f, "from");
    sw_fmt(w, "%d", event->from);
    sw_key(w, &f, "to");
    sw_fmt(w, "%d", event->to);
    sw_key(w, &f, "count");
    sw_fmt(w, "%d", event->count);
    sw_key(w, &f, "cardA");
    sw_fmt(w, "%d", event->card_a);
    sw_key(w, &f, "cardB");
    sw_fmt(w, "%d", event->card_b);
    sw_key(w, &f, "cardC");
    sw_fmt(w, "%d", event->card_c);
    sw_key(w, &f, "setBonus");
    sw_fmt(w, "%d", event->set_bonus);
    sw_key(w, &f, "battleCount");
    sw_fmt(w, "%d", event->battle_count);
    sw_key(w, &f, "truncated");
    sw_bool(w, event->truncated);
    sw_key(w, &f, "text");
    sw_string(w, event->text);
    sw_key(w, &f, "battles");
    sw_raw(w, "[");
    for (i = 0; i < event->battle_count && i < GAME_MAX_BLITZ_ROUNDS; i++) {
        const struct DiceBattle *battle = &event->battles[i];
        int bf = 1;
        if (i > 0) {
            sw_char(w, ',');
        }
        sw_raw(w, "{");
        sw_key(w, &bf, "attacker");
        sw_int_array(w, battle->attacker, battle->attacker_count);
        sw_key(w, &bf, "defender");
        sw_int_array(w, battle->defender, battle->defender_count);
        sw_key(w, &bf, "attackerCount");
        sw_fmt(w, "%d", battle->attacker_count);
        sw_key(w, &bf, "defenderCount");
        sw_fmt(w, "%d", battle->defender_count);
        sw_key(w, &bf, "attackerBonus");
        sw_fmt(w, "%d", battle->attacker_bonus);
        sw_key(w, &bf, "defenderBonus");
        sw_fmt(w, "%d", battle->defender_bonus);
        sw_key(w, &bf, "attackerLosses");
        sw_fmt(w, "%d", battle->attacker_losses);
        sw_key(w, &bf, "defenderLosses");
        sw_fmt(w, "%d", battle->defender_losses);
        sw_raw(w, "}");
    }
    sw_raw(w, "]");
    sw_raw(w, "}");
}

static void write_client_event(struct SW *w, const struct Event *event)
{
    int f = 1;
    int i;

    sw_raw(w, "{");
    sw_key(w, &f, "kind");
    sw_string(w, game_event_name(event->kind));
    sw_key(w, &f, "version");
    sw_fmt(w, "%d", event->version);
    sw_key(w, &f, "player");
    sw_fmt(w, "%d", event->player);
    sw_key(w, &f, "from");
    sw_fmt(w, "%d", event->from);
    sw_key(w, &f, "to");
    sw_fmt(w, "%d", event->to);
    sw_key(w, &f, "count");
    sw_fmt(w, "%d", event->count);
    if (event->kind == EVENT_TRADE_CARDS) {
        sw_key(w, &f, "cardA");
        sw_fmt(w, "%d", event->card_a);
        sw_key(w, &f, "cardB");
        sw_fmt(w, "%d", event->card_b);
        sw_key(w, &f, "cardC");
        sw_fmt(w, "%d", event->card_c);
        sw_key(w, &f, "setBonus");
        sw_fmt(w, "%d", event->set_bonus);
    }
    sw_key(w, &f, "text");
    sw_string(w, event->text);
    if (event->battle_count > 0) {
        sw_key(w, &f, "battles");
        sw_raw(w, "[");
        for (i = 0; i < event->battle_count &&
                    i < GAME_MAX_BLITZ_ROUNDS; i++) {
            const struct DiceBattle *battle = &event->battles[i];
            int bf = 1;
            if (i > 0) {
                sw_char(w, ',');
            }
            sw_raw(w, "{");
            sw_key(w, &bf, "attacker");
            sw_int_array(w, battle->attacker, battle->attacker_count);
            sw_key(w, &bf, "defender");
            sw_int_array(w, battle->defender, battle->defender_count);
            sw_key(w, &bf, "attackerBonus");
            sw_fmt(w, "%d", battle->attacker_bonus);
            sw_key(w, &bf, "defenderBonus");
            sw_fmt(w, "%d", battle->defender_bonus);
            sw_key(w, &bf, "attackerLosses");
            sw_fmt(w, "%d", battle->attacker_losses);
            sw_key(w, &bf, "defenderLosses");
            sw_fmt(w, "%d", battle->defender_losses);
            sw_raw(w, "}");
        }
        sw_raw(w, "]");
        if (event->truncated) {
            sw_key(w, &f, "truncated");
            sw_bool(w, 1);
        }
    }
    sw_raw(w, "}");
}

int game_event_to_json(const struct Event *event, char *out, size_t out_size)
{
    struct SW w;

    if (event == NULL || out == NULL || out_size == 0) {
        return -1;
    }
    memset(&w, 0, sizeof(w));
    w.out = out;
    w.capacity = out_size;
    write_client_event(&w, event);
    if (w.overflow) {
        out[out_size - 1] = '\0';
        return -1;
    }
    out[w.used] = '\0';
    return 0;
}

int game_to_json(const struct Game *g, char *out, size_t out_size)
{
    struct SW w;
    int first, ofirst;
    int i;

    if (g == NULL || out == NULL || out_size == 0) {
        return -1;
    }
    memset(&w, 0, sizeof(w));
    w.out = out;
    w.capacity = out_size;
    sw_raw(&w, "{");
    first = 1;
    sw_key(&w, &first, "format");
    sw_fmt(&w, "%d", GAME_SERIALIZE_FORMAT);
    sw_key(&w, &first, "id");
    sw_fmt(&w, "%d", g->id);
    sw_key(&w, &first, "name");
    sw_string(&w, g->name);
    sw_key(&w, &first, "mapPack");
    sw_string(&w, g->map_pack);
    sw_key(&w, &first, "passwordHash");
    sw_string(&w, g->password_hash);
    sw_key(&w, &first, "host");
    sw_string(&w, g->host);
    sw_key(&w, &first, "status");
    sw_string(&w, game_status_name(g->status));
    sw_key(&w, &first, "phase");
    sw_string(&w, game_phase_name(g->phase));
    sw_key(&w, &first, "version");
    sw_fmt(&w, "%d", g->version);
    sw_key(&w, &first, "maxPlayers");
    sw_fmt(&w, "%d", g->max_players);
    sw_key(&w, &first, "playerCount");
    sw_fmt(&w, "%d", g->player_count);
    sw_key(&w, &first, "turn");
    sw_fmt(&w, "%d", g->turn);
    sw_key(&w, &first, "current");
    sw_fmt(&w, "%d", g->current_player);
    sw_key(&w, &first, "winner");
    sw_fmt(&w, "%d", g->winner);
    sw_key(&w, &first, "created");
    sw_fmt(&w, "%lld", (long long)g->created);
    sw_key(&w, &first, "updated");
    sw_fmt(&w, "%lld", (long long)g->updated);

    sw_key(&w, &first, "flags");
    sw_raw(&w, "{");
    ofirst = 1;
    sw_key(&w, &ofirst, "conquered");
    sw_bool(&w, g->conquered_this_turn);
    sw_key(&w, &ofirst, "cardsTraded");
    sw_bool(&w, g->cards_traded_this_turn);
    sw_key(&w, &ofirst, "fortified");
    sw_bool(&w, g->fortified_this_turn);
    sw_key(&w, &ofirst, "mustTrade");
    sw_bool(&w, g->must_trade);
    sw_key(&w, &ofirst, "mustDiscardAdventure");
    sw_bool(&w, g->must_discard_adventure);
    sw_key(&w, &ofirst, "extraFortify");
    sw_fmt(&w, "%d", g->extra_fortifies);
    sw_key(&w, &ofirst, "conqueredThisTurn");
    sw_fmt(&w, "%d", g->territories_conquered_this_turn);
    sw_raw(&w, "}");

    sw_key(&w, &first, "closedLinks");
    sw_raw(&w, "[");
    for (i = 0; i < g->map->linkCount && i < MAP_MAX_LINKS; i++) {
        if (i > 0) {
            sw_char(&w, ',');
        }
        sw_fmt(&w, "%d", g->link_closed_by[i]);
    }
    sw_raw(&w, "]");

    sw_key(&w, &first, "kick");
    sw_raw(&w, "{");
    ofirst = 1;
    sw_key(&w, &ofirst, "target");
    sw_fmt(&w, "%d", g->kick_target);
    sw_key(&w, &ofirst, "votes");
    sw_fmt(&w, "%u", g->kick_votes);
    sw_key(&w, &ofirst, "started");
    sw_fmt(&w, "%ld", (long)g->kick_started);
    sw_raw(&w, "}");

    sw_key(&w, &first, "setup");
    sw_raw(&w, "{");
    ofirst = 1;
    sw_key(&w, &ofirst, "pool");
    sw_raw(&w, "[");
    for (i = 0; i < g->player_count; i++) {
        if (i > 0) {
            sw_char(&w, ',');
        }
        sw_fmt(&w, "%d", g->setup_pool[i]);
    }
    sw_raw(&w, "]");
    sw_key(&w, &ofirst, "batchRemaining");
    sw_fmt(&w, "%d", g->setup_batch_remaining);
    sw_key(&w, &ofirst, "batchSize");
    sw_fmt(&w, "%d", g->setup_batch_size);
    sw_key(&w, &ofirst, "startMode");
    sw_string(&w, g->start_mode == GAME_START_DRAFT ? "draft" : "deal");
    sw_key(&w, &ofirst, "neutral");
    sw_bool(&w, g->neutral_enabled);
    sw_raw(&w, "}");

    sw_key(&w, &first, "reinforce");
    sw_raw(&w, "{");
    ofirst = 1;
    sw_key(&w, &ofirst, "territories");
    sw_fmt(&w, "%d", g->reinforce_info.territories);
    sw_key(&w, &ofirst, "base");
    sw_fmt(&w, "%d", g->reinforce_info.base);
    sw_key(&w, &ofirst, "regions");
    sw_fmt(&w, "%d", g->reinforce_info.region_bonus);
    sw_key(&w, &ofirst, "strongholds");
    sw_fmt(&w, "%d", g->reinforce_info.stronghold_bonus);
    sw_key(&w, &ofirst, "cards");
    sw_fmt(&w, "%d", g->reinforce_info.card_bonus);
    sw_key(&w, &ofirst, "total");
    sw_fmt(&w, "%d", g->reinforce_info.total);
    sw_key(&w, &ofirst, "regionsHeld");
    sw_fmt(&w, "%d", g->reinforce_info.regions_held);
    sw_key(&w, &ofirst, "strongholdsHeld");
    sw_fmt(&w, "%d", g->reinforce_info.strongholds_held);
    sw_key(&w, &ofirst, "pending");
    sw_fmt(&w, "%d", g->reinforcements_pending);
    sw_key(&w, &ofirst, "deferred");
    sw_raw(&w, "[");
    for (i = 0; i < g->player_count; i++) {
        if (i > 0) {
            sw_char(&w, ',');
        }
        sw_fmt(&w, "%d", g->deferred_card_bonus[i]);
    }
    sw_raw(&w, "]");
    sw_raw(&w, "}");

    sw_key(&w, &first, "pending");
    sw_raw(&w, "{");
    ofirst = 1;
    sw_key(&w, &ofirst, "active");
    sw_bool(&w, g->pending_from >= 0);
    if (g->pending_from >= 0) {
        sw_key(&w, &ofirst, "from");
        sw_fmt(&w, "%d", g->pending_from);
        sw_key(&w, &ofirst, "to");
        sw_fmt(&w, "%d", g->pending_to);
        sw_key(&w, &ofirst, "min");
        sw_fmt(&w, "%d", g->pending_min);
        sw_key(&w, &ofirst, "max");
        sw_fmt(&w, "%d", g->pending_max);
    }
    sw_raw(&w, "}");

    sw_key(&w, &first, "rng");
    sw_raw(&w, "{");
    ofirst = 1;
    sw_key(&w, &ofirst, "hi");
    sw_fmt(&w, "%llu",
           (unsigned long long)(g->rng.state >> 32));
    sw_key(&w, &ofirst, "lo");
    sw_fmt(&w, "%llu",
           (unsigned long long)(g->rng.state & UINT64_C(0xffffffff)));
    sw_raw(&w, "}");

    sw_key(&w, &first, "players");
    sw_raw(&w, "[");
    for (i = 0; i < g->player_count; i++) {
        const struct Seat *seat = &g->seats[i];
        if (i > 0) {
            sw_char(&w, ',');
        }
        sw_raw(&w, "{");
        ofirst = 1;
        sw_key(&w, &ofirst, "user");
        sw_string(&w, seat->username);
        sw_key(&w, &ofirst, "faction");
        sw_fmt(&w, "%d", seat->faction);
        sw_key(&w, &ofirst, "alive");
        sw_bool(&w, seat->alive);
        sw_key(&w, &ofirst, "hand");
        sw_int_array(&w, seat->hand, seat->hand_count);
        sw_key(&w, &ofirst, "adventureHand");
        sw_int_array(&w, seat->adventure_hand,
                     seat->adventure_hand_count);
        sw_key(&w, &ofirst, "adventureAuto");
        sw_raw(&w, "[");
        {
            int aw_i;
            for (aw_i = 0; aw_i < seat->adventure_hand_count; aw_i++) {
                if (aw_i > 0) {
                    sw_char(&w, ',');
                }
                sw_fmt(&w, "%u",
                       (unsigned int)seat->adventure_auto[aw_i]);
            }
        }
        sw_raw(&w, "]");
        sw_key(&w, &ofirst, "adventureAutoN");
        sw_raw(&w, "[");
        {
            int aw_i;
            for (aw_i = 0; aw_i < seat->adventure_hand_count; aw_i++) {
                if (aw_i > 0) {
                    sw_char(&w, ',');
                }
                sw_fmt(&w, "%u",
                       (unsigned int)seat->adventure_auto_n[aw_i]);
            }
        }
        sw_raw(&w, "]");
        sw_key(&w, &ofirst, "attackBonus");
        sw_fmt(&w, "%d", seat->attack_bonus);
        sw_key(&w, &ofirst, "siteClaimed");
        sw_raw(&w, "[");
        {
            int sw_i;
            for (sw_i = 0; sw_i < GAME_SITE_MASK_WORDS; sw_i++) {
                if (sw_i > 0) {
                    sw_char(&w, ',');
                }
                sw_fmt(&w, "%u", seat->site_claimed[sw_i]);
            }
        }
        sw_raw(&w, "]");
        sw_key(&w, &ofirst, "leaders");
        sw_fmt(&w, "%d", seat->leaders_in_play);
        sw_key(&w, &ofirst, "missions");
        sw_fmt(&w, "%d", seat->missions_done);
        sw_key(&w, &ofirst, "lastSeen");
        sw_fmt(&w, "%d", seat->last_seen_version);
        write_stats(&w, &ofirst, &seat->stats);
        sw_raw(&w, "}");
    }
    sw_raw(&w, "]");

    sw_key(&w, &first, "territories");
    sw_raw(&w, "[");
    for (i = 0; i < g->territory_count; i++) {
        const struct TerritoryState *t = &g->territories[i];
        if (i > 0) {
            sw_char(&w, ',');
        }
        sw_raw(&w, "{");
        ofirst = 1;
        sw_key(&w, &ofirst, "owner");
        sw_fmt(&w, "%d", t->owner);
        sw_key(&w, &ofirst, "armies");
        sw_fmt(&w, "%d", t->armies);
        sw_key(&w, &ofirst, "leader");
        sw_fmt(&w, "%d", t->leader_of);
        sw_key(&w, &ofirst, "neutral");
        sw_bool(&w, t->neutral);
        sw_raw(&w, "}");
    }
    sw_raw(&w, "]");

    sw_key(&w, &first, "decks");
    sw_raw(&w, "{");
    ofirst = 1;
    sw_key(&w, &ofirst, "territory");
    sw_int_array(&w, g->deck, g->deck_count);
    sw_key(&w, &ofirst, "discard");
    sw_int_array(&w, g->discard, g->discard_count);
    sw_key(&w, &ofirst, "adventure");
    sw_int_array(&w, g->adventure_deck, g->adventure_deck_count);
    sw_key(&w, &ofirst, "adventureDiscard");
    sw_int_array(&w, g->adventure_discard, g->adventure_discard_count);
    sw_raw(&w, "}");

    sw_key(&w, &first, "log");
    sw_raw(&w, "[");
    for (i = 0; i < g->log.count; i++) {
        int index = (g->log.next - g->log.count + i + GAME_LOG_RING) %
                    GAME_LOG_RING;
        if (i > 0) {
            sw_char(&w, ',');
        }
        write_event(&w, &g->log.events[index]);
    }
    sw_raw(&w, "]");

    sw_raw(&w, "}");
    if (w.overflow) {
        out[out_size - 1] = '\0';
        return -1;
    }
    out[w.used] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/* Loading                                                            */
/* ------------------------------------------------------------------ */

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

static int req_int(const JsonValue *object, const char *key, int minimum,
                   int maximum, int *out, const char *context,
                   char *err, size_t err_size)
{
    long long value;

    if (json_get_int(json_get(object, key), &value) != 0) {
        set_error(err, err_size, "%s: field \"%s\" must be an integer",
                  context, key);
        return -1;
    }
    if (value < minimum || value > maximum) {
        set_error(err, err_size, "%s: field \"%s\" is out of range (%lld)",
                  context, key, value);
        return -1;
    }
    *out = (int)value;
    return 0;
}

static int req_bool(const JsonValue *object, const char *key, int *out,
                    const char *context, char *err, size_t err_size)
{
    if (json_get_bool(json_get(object, key), out) != 0) {
        set_error(err, err_size, "%s: field \"%s\" must be a boolean",
                  context, key);
        return -1;
    }
    return 0;
}

static int req_string(const JsonValue *object, const char *key,
                      char *out, size_t out_size, const char *context,
                      char *err, size_t err_size)
{
    const char *value = json_get_string(json_get(object, key));
    size_t length;

    if (value == NULL) {
        set_error(err, err_size, "%s: field \"%s\" must be a string",
                  context, key);
        return -1;
    }
    length = strlen(value);
    if (length >= out_size) {
        set_error(err, err_size, "%s: field \"%s\" is too long", context,
                  key);
        return -1;
    }
    memcpy(out, value, length + 1);
    return 0;
}

static int req_array(const JsonValue *object, const char *key, size_t maximum,
                     const JsonValue **out, const char *context,
                     char *err, size_t err_size)
{
    const JsonValue *value = json_get(object, key);

    if (value == NULL || value->type != JSON_ARRAY) {
        set_error(err, err_size, "%s: field \"%s\" must be an array",
                  context, key);
        return -1;
    }
    if (json_count(value) > maximum) {
        set_error(err, err_size, "%s: field \"%s\" has too many entries",
                  context, key);
        return -1;
    }
    *out = value;
    return 0;
}

static int req_object(const JsonValue *object, const char *key,
                      const JsonValue **out, const char *context,
                      char *err, size_t err_size)
{
    const JsonValue *value = json_get(object, key);

    if (value == NULL || value->type != JSON_OBJECT) {
        set_error(err, err_size, "%s: field \"%s\" must be an object",
                  context, key);
        return -1;
    }
    *out = value;
    return 0;
}

static int read_int_array(const JsonValue *object, const char *key,
                          int *out, int maximum, int minimum_value,
                          int maximum_value, int *count,
                          const char *context, char *err, size_t err_size)
{
    const JsonValue *array;
    size_t i, length;

    if (req_array(object, key, (size_t)maximum, &array, context,
                  err, err_size) != 0) {
        return -1;
    }
    length = json_count(array);
    for (i = 0; i < length; i++) {
        long long value;
        if (json_get_int(json_at(array, i), &value) != 0) {
            set_error(err, err_size, "%s: \"%s\" entry %zu is not an integer",
                      context, key, i);
            return -1;
        }
        if (value < minimum_value || value > maximum_value) {
            set_error(err, err_size, "%s: \"%s\" entry %zu is out of range",
                      context, key, i);
            return -1;
        }
        out[i] = (int)value;
    }
    *count = (int)length;
    return 0;
}

static int status_from_name(const char *name)
{
    if (strcmp(name, "open") == 0) {
        return GAME_OPEN;
    }
    if (strcmp(name, "running") == 0) {
        return GAME_RUNNING;
    }
    if (strcmp(name, "finished") == 0) {
        return GAME_FINISHED;
    }
    return -1;
}

static int phase_from_name(const char *name)
{
    int phase;
    for (phase = PHASE_LOBBY; phase <= PHASE_FINISHED; phase++) {
        if (strcmp(name, game_phase_name(phase)) == 0) {
            return phase;
        }
    }
    return -1;
}

static int read_stats(const JsonValue *object, struct PlayerStats *stats,
                      const char *context, char *err, size_t err_size)
{
    const JsonValue *stats_object;

    if (req_object(object, "stats", &stats_object, context,
                   err, err_size) != 0) {
        return -1;
    }
    if (req_int(stats_object, "gamesPlayed", 0, 100000000,
                &stats->games_played, context, err, err_size) != 0 ||
        req_int(stats_object, "wins", 0, 100000000, &stats->wins,
                context, err, err_size) != 0 ||
        req_int(stats_object, "losses", 0, 100000000, &stats->losses,
                context, err, err_size) != 0 ||
        req_int(stats_object, "territoriesTaken", 0, 100000000,
                &stats->territories_taken, context, err, err_size) != 0 ||
        req_int(stats_object, "armiesKilled", 0, 100000000,
                &stats->armies_killed, context, err, err_size) != 0 ||
        req_int(stats_object, "armiesLost", 0, 100000000,
                &stats->armies_lost, context, err, err_size) != 0) {
        return -1;
    }
    return 0;
}

static int read_seat(const JsonValue *object, struct Seat *seat,
                     int territory_card_count, int adventure_card_count,
                     const char *context, char *err, size_t err_size)
{
    int count;

    if (req_string(object, "user", seat->username, sizeof(seat->username),
                   context, err, err_size) != 0 ||
        req_int(object, "faction", 0, MAP_MAX_FACTIONS - 1,
                &seat->faction, context, err, err_size) != 0 ||
        req_bool(object, "alive", &seat->alive, context,
                 err, err_size) != 0 ||
        read_int_array(object, "hand", seat->hand, GAME_MAX_CARDS, 0,
                       territory_card_count - 1, &count, context,
                       err, err_size) != 0) {
        return -1;
    }
    seat->hand_count = count;
    if (read_int_array(object, "adventureHand", seat->adventure_hand,
                       GAME_ADVENTURE_HAND_MAX, 0,
                       adventure_card_count - 1, &count, context,
                       err, err_size) != 0) {
        return -1;
    }
    seat->adventure_hand_count = count;
    {
        const JsonValue *auto_values = json_get(object, "adventureAuto");
        const JsonValue *auto_n_values = json_get(object, "adventureAutoN");
        const JsonValue *sites = json_get(object, "siteClaimed");
        const JsonValue *bonus = json_get(object, "attackBonus");
        size_t k;
        memset(seat->adventure_auto, 0, sizeof(seat->adventure_auto));
        memset(seat->adventure_auto_n, 0,
               sizeof(seat->adventure_auto_n));
        memset(seat->site_claimed, 0, sizeof(seat->site_claimed));
        seat->attack_bonus = 0;
        if (bonus != NULL) {
            long long value;
            if (json_get_int(bonus, &value) != 0 ||
                value < 0 || value > 1000) {
                set_error(err, err_size, "%s: bad attack bonus", context);
                return -1;
            }
            seat->attack_bonus = (int)value;
        }
        if (sites != NULL) {
            if (sites->type != JSON_ARRAY ||
                json_count(sites) > GAME_SITE_MASK_WORDS) {
                set_error(err, err_size, "%s: bad site mask", context);
                return -1;
            }
            for (k = 0; k < json_count(sites); k++) {
                long long value;
                if (json_get_int(json_at(sites, k), &value) != 0 ||
                    value < 0 || value > 0xffffffffLL) {
                    set_error(err, err_size, "%s: bad site mask", context);
                    return -1;
                }
                seat->site_claimed[k] = (unsigned int)value;
            }
        }
        if (auto_values != NULL) {
            if (auto_values->type != JSON_ARRAY ||
                json_count(auto_values) > (size_t)count) {
                set_error(err, err_size, "%s: bad standing orders",
                          context);
                return -1;
            }
            for (k = 0; k < json_count(auto_values); k++) {
                long long value;
                if (json_get_int(json_at(auto_values, k), &value) != 0 ||
                    value < 0 || value > 2) {
                    set_error(err, err_size, "%s: bad standing order",
                              context);
                    return -1;
                }
                seat->adventure_auto[k] = (unsigned char)value;
            }
        }
        if (auto_n_values != NULL) {
            if (auto_n_values->type != JSON_ARRAY ||
                json_count(auto_n_values) > (size_t)count) {
                set_error(err, err_size, "%s: bad standing thresholds",
                          context);
                return -1;
            }
            for (k = 0; k < json_count(auto_n_values); k++) {
                long long value;
                if (json_get_int(json_at(auto_n_values, k), &value) != 0 ||
                    value < 0 || value > 3) {
                    set_error(err, err_size, "%s: bad standing threshold",
                              context);
                    return -1;
                }
                seat->adventure_auto_n[k] = (unsigned char)value;
            }
        }
    }
    if (req_int(object, "leaders", 0, 1000, &seat->leaders_in_play,
                context, err, err_size) != 0 ||
        req_int(object, "missions", 0, 1000, &seat->missions_done,
                context, err, err_size) != 0 ||
        req_int(object, "lastSeen", 0, 1000000000,
                &seat->last_seen_version, context, err, err_size) != 0 ||
        read_stats(object, &seat->stats, context, err, err_size) != 0) {
        return -1;
    }
    return 0;
}

static int read_territory(const JsonValue *object,
                          struct TerritoryState *territory,
                          int player_count, const char *context,
                          char *err, size_t err_size)
{
    int neutral;

    if (req_int(object, "owner", -1, player_count - 1, &territory->owner,
                context, err, err_size) != 0 ||
        req_int(object, "armies", 0, 100000000, &territory->armies,
                context, err, err_size) != 0 ||
        req_int(object, "leader", 0, player_count, &territory->leader_of,
                context, err, err_size) != 0 ||
        req_bool(object, "neutral", &neutral, context,
                 err, err_size) != 0) {
        return -1;
    }
    territory->neutral = neutral;
    return 0;
}

static int read_event(const JsonValue *object, struct Event *event,
                      int player_count, int territory_count,
                      int territory_card_count, int adventure_card_count,
                      const char *context, char *err, size_t err_size)
{
    const JsonValue *battles;
    int battle_count;
    int event_card_max = territory_card_count > adventure_card_count ?
        territory_card_count : adventure_card_count;
    size_t i;

    if (req_int(object, "kind", 0, EVENT_SEA_CLOSED, &event->kind,
                context, err, err_size) != 0) {
        return -1;
    }
    {
        const JsonValue *version_value = json_get(object, "version");
        long long version = 0;
        if (version_value != NULL &&
            (json_get_int(version_value, &version) != 0 ||
             version < 0 || version > 1000000000LL)) {
            set_error(err, err_size, "%s: bad event version", context);
            return -1;
        }
        event->version = (int)version;
    }
    if (req_int(object, "player", -1, player_count - 1, &event->player,
                context, err, err_size) != 0 ||
        req_int(object, "from", -1, territory_count - 1, &event->from,
                context, err, err_size) != 0 ||
        req_int(object, "to", -1, territory_count - 1, &event->to,
                context, err, err_size) != 0 ||
        req_int(object, "count", -100000000, 100000000, &event->count,
                context, err, err_size) != 0 ||
        req_int(object, "cardA", -1,
                event_card_max > 0 ? event_card_max - 1 : 0,
                &event->card_a, context, err, err_size) != 0 ||
        req_int(object, "cardB", -1, territory_card_count - 1,
                &event->card_b, context, err, err_size) != 0 ||
        req_int(object, "cardC", -1, territory_card_count - 1,
                &event->card_c, context, err, err_size) != 0 ||
        req_int(object, "setBonus", -1000000, 1000000,
                &event->set_bonus, context, err, err_size) != 0 ||
        req_int(object, "battleCount", 0, GAME_MAX_BLITZ_ROUNDS,
                &battle_count, context, err, err_size) != 0 ||
        req_bool(object, "truncated", &event->truncated, context,
                 err, err_size) != 0 ||
        req_string(object, "text", event->text, sizeof(event->text),
                   context, err, err_size) != 0) {
        return -1;
    }
    event->battle_count = battle_count;
    if (req_array(object, "battles", GAME_MAX_BLITZ_ROUNDS, &battles,
                  context, err, err_size) != 0) {
        return -1;
    }
    if (json_count(battles) != (size_t)battle_count) {
        set_error(err, err_size,
                  "%s: \"battleCount\" does not match \"battles\"", context);
        return -1;
    }
    for (i = 0; i < (size_t)battle_count; i++) {
        const JsonValue *battle = json_at(battles, i);
        const JsonValue *attacker;
        const JsonValue *defender;
        struct DiceBattle *out = &event->battles[i];
        int attacker_count, defender_count;
        int k;

        if (battle == NULL || battle->type != JSON_OBJECT) {
            set_error(err, err_size, "%s: battle %zu is not an object",
                      context, i);
            return -1;
        }
        if (req_int(battle, "attackerCount", 0, 3, &attacker_count,
                    context, err, err_size) != 0 ||
            req_int(battle, "defenderCount", 0, 2, &defender_count,
                    context, err, err_size) != 0) {
            return -1;
        }
        if (attacker_count < 1 || defender_count < 1) {
            set_error(err, err_size, "%s: battle %zu has no dice", context,
                      i);
            return -1;
        }
        out->attacker_count = attacker_count;
        out->defender_count = defender_count;
        if (req_array(battle, "attacker", 3, &attacker, context,
                      err, err_size) != 0 ||
            req_array(battle, "defender", 2, &defender, context,
                      err, err_size) != 0) {
            return -1;
        }
        if (json_count(attacker) != (size_t)attacker_count ||
            json_count(defender) != (size_t)defender_count) {
            set_error(err, err_size, "%s: battle %zu die count mismatch",
                      context, i);
            return -1;
        }
        for (k = 0; k < attacker_count; k++) {
            long long value;
            if (json_get_int(json_at(attacker, k), &value) != 0 ||
                value < 1 || value > 6) {
                set_error(err, err_size, "%s: bad attacker die", context);
                return -1;
            }
            out->attacker[k] = (int)value;
        }
        for (k = 0; k < defender_count; k++) {
            long long value;
            if (json_get_int(json_at(defender, k), &value) != 0 ||
                value < 1 || value > 6) {
                set_error(err, err_size, "%s: bad defender die", context);
                return -1;
            }
            out->defender[k] = (int)value;
        }
        if (req_int(battle, "attackerBonus", 0, 100,
                    &out->attacker_bonus, context, err, err_size) != 0 ||
            req_int(battle, "defenderBonus", 0, 100,
                    &out->defender_bonus, context, err, err_size) != 0 ||
            req_int(battle, "attackerLosses", 0, 100,
                    &out->attacker_losses, context, err, err_size) != 0 ||
            req_int(battle, "defenderLosses", 0, 100,
                    &out->defender_losses, context, err, err_size) != 0) {
            return -1;
        }
    }
    return 0;
}

static void pack_basename(const struct Map *map, char *out, size_t out_size)
{
    const char *base = strrchr(map->dir, '/');
    size_t length;
    base = (base != NULL) ? base + 1 : map->dir;
    length = strlen(base);
    if (length >= out_size) {
        length = out_size - 1;
    }
    memcpy(out, base, length);
    out[length] = '\0';
}

static int validate_loaded(const struct Game *g, const struct Map *map,
                           char *err, size_t err_size)
{
    int seen[GAME_MAX_CARDS];
    int seen_adventure[GAME_MAX_CARDS];
    int i, j;
    int total;
    int leaders;

    if (g->player_count < 0 || g->player_count > GAME_MAX_PLAYERS) {
        set_error(err, err_size, "invalid player count");
        return -1;
    }
    if (g->territory_count != map->territoryCount) {
        set_error(err, err_size, "territory count does not match the map");
        return -1;
    }
    if (g->status == GAME_OPEN) {
        if (g->phase != PHASE_LOBBY || g->current_player != -1) {
            set_error(err, err_size, "open game is not in the lobby");
            return -1;
        }
        for (i = 0; i < g->territory_count; i++) {
            if (g->territories[i].owner != -1) {
                set_error(err, err_size,
                          "open game has a claimed territory");
                return -1;
            }
        }
        if (g->deck_count != 0 || g->discard_count != 0 ||
            g->adventure_deck_count != 0 ||
            g->adventure_discard_count != 0) {
            set_error(err, err_size, "open game has cards in its decks");
            return -1;
        }
        for (i = 0; i < g->player_count; i++) {
            if (g->seats[i].hand_count != 0 ||
                g->seats[i].adventure_hand_count != 0) {
                set_error(err, err_size, "open game has cards in a hand");
                return -1;
            }
        }
        return 0;
    }
    if (g->max_players < 2 || g->max_players > GAME_MAX_PLAYERS) {
        set_error(err, err_size, "invalid maximum player count");
        return -1;
    }
    if (g->player_count < 2 || g->player_count > g->max_players) {
        set_error(err, err_size, "games need 2 to %d players", g->max_players);
        return -1;
    }
    for (i = 0; i < g->player_count; i++) {
        if (g->seats[i].username[0] == '\0') {
            set_error(err, err_size, "seat %d has no user name", i);
            return -1;
        }
        if (g->seats[i].faction < 0 ||
            g->seats[i].faction >= map->factionCount) {
            set_error(err, err_size, "seat %d has an invalid faction", i);
            return -1;
        }
    }
    if (g->status == GAME_RUNNING) {
        if (g->phase < PHASE_SETUP_CLAIM || g->phase > PHASE_FORTIFY) {
            set_error(err, err_size, "running game has an invalid phase");
            return -1;
        }
        if (g->current_player < 0 || g->current_player >= g->player_count ||
            !g->seats[g->current_player].alive) {
            set_error(err, err_size, "running game has no live current player");
            return -1;
        }
        if (g->winner != -1) {
            set_error(err, err_size, "running game already has a winner");
            return -1;
        }
    } else if (g->status == GAME_FINISHED) {
        if (g->phase != PHASE_FINISHED) {
            set_error(err, err_size, "finished game is in the wrong phase");
            return -1;
        }
        if (g->winner < -1 || g->winner >= g->player_count) {
            set_error(err, err_size, "finished game has an invalid winner");
            return -1;
        }
    } else {
        set_error(err, err_size, "unknown game status");
        return -1;
    }
    if (g->version < 1 || g->turn < 0) {
        set_error(err, err_size, "invalid version or turn");
        return -1;
    }

    for (i = 0; i < g->territory_count; i++) {
        const struct TerritoryState *t = &g->territories[i];
        if (t->owner < -1 || t->owner >= g->player_count) {
            set_error(err, err_size, "territory %d has an invalid owner", i);
            return -1;
        }
        if (t->leader_of < 0 || t->leader_of > g->player_count) {
            set_error(err, err_size, "territory %d has an invalid leader", i);
            return -1;
        }
        if (t->leader_of > 0 && t->owner != t->leader_of - 1) {
            set_error(err, err_size,
                      "territory %d has a leader but a foreign owner", i);
            return -1;
        }
        if (t->neutral) {
            if (t->owner != -1 || t->armies < 1) {
                set_error(err, err_size, "territory %d has bad neutral state",
                          i);
                return -1;
            }
        } else if (t->owner == -1) {
            if (g->phase >= PHASE_SETUP_PLACE && g->phase != PHASE_FINISHED) {
                set_error(err, err_size, "territory %d is still unclaimed", i);
                return -1;
            }
        } else if (t->armies < 1 &&
                   !(g->phase == PHASE_CONQUEST_MOVE &&
                     i == g->pending_to) &&
                   g->phase != PHASE_FINISHED) {
            set_error(err, err_size,
                      "territory %d is owned but has no armies", i);
            return -1;
        }
    }

    if (g->phase == PHASE_CONQUEST_MOVE) {
        if (g->pending_from < 0 || g->pending_to < 0 ||
            g->pending_from >= g->territory_count ||
            g->pending_to >= g->territory_count ||
            g->pending_min < 1 ||
            g->pending_min > g->pending_max ||
            g->territories[g->pending_from].owner != g->current_player ||
            g->territories[g->pending_to].owner != g->current_player ||
            g->territories[g->pending_from].armies <= g->pending_max ||
            g->territories[g->pending_to].armies < 0) {
            set_error(err, err_size, "invalid pending conquest");
            return -1;
        }
    } else if (g->pending_from != -1) {
        set_error(err, err_size, "game has a stale pending conquest");
        return -1;
    }
    if (g->must_trade) {
        int a, b, c;
        if (g->current_player < 0 ||
            g->seats[g->current_player].hand_count < 5 ||
            !game_find_card_set(g, g->current_player, &a, &b, &c)) {
            set_error(err, err_size, "forced trade without a valid set");
            return -1;
        }
    }

    memset(seen, 0, sizeof(seen));
    total = 0;
    for (i = 0; i < g->deck_count; i++) {
        int c = g->deck[i];
        if (c < 0 || c >= map->territoryCardCount || seen[c]) {
            set_error(err, err_size, "invalid deck card");
            return -1;
        }
        seen[c] = 1;
        total++;
    }
    for (i = 0; i < g->discard_count; i++) {
        int c = g->discard[i];
        if (c < 0 || c >= map->territoryCardCount || seen[c]) {
            set_error(err, err_size, "invalid discard card");
            return -1;
        }
        seen[c] = 1;
        total++;
    }
    for (i = 0; i < g->player_count; i++) {
        for (j = 0; j < g->seats[i].hand_count; j++) {
            int c = g->seats[i].hand[j];
            if (c < 0 || c >= map->territoryCardCount || seen[c]) {
                set_error(err, err_size, "invalid hand card");
                return -1;
            }
            seen[c] = 1;
            total++;
        }
    }
    if (total != map->territoryCardCount) {
        set_error(err, err_size, "territory cards are not conserved");
        return -1;
    }
    memset(seen_adventure, 0, sizeof(seen_adventure));
    total = 0;
    for (i = 0; i < g->adventure_deck_count; i++) {
        int c = g->adventure_deck[i];
        if (c < 0 || c >= map->adventureCardCount || seen_adventure[c]) {
            set_error(err, err_size, "invalid adventure deck card");
            return -1;
        }
        seen_adventure[c] = 1;
        total++;
    }
    for (i = 0; i < g->adventure_discard_count; i++) {
        int c = g->adventure_discard[i];
        if (c < 0 || c >= map->adventureCardCount ||
            seen_adventure[c]) {
            set_error(err, err_size, "invalid adventure discard card");
            return -1;
        }
        seen_adventure[c] = 1;
        total++;
    }
    for (i = 0; i < g->player_count; i++) {
        for (j = 0; j < g->seats[i].adventure_hand_count; j++) {
            int c = g->seats[i].adventure_hand[j];
            if (c < 0 || c >= map->adventureCardCount ||
                seen_adventure[c]) {
                set_error(err, err_size, "invalid adventure hand card");
                return -1;
            }
            seen_adventure[c] = 1;
            total++;
        }
    }
    if (total != map->adventureCardCount) {
        set_error(err, err_size, "adventure cards are not conserved");
        return -1;
    }

    for (i = 0; i < g->player_count; i++) {
        leaders = 0;
        for (j = 0; j < g->territory_count; j++) {
            if (g->territories[j].leader_of == i + 1) {
                leaders++;
            }
        }
        if (leaders != g->seats[i].leaders_in_play) {
            set_error(err, err_size,
                      "leader count for seat %d does not match the board",
                      i);
            return -1;
        }
    }
    return 0;
}

int game_from_json(const char *json, struct Game *g, const struct Map *map,
                   char *err, size_t err_size)
{
    JsonValue *root = NULL;
    struct Game *tmp = NULL;
    const JsonValue *players;
    const JsonValue *territories;
    const JsonValue *decks;
    const JsonValue *log;
    const JsonValue *pending;
    const JsonValue *rng;
    const JsonValue *setup;
    const JsonValue *reinforce;
    const JsonValue *flags;
    char status_name[32];
    char phase_name[32];
    char start_mode_name[16];
    char pack_name[MAP_NAME_SIZE];
    int format = 0;
    int player_count;
    int territory_count;
    int deck_count, discard_count;
    int adventure_count, adventure_discard_count;
    long long rng_hi, rng_lo;
    int i;
    char context[128];

    if (err != NULL && err_size > 0) {
        err[0] = '\0';
    }
    if (json == NULL || g == NULL || map == NULL) {
        set_error(err, err_size, "invalid serializer arguments");
        return -1;
    }
    root = json_parse(json, err, err_size);
    if (root == NULL) {
        return -1;
    }
    if (root->type != JSON_OBJECT) {
        set_error(err, err_size, "game file must contain a JSON object");
        json_free(root);
        return -1;
    }
    tmp = (struct Game *)calloc(1, sizeof(*tmp));
    if (tmp == NULL) {
        set_error(err, err_size, "out of memory loading game");
        json_free(root);
        return -1;
    }
    if (req_int(root, "format", 1, 1000, &format, "game",
                err, err_size) != 0 ||
        format != GAME_SERIALIZE_FORMAT) {
        if (err != NULL && err[0] == '\0') {
            set_error(err, err_size, "unsupported game format %d", format);
        }
        goto fail;
    }
    if (req_int(root, "id", -1000000000, 1000000000, &tmp->id, "game",
                err, err_size) != 0 ||
        req_string(root, "name", tmp->name, sizeof(tmp->name), "game",
                   err, err_size) != 0 ||
        req_string(root, "mapPack", tmp->map_pack,
                   sizeof(tmp->map_pack), "game",
                   err, err_size) != 0 ||
        req_string(root, "passwordHash", tmp->password_hash,
                   sizeof(tmp->password_hash), "game",
                   err, err_size) != 0 ||
        req_string(root, "host", tmp->host, sizeof(tmp->host), "game",
                   err, err_size) != 0 ||
        req_string(root, "status", status_name, sizeof(status_name), "game",
                   err, err_size) != 0 ||
        req_string(root, "phase", phase_name, sizeof(phase_name), "game",
                   err, err_size) != 0) {
        goto fail;
    }
    tmp->status = status_from_name(status_name);
    tmp->phase = phase_from_name(phase_name);
    if (tmp->status < 0 || tmp->phase < 0) {
        set_error(err, err_size, "unknown game status or phase");
        goto fail;
    }
    if (req_int(root, "version", 1, 1000000000, &tmp->version, "game",
                err, err_size) != 0 ||
        req_int(root, "maxPlayers", 2, GAME_MAX_PLAYERS,
                &tmp->max_players, "game", err, err_size) != 0 ||
        req_int(root, "playerCount", 0, GAME_MAX_PLAYERS,
                &player_count, "game", err, err_size) != 0 ||
        req_int(root, "turn", 0, 1000000000, &tmp->turn, "game",
                err, err_size) != 0 ||
        req_int(root, "current", -1, GAME_MAX_PLAYERS - 1,
                &tmp->current_player, "game", err, err_size) != 0 ||
        req_int(root, "winner", -1, GAME_MAX_PLAYERS - 1,
                &tmp->winner, "game", err, err_size) != 0) {
        goto fail;
    }
    tmp->player_count = player_count;
    {
        long long created, updated;
        if (json_get_int(json_get(root, "created"), &created) != 0 ||
            json_get_int(json_get(root, "updated"), &updated) != 0) {
            set_error(err, err_size, "missing or invalid timestamps");
            goto fail;
        }
        if (created < 0 || created > 1000000000000LL ||
            updated < 0 || updated > 1000000000000LL) {
            set_error(err, err_size, "timestamps are out of range");
            goto fail;
        }
        tmp->created = (time_t)created;
        tmp->updated = (time_t)updated;
    }

    if (req_object(root, "flags", &flags, "game", err, err_size) != 0 ||
        req_bool(flags, "conquered", &tmp->conquered_this_turn, "flags",
                 err, err_size) != 0 ||
        req_bool(flags, "cardsTraded", &tmp->cards_traded_this_turn,
                 "flags", err, err_size) != 0 ||
        req_bool(flags, "fortified", &tmp->fortified_this_turn, "flags",
                 err, err_size) != 0 ||
        req_bool(flags, "mustTrade", &tmp->must_trade, "flags",
                 err, err_size) != 0) {
        goto fail;
    }
    {
        const JsonValue *v;
        long long n;
        tmp->must_discard_adventure = 0;
        tmp->extra_fortifies = 0;
        tmp->territories_conquered_this_turn = 0;
        v = json_get(flags, "mustDiscardAdventure");
        if (v != NULL) {
            int b = 0;
            if (json_get_bool(v, &b) != 0) {
                set_error(err, err_size, "flags: bad discard flag");
                goto fail;
            }
            tmp->must_discard_adventure = b;
        }
        v = json_get(flags, "extraFortify");
        if (v != NULL) {
            if (json_get_int(v, &n) != 0 || n < 0 || n > 100) {
                set_error(err, err_size, "flags: bad extra fortify");
                goto fail;
            }
            tmp->extra_fortifies = (int)n;
        }
        v = json_get(flags, "conqueredThisTurn");
        if (v != NULL) {
            if (json_get_int(v, &n) != 0 || n < 0 || n > 1000000) {
                set_error(err, err_size, "flags: bad conquest count");
                goto fail;
            }
            tmp->territories_conquered_this_turn = (int)n;
        }
    }
    {
        const JsonValue *closed = json_get(root, "closedLinks");
        size_t k;
        memset(tmp->link_closed_by, 0, sizeof(tmp->link_closed_by));
        if (closed != NULL) {
            if (closed->type != JSON_ARRAY ||
                json_count(closed) > (size_t)map->linkCount ||
                json_count(closed) > MAP_MAX_LINKS) {
                set_error(err, err_size, "game: bad closedLinks");
                goto fail;
            }
            for (k = 0; k < json_count(closed); k++) {
                long long value;
                if (json_get_int(json_at(closed, k), &value) != 0 ||
                    value < 0 || value > tmp->player_count) {
                    set_error(err, err_size, "game: bad closed link");
                    goto fail;
                }
                tmp->link_closed_by[k] = (int)value;
            }
        }
    }
    {
        const JsonValue *kick = json_get(root, "kick");
        tmp->kick_target = -1;
        tmp->kick_votes = 0;
        tmp->kick_started = 0;
        if (kick != NULL) {
            long long started;
            int target = -1;
            int votes = 0;
            if (kick->type != JSON_OBJECT ||
                req_int(kick, "target", -1, tmp->player_count - 1,
                        &target, "kick", err, err_size) != 0 ||
                req_int(kick, "votes", 0, 1 << tmp->player_count,
                        &votes, "kick", err, err_size) != 0) {
                goto fail;
            }
            if (json_get_int(json_get(kick, "started"), &started) != 0 ||
                started < 0) {
                set_error(err, err_size, "kick: bad start time");
                goto fail;
            }
            tmp->kick_target = target;
            tmp->kick_votes = (unsigned int)votes;
            tmp->kick_started = (time_t)started;
        }
    }
    if (req_object(root, "setup", &setup, "game", err, err_size) != 0) {
        goto fail;
    }
    {
        int pool_count;
        if (read_int_array(setup, "pool", tmp->setup_pool,
                           GAME_MAX_PLAYERS, 0, 100000000, &pool_count,
                           "setup", err, err_size) != 0) {
            goto fail;
        }
        if (pool_count > tmp->player_count) {
            pool_count = tmp->player_count;
        }
        if (req_int(setup, "batchRemaining", 0, 100000000,
                    &tmp->setup_batch_remaining, "setup",
                    err, err_size) != 0 ||
            req_int(setup, "batchSize", 1, 100000000,
                    &tmp->setup_batch_size, "setup",
                    err, err_size) != 0 ||
            req_string(setup, "startMode", start_mode_name,
                       sizeof(start_mode_name), "setup",
                       err, err_size) != 0 ||
            req_bool(setup, "neutral", &tmp->neutral_enabled, "setup",
                     err, err_size) != 0) {
            goto fail;
        }
        if (strcmp(start_mode_name, "deal") == 0) {
            tmp->start_mode = GAME_START_DEAL;
        } else if (strcmp(start_mode_name, "draft") == 0) {
            tmp->start_mode = GAME_START_DRAFT;
        } else {
            set_error(err, err_size, "unknown start mode");
            goto fail;
        }
    }
    if (req_object(root, "reinforce", &reinforce, "game",
                   err, err_size) != 0 ||
        req_int(reinforce, "territories", 0, 100000000,
                &tmp->reinforce_info.territories, "reinforce",
                err, err_size) != 0 ||
        req_int(reinforce, "base", 0, 100000000,
                &tmp->reinforce_info.base, "reinforce",
                err, err_size) != 0 ||
        req_int(reinforce, "regions", 0, 100000000,
                &tmp->reinforce_info.region_bonus, "reinforce",
                err, err_size) != 0 ||
        req_int(reinforce, "strongholds", 0, 100000000,
                &tmp->reinforce_info.stronghold_bonus, "reinforce",
                err, err_size) != 0 ||
        req_int(reinforce, "cards", 0, 100000000,
                &tmp->reinforce_info.card_bonus, "reinforce",
                err, err_size) != 0 ||
        req_int(reinforce, "total", 0, 100000000,
                &tmp->reinforce_info.total, "reinforce",
                err, err_size) != 0 ||
        req_int(reinforce, "regionsHeld", 0, 100000000,
                &tmp->reinforce_info.regions_held, "reinforce",
                err, err_size) != 0 ||
        req_int(reinforce, "strongholdsHeld", 0, 100000000,
                &tmp->reinforce_info.strongholds_held, "reinforce",
                err, err_size) != 0 ||
        req_int(reinforce, "pending", 0, 100000000,
                &tmp->reinforcements_pending, "reinforce",
                err, err_size) != 0) {
        goto fail;
    }
    {
        const JsonValue *deferred = json_get(reinforce, "deferred");
        size_t k;
        memset(tmp->deferred_card_bonus, 0,
               sizeof(tmp->deferred_card_bonus));
        if (deferred != NULL && deferred->type == JSON_ARRAY) {
            if (json_count(deferred) > (size_t)tmp->player_count) {
                set_error(err, err_size, "reinforce: bad deferred bonuses");
                goto fail;
            }
            for (k = 0; k < json_count(deferred); k++) {
                long long value;
                if (json_get_int(json_at(deferred, k), &value) != 0 ||
                    value < 0 || value > 100000000) {
                    set_error(err, err_size,
                              "reinforce: bad deferred bonus");
                    goto fail;
                }
                tmp->deferred_card_bonus[k] = (int)value;
            }
        } else if (deferred != NULL && deferred->type == JSON_INT) {
            long long value;
            if (json_get_int(deferred, &value) != 0 ||
                value < 0 || value > 100000000) {
                set_error(err, err_size, "reinforce: bad deferred bonus");
                goto fail;
            }
            if (tmp->current_player >= 0 &&
                tmp->current_player < GAME_MAX_PLAYERS) {
                tmp->deferred_card_bonus[tmp->current_player] = (int)value;
            }
        } else if (deferred != NULL) {
            set_error(err, err_size, "reinforce: bad deferred bonus");
            goto fail;
        }
    }
    if (req_object(root, "pending", &pending, "game",
                   err, err_size) != 0) {
        goto fail;
    }
    {
        int active;
        if (req_bool(pending, "active", &active, "pending",
                     err, err_size) != 0) {
            goto fail;
        }
        if (active) {
            if (req_int(pending, "from", 0, MAP_MAX_TERRITORIES - 1,
                        &tmp->pending_from, "pending",
                        err, err_size) != 0 ||
                req_int(pending, "to", 0, MAP_MAX_TERRITORIES - 1,
                        &tmp->pending_to, "pending",
                        err, err_size) != 0 ||
                req_int(pending, "min", 0, 100000000,
                        &tmp->pending_min, "pending",
                        err, err_size) != 0 ||
                req_int(pending, "max", 0, 100000000,
                        &tmp->pending_max, "pending",
                        err, err_size) != 0) {
                goto fail;
            }
        } else {
            tmp->pending_from = -1;
            tmp->pending_to = -1;
        }
    }
    if (req_object(root, "rng", &rng, "game", err, err_size) != 0 ||
        json_get_int(json_get(rng, "hi"), &rng_hi) != 0 ||
        json_get_int(json_get(rng, "lo"), &rng_lo) != 0 ||
        rng_hi < 0 || rng_hi > 4294967295LL ||
        rng_lo < 0 || rng_lo > 4294967295LL) {
        if (err != NULL && err[0] == '\0') {
            set_error(err, err_size, "invalid RNG state");
        }
        goto fail;
    }
    tmp->rng.state = ((uint64_t)rng_hi << 32) |
                     (uint64_t)rng_lo;

    if (req_array(root, "players", GAME_MAX_PLAYERS, &players,
                  "game", err, err_size) != 0) {
        goto fail;
    }
    if (json_count(players) != (size_t)tmp->player_count) {
        set_error(err, err_size, "player array length does not match count");
        goto fail;
    }
    for (i = 0; i < tmp->player_count; i++) {
        snprintf(context, sizeof(context), "player %d", i);
        if (read_seat(json_at(players, i), &tmp->seats[i],
                      map->territoryCardCount, map->adventureCardCount,
                      context, err, err_size) != 0) {
            goto fail;
        }
    }

    if (req_array(root, "territories", MAP_MAX_TERRITORIES, &territories,
                  "game", err, err_size) != 0) {
        goto fail;
    }
    territory_count = (int)json_count(territories);
    if (territory_count != map->territoryCount) {
        set_error(err, err_size, "territory array length does not match map");
        goto fail;
    }
    tmp->territory_count = territory_count;
    for (i = 0; i < territory_count; i++) {
        snprintf(context, sizeof(context), "territory %d", i);
        if (read_territory(json_at(territories, i), &tmp->territories[i],
                           tmp->player_count, context,
                           err, err_size) != 0) {
            goto fail;
        }
    }

    if (req_object(root, "decks", &decks, "game",
                   err, err_size) != 0) {
        goto fail;
    }
    if (read_int_array(decks, "territory", tmp->deck, GAME_MAX_CARDS, 0,
                       map->territoryCardCount - 1, &deck_count,
                       "decks", err, err_size) != 0 ||
        read_int_array(decks, "discard", tmp->discard, GAME_MAX_CARDS, 0,
                       map->territoryCardCount - 1, &discard_count,
                       "decks", err, err_size) != 0 ||
        read_int_array(decks, "adventure", tmp->adventure_deck,
                       GAME_MAX_CARDS, 0, map->adventureCardCount - 1,
                       &adventure_count, "decks",
                       err, err_size) != 0 ||
        read_int_array(decks, "adventureDiscard",
                       tmp->adventure_discard, GAME_MAX_CARDS, 0,
                       map->adventureCardCount - 1,
                       &adventure_discard_count, "decks",
                       err, err_size) != 0) {
        goto fail;
    }
    tmp->deck_count = deck_count;
    tmp->discard_count = discard_count;
    tmp->adventure_deck_count = adventure_count;
    tmp->adventure_discard_count = adventure_discard_count;

    if (req_array(root, "log", GAME_LOG_RING, &log, "game",
                  err, err_size) != 0) {
        goto fail;
    }
    tmp->log.count = (int)json_count(log);
    for (i = 0; i < tmp->log.count; i++) {
        snprintf(context, sizeof(context), "log event %d", i);
        if (read_event(json_at(log, i), &tmp->log.events[i],
                       tmp->player_count, tmp->territory_count,
                       map->territoryCardCount, map->adventureCardCount,
                       context, err, err_size) != 0) {
            goto fail;
        }
    }
    tmp->log.next = tmp->log.count % GAME_LOG_RING;
    if (tmp->log.count == GAME_LOG_RING) {
        tmp->log.next = 0;
    }

    tmp->map = map;
    tmp->end_hook = NULL;
    tmp->end_hook_user = NULL;
    pack_basename(map, pack_name, sizeof(pack_name));
    if (strcmp(tmp->map_pack, pack_name) != 0) {
        set_error(err, err_size,
                  "game was saved for map pack \"%s\", not \"%s\"",
                  tmp->map_pack, pack_name);
        goto fail;
    }
    if (validate_loaded(tmp, map, err, err_size) != 0) {
        goto fail;
    }
    *g = *tmp;
    free(tmp);
    json_free(root);
    return 0;

fail:
    free(tmp);
    json_free(root);
    return -1;
}
