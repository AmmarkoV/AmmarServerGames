#include "lobby.h"

#include <stdio.h>
#include <string.h>

#include "accounts.h"
#include "chat.h"
#include "frisk.h"
#include "games.h"
#include "http.h"

static void write_players(struct FriskJson *json,
                          const struct Game *game)
{
    int i;

    frisk_json_key_array(json, "players");
    for (i = 0; i < game->player_count; i++) {
        frisk_json_array_object(json);
        frisk_json_key_int(json, "seat", i);
        frisk_json_key_str(json, "user", game->seats[i].username);
        frisk_json_key_int(json, "faction", game->seats[i].faction);
        frisk_json_key_bool(json, "alive", game->seats[i].alive);
        frisk_json_object_end(json);
    }
    frisk_json_array_end(json);
}

static void write_game_summary(struct FriskJson *json,
                               const struct FriskGame *slot,
                               int viewer_seat)
{
    const struct Game *game = &slot->game;

    frisk_json_array_object(json);
    frisk_json_key_int(json, "id", game->id);
    frisk_json_key_str(json, "name", game->name);
    frisk_json_key_str(json, "map", game->map_pack);
    frisk_json_key_str(json, "status", game_status_name(game->status));
    frisk_json_key_str(json, "phase", game_phase_name(game->phase));
    frisk_json_key_int(json, "version", game->version);
    frisk_json_key_int(json, "current", game->current_player);
    frisk_json_key_int(json, "turn", game->turn);
    frisk_json_key_int(json, "playerCount", game->player_count);
    frisk_json_key_int(json, "maxPlayers", game->max_players);
    frisk_json_key_int(json, "updated", game->updated);
    frisk_json_key_int(json, "yourSeat", viewer_seat);
    frisk_json_key_bool(json, "yourTurn",
                        viewer_seat >= 0 &&
                        game->status == GAME_RUNNING &&
                        game->current_player == viewer_seat);
    frisk_json_key_int(json, "winner", game->winner);
    frisk_json_key_str(json, "host", game->host);
    frisk_json_key_bool(json, "hasPassword",
                        game->password_hash[0] != '\0');
    write_players(json, game);
    frisk_json_object_end(json);
}

static void game_error(struct AmmServer_DynamicRequest *rqst,
                       const char *reason)
{
    if (strstr(reason, "faction") != NULL) {
        frisk_json_error(rqst, 409, "faction");
    } else if (strstr(reason, "full") != NULL) {
        frisk_json_error(rqst, 409, "full");
    } else if (strstr(reason, "password") != NULL) {
        frisk_json_error(rqst, 403, "password");
    } else if (strstr(reason, "host") != NULL) {
        frisk_json_error(rqst, 403, "host");
    } else if (strstr(reason, "started") != NULL) {
        frisk_json_error(rqst, 409, "started");
    } else if (strstr(reason, "notfound") != NULL) {
        frisk_json_error(rqst, 404, "notfound");
    } else if (strstr(reason, "running") != NULL) {
        frisk_json_error(rqst, 409, "running");
    } else if (strstr(reason, "seat") != NULL) {
        frisk_json_error(rqst, 400, "seat");
    } else if (strstr(reason, "map") != NULL) {
        frisk_json_error(rqst, 400, "map");
    } else if (strstr(reason, "name") != NULL) {
        frisk_json_error(rqst, 400, "name");
    } else if (strstr(reason, "mode") != NULL) {
        frisk_json_error(rqst, 400, "mode");
    } else if (strstr(reason, "save") != NULL) {
        frisk_json_error(rqst, 500, "save");
    } else {
        frisk_json_error(rqst, 400, "game");
    }
}

