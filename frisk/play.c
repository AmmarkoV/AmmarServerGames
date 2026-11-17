#include "play.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "accounts.h"
#include "chat.h"
#include "frisk.h"
#include "engine/serialize.h"
#include "games.h"
#include "http.h"


#define FRISK_KICK_IDLE_DEFAULT (7L * 24L * 60L * 60L)

static long kick_idle_seconds(void)
{
    static long cached = -1;
    const char *text;
    long value;

    if (cached >= 0) {
        return cached;
    }
    text = getenv("FRISK_KICK_IDLE_SECONDS");
    value = (text != NULL && text[0] != '\0')
        ? strtol(text, NULL, 10) : FRISK_KICK_IDLE_DEFAULT;
    if (value < 0) {
        value = FRISK_KICK_IDLE_DEFAULT;
    }
    cached = value;
    return cached;
}

static void play_json_error_msg(struct AmmServer_DynamicRequest *rqst,
                                unsigned int status, const char *code,
                                const char *msg)
{
    struct FriskJson json;

    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 0);
    frisk_json_key_str(&json, "err", code);
    if (msg != NULL && msg[0] != '\0') {
        frisk_json_key_str(&json, "msg", msg);
    }
    frisk_json_end(&json);
    if (rqst != NULL) {
        rqst->headerResponse = status;
    }
}

static int kick_vote_count(const struct Game *game)
{
    int i;
    int count = 0;

    for (i = 0; i < game->player_count && i < 32; i++) {
        if (game->kick_votes & (1u << (unsigned int)i)) {
            count++;
        }
    }
    return count;
}

static int kick_vote_needed(const struct Game *game, int target)
{
    int alive = 0;
    int i;

    for (i = 0; i < game->player_count; i++) {
        if (game->seats[i].alive && i != target) {
            alive++;
        }
    }
    return alive / 2 + 1;
}

static void write_event(struct FriskJson *json, const struct Event *event)
{
    int i;

    frisk_json_array_object(json);
    frisk_json_key_str(json, "kind", game_event_name(event->kind));
    frisk_json_key_int(json, "version", event->version);
    frisk_json_key_int(json, "player", event->player);
    frisk_json_key_int(json, "from", event->from);
    frisk_json_key_int(json, "to", event->to);
    frisk_json_key_int(json, "count", event->count);
    if (event->kind == EVENT_TRADE_CARDS) {
        frisk_json_key_int(json, "cardA", event->card_a);
        frisk_json_key_int(json, "cardB", event->card_b);
        frisk_json_key_int(json, "cardC", event->card_c);
        frisk_json_key_int(json, "setBonus", event->set_bonus);
    }
    frisk_json_key_str(json, "text", event->text);
    if (event->battle_count > 0) {
        frisk_json_key_array(json, "battles");
        for (i = 0; i < event->battle_count &&
                    i < GAME_MAX_BLITZ_ROUNDS; i++) {
            const struct DiceBattle *battle = &event->battles[i];
            int k;
            frisk_json_array_object(json);
            frisk_json_key_array(json, "attacker");
            for (k = 0; k < battle->attacker_count; k++) {
                frisk_json_array_int(json, battle->attacker[k]);
            }
            frisk_json_array_end(json);
            frisk_json_key_array(json, "defender");
            for (k = 0; k < battle->defender_count; k++) {
                frisk_json_array_int(json, battle->defender[k]);
            }
            frisk_json_array_end(json);
            frisk_json_key_int(json, "attackerBonus",
                               battle->attacker_bonus);
            frisk_json_key_int(json, "defenderBonus",
                               battle->defender_bonus);
            frisk_json_key_int(json, "attackerLosses",
                               battle->attacker_losses);
            frisk_json_key_int(json, "defenderLosses",
                               battle->defender_losses);
            frisk_json_object_end(json);
        }
        frisk_json_array_end(json);
        if (event->truncated) {
            frisk_json_key_bool(json, "truncated", 1);
        }
    }
    frisk_json_object_end(json);
}

static const struct MapAdventureCard *adventure_card_for(
    const struct FriskGame *slot, int index)
{
    if (slot == NULL || slot->game.map == NULL || index < 0 ||
        index >= slot->game.map->adventureCardCount) {
        return NULL;
    }
    return &slot->game.map->adventureCards[index];
}