void *lobby_list_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    unsigned int since = 0;
    struct FriskJson json;
    int i;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username))) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (_GETexists(rqst, "since") &&
        !frisk_param_uint(rqst, "since", 0, 1000000000u, &since)) {
        frisk_json_error(rqst, 400, "params");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    games_unload_finished();
    if (since != 0 && since == (unsigned int)games_lobby_version()) {
        frisk_json_begin(&json, rqst);
        frisk_json_key_int(&json, "ok", 1);
        frisk_json_key_bool(&json, "same", 1);
        frisk_json_key_int(&json, "version", games_lobby_version());
        frisk_json_end(&json);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }

    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_int(&json, "version", games_lobby_version());

    frisk_json_key_array(&json, "yourGames");
    for (i = 0; i < games_max_slots(); i++) {
        struct FriskGame *slot = games_slot(i);
        int seat;
        if (slot == NULL || !slot->used) {
            continue;
        }
        seat = game_find_seat(&slot->game, username);
        if (seat >= 0) {
            write_game_summary(&json, slot, seat);
        }
    }
    frisk_json_array_end(&json);

    frisk_json_key_array(&json, "openGames");
    for (i = 0; i < games_max_slots(); i++) {
        struct FriskGame *slot = games_slot(i);
        int seat;
        if (slot == NULL || !slot->used ||
            slot->game.status != GAME_OPEN) {
            continue;
        }
        seat = game_find_seat(&slot->game, username);
        write_game_summary(&json, slot, seat);
    }
    frisk_json_array_end(&json);

    frisk_json_key_array(&json, "runningGames");
    for (i = 0; i < games_max_slots(); i++) {
        struct FriskGame *slot = games_slot(i);
        int seat;
        if (slot == NULL || !slot->used ||
            slot->game.status != GAME_RUNNING) {
            continue;
        }
        seat = game_find_seat(&slot->game, username);
        write_game_summary(&json, slot, seat);
    }
    frisk_json_array_end(&json);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *lobby_map_callback(struct AmmServer_DynamicRequest *rqst)
{
    struct FriskJson json;
    int i;

    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_array(&json, "packs");
    for (i = 0; i < games_map_count(); i++) {
        frisk_json_array_str(&json, games_map_name_at(i));
    }
    frisk_json_array_end(&json);
    frisk_json_key_str(&json, "default", "lotr");
    frisk_json_end(&json);
    return 0;
}

void *lobby_create_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char name[GAME_NAME_SIZE];
    char map_name[MAP_NAME_SIZE];
    char mode_text[16];
    char password[128];
    unsigned int max_players = 4;
    unsigned int batch = 5;
    int mode = GAME_START_DEAL;
    char error[256];
    struct FriskGame *game;
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    name[0] = map_name[0] = mode_text[0] = password[0] = '\0';
    if ((_POSTexists(rqst, "name") &&
         !frisk_post_str(rqst, "name", name, sizeof(name))) ||
        (_POSTexists(rqst, "map") &&
         !frisk_post_str(rqst, "map", map_name, sizeof(map_name))) ||
        (_POSTexists(rqst, "mode") &&
         !frisk_post_str(rqst, "mode", mode_text, sizeof(mode_text))) ||
        (_POSTexists(rqst, "password") &&
         !frisk_post_str(rqst, "password", password, sizeof(password))) ||
        (_POSTexists(rqst, "seats") &&
         !frisk_post_uint(rqst, "seats", 2, GAME_MAX_PLAYERS,
                          &max_players)) ||
        (_POSTexists(rqst, "batch") &&
         !frisk_post_uint(rqst, "batch", 1, 1000, &batch))) {
        frisk_json_error(rqst, 400, "params");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (strcmp(mode_text, "draft") == 0) {
        mode = GAME_START_DRAFT;
    }
    game = games_create(name, map_name[0] ? map_name : "lotr", username,
                        (int)max_players, mode, (int)batch, password,
                        error, sizeof(error));
    if (game == NULL) {
        game_error(rqst, error);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_int(&json, "id", game->game.id);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *lobby_join_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char password[128];
    unsigned int id = 0;
    unsigned int faction = 0;
    int seat = -1;
    int was_seated;
    struct FriskGame *game;
    char error[256];
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_post_uint(rqst, "g", 1, 1000000000u, &id)) {
        frisk_json_error(rqst, 400, "game");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    password[0] = '\0';
    if (_POSTexists(rqst, "password") &&
        !frisk_post_str(rqst, "password", password, sizeof(password))) {
        frisk_json_error(rqst, 400, "params");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (_POSTexists(rqst, "faction")) {
        if (!frisk_post_uint(rqst, "faction", 0,
                             MAP_MAX_FACTIONS - 1, &faction)) {
            frisk_json_error(rqst, 400, "faction");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
    } else {
        faction = 0;
    }
    game = games_find((int)id);
    if (game == NULL) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    was_seated = game_find_seat(&game->game, username) >= 0;
    if (!games_join(game, username,
                    _POSTexists(rqst, "faction") ? (int)faction : -1,
                    password, &seat, error, sizeof(error))) {
        game_error(rqst, error);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!was_seated) {
        char message[128];
        snprintf(message, sizeof(message), "%s joined the game", username);
        chat_system_game(game, message);
    }
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_int(&json, "id", game->game.id);
    frisk_json_key_int(&json, "seat", seat);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *lobby_leave_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    unsigned int id = 0;
    struct FriskGame *game;
    char error[256];
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_post_uint(rqst, "g", 1, 1000000000u, &id)) {
        frisk_json_error(rqst, 400, "game");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    game = games_find((int)id);
    if (game == NULL) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!games_leave(game, username, error, sizeof(error))) {
        game_error(rqst, error);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    {
        char message[128];
        snprintf(message, sizeof(message), "%s left the game", username);
        chat_system_game(game, message);
    }
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *lobby_start_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    unsigned int id = 0;
    struct FriskGame *game;
    char error[256];
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_post_uint(rqst, "g", 1, 1000000000u, &id)) {
        frisk_json_error(rqst, 400, "game");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    game = games_find((int)id);
    if (game == NULL) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!games_start(game, username, error, sizeof(error))) {
        game_error(rqst, error);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    chat_system_game(game, "The game has started.");
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_int(&json, "version", game->game.version);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *lobby_kick_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char target[32];
    unsigned int id = 0;
    struct FriskGame *game;
    char error[256];
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_post_uint(rqst, "g", 1, 1000000000u, &id) ||
        !frisk_post_str(rqst, "user", target, sizeof(target))) {
        frisk_json_error(rqst, 400, "game");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    game = games_find((int)id);
    if (game == NULL) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!games_kick(game, username, target,
                    accounts_is_admin(username), error, sizeof(error))) {
        game_error(rqst, error);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *lobby_delete_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    unsigned int id = 0;
    struct FriskGame *game;
    char error[256];
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_post_uint(rqst, "g", 1, 1000000000u, &id)) {
        frisk_json_error(rqst, 400, "game");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    game = games_find((int)id);
    if (game == NULL) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!games_delete(game, username, accounts_is_admin(username),
                      error, sizeof(error))) {
        game_error(rqst, error);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}