void *play_state_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    unsigned int id = 0;
    unsigned int since = 0;
    struct FriskGame *slot;
    int seat;
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username))) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_param_uint(rqst, "g", 1, 1000000000u, &id)) {
        frisk_json_error(rqst, 400, "game");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (_GETexists(rqst, "since") &&
        !frisk_param_uint(rqst, "since", 0, 1000000000u, &since)) {
        frisk_json_error(rqst, 400, "params");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    slot = games_find((int)id);
    if (slot == NULL) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (since != 0 && since == (unsigned int)slot->game.version) {
        frisk_json_begin(&json, rqst);
        frisk_json_key_int(&json, "ok", 1);
        frisk_json_key_bool(&json, "same", 1);
        frisk_json_key_int(&json, "version", slot->game.version);
        frisk_json_end(&json);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    seat = game_find_seat(&slot->game, username);
    if (game_view(&slot->game, seat, rqst->content,
                  rqst->MAXcontentSize) != 0) {
        frisk_json_error(rqst, 500, "overflow");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    rqst->contentSize = (unsigned long)strlen(rqst->content);
    rqst->headerResponse = 200;
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *play_log_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    unsigned int id = 0;
    unsigned int from = 0;
    unsigned int after = 0;
    unsigned int limit = 100;
    int has_after;
    struct FriskGame *slot;
    int count;
    int i;
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username))) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_param_uint(rqst, "g", 1, 1000000000u, &id)) {
        frisk_json_error(rqst, 400, "game");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    slot = games_find((int)id);
    if (slot == NULL) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    has_after = frisk_param_uint(rqst, "after", 0, 1000000000u,
                                 &after);
    if (_GETexists(rqst, "after") && !has_after) {
        frisk_json_error(rqst, 400, "params");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (_GETexists(rqst, "limit") &&
        !frisk_param_uint(rqst, "limit", 1, 100, &limit)) {
        frisk_json_error(rqst, 400, "params");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (has_after) {
        char *raw = NULL;
        size_t raw_size = 0;
        char error[256];
        int rc;

        error[0] = '\0';
        rc = games_read_log_after((int)id, (int)after, (int)limit,
                                  &raw, &raw_size, error, sizeof(error));
        if (rc < 0) {
            frisk_json_error(rqst, 500, "log");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        frisk_json_begin(&json, rqst);
        frisk_json_key_int(&json, "ok", 1);
        frisk_json_key_int(&json, "after", after);
        frisk_json_key_int(&json, "next", slot->game.version);
        frisk_json_key_array(&json, "events");
        if (rc == 0 && raw != NULL && raw_size > 0) {
            frisk_json_value_raw(&json, raw);
        } else if (rc == 1) {
            /* No durable log yet: fall back to the in-memory ring. */
            count = game_log_count(&slot->game);
            for (i = 0; i < count; i++) {
                const struct Event *event = game_log_event(&slot->game, i);
                if (event != NULL && event->version > (int)after) {
                    frisk_json_array_next(&json);
                    {
                        char event_json[32768];
                        if (game_event_to_json(event, event_json,
                                               sizeof(event_json)) == 0) {
                            frisk_json_value_raw(&json, event_json);
                        }
                    }
                }
            }
        }
        frisk_json_array_end(&json);
        frisk_json_end(&json);
        free(raw);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (_GETexists(rqst, "from") &&
        !frisk_param_uint(rqst, "from", 0, 1000000000u, &from)) {
        frisk_json_error(rqst, 400, "params");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    count = game_log_count(&slot->game);
    if (from > (unsigned int)count) {
        from = (unsigned int)count;
    }
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_int(&json, "from", from);
    frisk_json_key_int(&json, "next", count);
    frisk_json_key_array(&json, "events");
    for (i = (int)from; i < count; i++) {
        const struct Event *event = game_log_event(&slot->game, i);
        if (event != NULL) {
            write_event(&json, event);
        }
    }
    frisk_json_array_end(&json);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

static int post_uint_if_present(struct AmmServer_DynamicRequest *rqst,
                                const char *name, unsigned int minimum,
                                unsigned int maximum, unsigned int *out)
{
    if (!_POSTexists(rqst, name)) {
        *out = 0;
        return 1;
    }
    return frisk_post_uint(rqst, name, minimum, maximum, out);
}

void *play_action_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char type[32];
    unsigned int id = 0;
    struct FriskGame *slot;
    int seat;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    int event_count = 0;
    char error[256];
    struct FriskJson json;
    int rc;
    unsigned int value;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_post_uint(rqst, "g", 1, 1000000000u, &id) ||
        !frisk_post_str(rqst, "type", type, sizeof(type))) {
        frisk_json_error(rqst, 400, "game");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    slot = games_find((int)id);
    if (slot == NULL) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    seat = game_find_seat(&slot->game, username);
    if (seat < 0) {
        frisk_json_error(rqst, 403, "seat");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (strcmp(type, "seen") == 0) {
        slot->game.seats[seat].last_seen_version = slot->game.version;
        games_save(slot);
        frisk_json_begin(&json, rqst);
        frisk_json_key_int(&json, "ok", 1);
        frisk_json_key_int(&json, "version", slot->game.version);
        frisk_json_end(&json);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (strcmp(type, "kick_vote") == 0) {
        unsigned int target_value = 0;
        int target;
        int needed;
        int votes;
        unsigned int bit;
        const struct FriskProfile *profile;
        long idle;
        long required;
        char message[192];
        struct Action kick_action;
        struct Event kick_events[GAME_MAX_EVENTS_PER_ACTION];
        int kick_event_count = 0;
        char kick_error[256];

        if (!frisk_post_uint(rqst, "target", 0, GAME_MAX_PLAYERS - 1,
                             &target_value)) {
            play_json_error_msg(rqst, 400, "params",
                                "a target seat is required");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        target = (int)target_value;
        if (slot->game.status != GAME_RUNNING ||
            (slot->game.phase != PHASE_REINFORCE &&
             slot->game.phase != PHASE_ATTACK &&
             slot->game.phase != PHASE_FORTIFY)) {
            play_json_error_msg(rqst, 409, "phase",
                                "kick votes are only available during a "
                                "normal turn");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        if (!slot->game.seats[seat].alive) {
            play_json_error_msg(rqst, 403, "seat", "you are out of the game");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        if (target < 0 || target >= slot->game.player_count ||
            target == seat || !slot->game.seats[target].alive) {
            play_json_error_msg(rqst, 400, "target",
                                "choose another living player");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        profile = accounts_profile(slot->game.seats[target].username);
        required = kick_idle_seconds();
        idle = (profile != NULL && profile->last_seen > 0)
            ? (long)(time(NULL) - profile->last_seen) : required;
        if (idle < required) {
            snprintf(message, sizeof(message),
                     "%s has been active recently (%lds idle, %lds required)",
                     slot->game.seats[target].username, idle, required);
            play_json_error_msg(rqst, 409, "idle", message);
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        bit = 1u << (unsigned int)seat;
        needed = kick_vote_needed(&slot->game, target);
        if (slot->game.kick_target < 0) {
            slot->game.kick_target = target;
            slot->game.kick_votes = bit;
            slot->game.kick_started = time(NULL);
        } else if (slot->game.kick_target != target) {
            play_json_error_msg(rqst, 409, "vote",
                                "another kick vote is already in progress");
            pthread_mutex_unlock(&friskLock);
            return 0;
        } else if (slot->game.kick_votes & bit) {
            play_json_error_msg(rqst, 409, "voted",
                                "you already voted to remove that player");
            pthread_mutex_unlock(&friskLock);
            return 0;
        } else {
            slot->game.kick_votes |= bit;
        }
        votes = kick_vote_count(&slot->game);
        if (votes >= needed) {
            memset(&kick_action, 0, sizeof(kick_action));
            kick_action.type = ACT_KICK;
            kick_error[0] = '\0';
            if (game_apply(&slot->game, slot->game.kick_target,
                           &kick_action, kick_events, &kick_event_count,
                           kick_error, sizeof(kick_error)) != 0) {
                slot->game.kick_target = -1;
                slot->game.kick_votes = 0;
                slot->game.kick_started = 0;
                slot->game.version++;
                slot->game.updated = time(NULL);
                games_save(slot);
                play_json_error_msg(rqst, 409, "action", kick_error);
                pthread_mutex_unlock(&friskLock);
                return 0;
            }
            games_save(slot);
            if (kick_event_count > 0) {
                games_append_log(slot, kick_events, kick_event_count);
                chat_system_events(slot, kick_events, kick_event_count);
            }
            games_bump_lobby();
        } else {
            slot->game.version++;
            slot->game.updated = time(NULL);
            games_save(slot);
            games_bump_lobby();
        }
        frisk_json_begin(&json, rqst);
        frisk_json_key_int(&json, "ok", 1);
        frisk_json_key_int(&json, "version", slot->game.version);
        frisk_json_key_str(&json, "status",
                           game_status_name(slot->game.status));
        frisk_json_key_int(&json, "kickTarget", slot->game.kick_target);
        frisk_json_key_int(&json, "kickVotes", votes);
        frisk_json_key_int(&json, "kickNeeded", needed);
        frisk_json_key_array(&json, "events");
        {
            int i;
            for (i = 0; i < kick_event_count; i++) {
                write_event(&json, &kick_events[i]);
            }
        }
        frisk_json_array_end(&json);
        frisk_json_end(&json);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }

    if (strcmp(type, "set_standing") == 0) {
        unsigned int card = 0;
        unsigned int mode = 0;
        unsigned int threshold = 0;
        const struct MapAdventureCard *mc;

        if (slot->game.status != GAME_RUNNING ||
            !slot->game.seats[seat].alive) {
            frisk_json_error(rqst, 409, "phase");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        if (!frisk_post_uint(rqst, "card", 0, GAME_ADVENTURE_HAND_MAX - 1,
                             &card) ||
            !frisk_post_uint(rqst, "mode", 0, 2, &mode)) {
            frisk_json_error(rqst, 400, "params");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        frisk_post_uint(rqst, "n", 0, 3, &threshold);
        if (card >= (unsigned int)slot->game.seats[seat].adventure_hand_count ||
            slot->game.map == NULL) {
            frisk_json_error(rqst, 400, "params");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        mc = adventure_card_for(slot,
            slot->game.seats[seat].adventure_hand[card]);
        if (mc == NULL) {
            frisk_json_error(rqst, 400, "params");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        if (strcmp(mc->effect, "DEFENSE_BONUS") == 0 ||
            strcmp(mc->effect, "REROLL") == 0) {
            if (mode == 2 && threshold < 1) {
                frisk_json_error(rqst, 400, "params");
                pthread_mutex_unlock(&friskLock);
                return 0;
            }
        } else if (strcmp(mc->effect, "CANCEL_EVENT") == 0) {
            if (mode > 1) {
                frisk_json_error(rqst, 400, "params");
                pthread_mutex_unlock(&friskLock);
                return 0;
            }
            threshold = 0;
        } else {
            frisk_json_error(rqst, 400, "action");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        slot->game.seats[seat].adventure_auto[card] =
            (unsigned char)mode;
        slot->game.seats[seat].adventure_auto_n[card] =
            (unsigned char)threshold;
        slot->game.version++;
        slot->game.updated = time(NULL);
        games_save(slot);
        games_bump_lobby();
        frisk_json_begin(&json, rqst);
        frisk_json_key_int(&json, "ok", 1);
        frisk_json_key_int(&json, "version", slot->game.version);
        frisk_json_end(&json);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }

    memset(&action, 0, sizeof(action));
    if (strcmp(type, "claim") == 0) {
        if (!frisk_post_uint(rqst, "t", 0, MAP_MAX_TERRITORIES - 1,
                             &value)) {
            goto bad_params;
        }
        action.type = ACT_CLAIM;
        action.territory = (int)value;
    } else if (strcmp(type, "place") == 0) {
        if (!frisk_post_uint(rqst, "t", 0, MAP_MAX_TERRITORIES - 1,
                             &value)) {
            goto bad_params;
        }
        action.territory = (int)value;
        if (!frisk_post_uint(rqst, "count", 1, 1000000, &value)) {
            goto bad_params;
        }
        action.type = ACT_PLACE;
        action.count = (int)value;
    } else if (strcmp(type, "place_leader") == 0) {
        if (!frisk_post_uint(rqst, "t", 0, MAP_MAX_TERRITORIES - 1,
                             &value)) {
            goto bad_params;
        }
        action.type = ACT_PLACE_LEADER;
        action.territory = (int)value;
    } else if (strcmp(type, "trade_cards") == 0) {
        if (!frisk_post_uint(rqst, "cardA", 0, GAME_MAX_CARDS - 1,
                             &value)) {
            goto bad_params;
        }
        action.card_a = (int)value;
        if (!frisk_post_uint(rqst, "cardB", 0, GAME_MAX_CARDS - 1,
                             &value)) {
            goto bad_params;
        }
        action.card_b = (int)value;
        if (!frisk_post_uint(rqst, "cardC", 0, GAME_MAX_CARDS - 1,
                             &value)) {
            goto bad_params;
        }
        action.card_c = (int)value;
        action.type = ACT_TRADE_CARDS;
    } else if (strcmp(type, "done_reinforce") == 0) {
        action.type = ACT_DONE_REINFORCE;
    } else if (strcmp(type, "attack") == 0) {
        if (!frisk_post_uint(rqst, "from", 0, MAP_MAX_TERRITORIES - 1,
                             &value)) {
            goto bad_params;
        }
        action.from = (int)value;
        if (!frisk_post_uint(rqst, "to", 0, MAP_MAX_TERRITORIES - 1,
                             &value)) {
            goto bad_params;
        }
        action.to = (int)value;
        if (!frisk_post_uint(rqst, "dice", 1, 3, &value)) {
            goto bad_params;
        }
        action.dice = (int)value;
        if (!post_uint_if_present(rqst, "repeat", 0, 1, &value)) {
            goto bad_params;
        }
        action.repeat = (int)value;
        if (!post_uint_if_present(rqst, "stopAt", 0, 1000000, &value)) {
            goto bad_params;
        }
        action.stop_at = (int)value;
        action.type = ACT_ATTACK;
    } else if (strcmp(type, "move_in") == 0) {
        if (!frisk_post_uint(rqst, "from", 0, MAP_MAX_TERRITORIES - 1,
                             &value)) {
            goto bad_params;
        }
        action.from = (int)value;
        if (!frisk_post_uint(rqst, "to", 0, MAP_MAX_TERRITORIES - 1,
                             &value)) {
            goto bad_params;
        }
        action.to = (int)value;
        if (!frisk_post_uint(rqst, "count", 1, 1000000, &value)) {
            goto bad_params;
        }
        action.count = (int)value;
        if (!post_uint_if_present(rqst, "leader", 0, 1, &value)) {
            goto bad_params;
        }
        action.leader = (int)value;
        action.type = ACT_MOVE_IN;
    } else if (strcmp(type, "done_attack") == 0) {
        action.type = ACT_DONE_ATTACK;
    } else if (strcmp(type, "fortify") == 0) {
        if (!frisk_post_uint(rqst, "from", 0, MAP_MAX_TERRITORIES - 1,
                             &value)) {
            goto bad_params;
        }
        action.from = (int)value;
        if (!frisk_post_uint(rqst, "to", 0, MAP_MAX_TERRITORIES - 1,
                             &value)) {
            goto bad_params;
        }
        action.to = (int)value;
        if (!frisk_post_uint(rqst, "count", 1, 1000000, &value)) {
            goto bad_params;
        }
        action.count = (int)value;
        if (!post_uint_if_present(rqst, "leader", 0, 1, &value)) {
            goto bad_params;
        }
        action.leader = (int)value;
        action.type = ACT_FORTIFY;
    } else if (strcmp(type, "end_turn") == 0) {
        action.type = ACT_END_TURN;
    } else if (strcmp(type, "resign") == 0) {
        action.type = ACT_RESIGN;
    } else if (strcmp(type, "recruit_leader") == 0) {
        if (!frisk_post_uint(rqst, "t", 0, MAP_MAX_TERRITORIES - 1,
                             &value)) {
            goto bad_params;
        }
        action.type = ACT_RECRUIT_LEADER;
        action.territory = (int)value;
    } else if (strcmp(type, "play_card") == 0) {
        if (!frisk_post_uint(rqst, "card", 0,
                             GAME_ADVENTURE_HAND_MAX - 1, &value)) {
            goto bad_params;
        }
        action.card_a = (int)value;
        action.territory = -1;
        if (post_uint_if_present(rqst, "t", 0,
                                 MAP_MAX_TERRITORIES - 1, &value)) {
            if (_POSTexists(rqst, "t")) {
                action.territory = (int)value;
            }
        } else {
            goto bad_params;
        }
        action.type = ACT_PLAY_CARD;
    } else if (strcmp(type, "discard_adventure") == 0) {
        if (!frisk_post_uint(rqst, "card", 0,
                             GAME_ADVENTURE_HAND_MAX - 1, &value)) {
            goto bad_params;
        }
        action.type = ACT_DISCARD_ADVENTURE;
        action.card_a = (int)value;
    } else {
        goto bad_params;
    }

    error[0] = '\0';
    rc = game_apply(&slot->game, seat, &action, events, &event_count,
                    error, sizeof(error));
    if (rc != 0) {
        frisk_json_begin(&json, rqst);
        frisk_json_key_int(&json, "ok", 0);
        frisk_json_key_str(&json, "err", "action");
        frisk_json_key_str(&json, "msg", error);
        frisk_json_end(&json);
        rqst->headerResponse = 400;
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!games_save(slot)) {
        AmmServer_Warning("frisk: game %d action was accepted but not saved",
                          slot->game.id);
    }
    if (event_count > 0) {
        games_append_log(slot, events, event_count);
        chat_system_events(slot, events, event_count);
    }
    games_bump_lobby();
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_int(&json, "version", slot->game.version);
    frisk_json_key_str(&json, "status",
                       game_status_name(slot->game.status));
    frisk_json_key_array(&json, "events");
    {
        int i;
        for (i = 0; i < event_count; i++) {
            write_event(&json, &events[i]);
        }
    }
    frisk_json_array_end(&json);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;

bad_params:
    frisk_json_error(rqst, 400, "params");
    pthread_mutex_unlock(&friskLock);
    return 0;
}
