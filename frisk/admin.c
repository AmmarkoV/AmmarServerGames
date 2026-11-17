#include "admin.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "accounts.h"
#include "chat.h"
#include "frisk.h"
#include "games.h"
#include "http.h"

static void dir_usage(const char *path, unsigned long long *bytes,
                      unsigned long long *files)
{
    DIR *directory;
    struct dirent *entry;

    *bytes = 0;
    *files = 0;
    directory = opendir(path);
    if (directory == NULL) {
        return;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child[FRISK_PATH_SIZE * 2];
        struct stat info;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (snprintf(child, sizeof(child), "%s/%s", path,
                     entry->d_name) >= (int)sizeof(child) ||
            stat(child, &info) != 0 || !S_ISREG(info.st_mode)) {
            continue;
        }
        *bytes += (unsigned long long)info.st_size;
        (*files)++;
    }
    closedir(directory);
}

static void admin_error(struct AmmServer_DynamicRequest *rqst,
                        unsigned int status, const char *code,
                        const char *message)
{
    struct FriskJson json;

    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 0);
    frisk_json_key_str(&json, "err", code);
    if (message != NULL && message[0] != '\0') {
        frisk_json_key_str(&json, "msg", message);
    }
    frisk_json_end(&json);
    rqst->headerResponse = status;
}

void *admin_overview_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char games_path[FRISK_PATH_SIZE * 2];
    char chat_path[FRISK_PATH_SIZE * 2];
    unsigned long long games_bytes, games_files;
    unsigned long long chat_bytes, chat_files;
    unsigned long long clip_bytes, clip_files;
    struct FriskJson json;
    int i;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username))) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!accounts_is_admin(username)) {
        frisk_json_error(rqst, 403, "admin");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }

    snprintf(games_path, sizeof(games_path), "%s/games", frisk_data_dir());
    snprintf(chat_path, sizeof(chat_path), "%s/chat", frisk_data_dir());
    dir_usage(games_path, &games_bytes, &games_files);
    dir_usage(chat_path, &chat_bytes, &chat_files);
    dir_usage(chat_clips_dir(), &clip_bytes, &clip_files);

    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);

    frisk_json_key_array(&json, "users");
    for (i = 0; i < accounts_profile_count(); i++) {
        const struct FriskProfile *profile = accounts_profile_at(i);
        if (profile == NULL) {
            continue;
        }
        frisk_json_array_object(&json);
        frisk_json_key_str(&json, "username", profile->username);
        frisk_json_key_bool(&json, "admin", profile->admin);
        frisk_json_key_str(&json, "colour", profile->colour);
        frisk_json_key_str(&json, "glyph", profile->glyph);
        frisk_json_key_int(&json, "gamesPlayed", profile->games_played);
        frisk_json_key_int(&json, "wins", profile->wins);
        frisk_json_key_int(&json, "losses", profile->losses);
        frisk_json_key_int(&json, "territoriesTaken",
                           profile->territories_taken);
        frisk_json_key_int(&json, "armiesKilled", profile->armies_killed);
        frisk_json_key_int(&json, "armiesLost", profile->armies_lost);
        frisk_json_key_int(&json, "created", profile->created);
        frisk_json_key_int(&json, "lastSeen", profile->last_seen);
        frisk_json_object_end(&json);
    }
    frisk_json_array_end(&json);

    frisk_json_key_array(&json, "invites");
    for (i = 0; i < accounts_invite_count(); i++) {
        const struct FriskInvite *invite = accounts_invite_at(i);
        if (invite == NULL) {
            continue;
        }
        frisk_json_array_object(&json);
        frisk_json_key_str(&json, "code", invite->code);
        frisk_json_key_str(&json, "creator", invite->creator);
        frisk_json_key_int(&json, "created", invite->created);
        frisk_json_key_str(&json, "usedBy", invite->used_by);
        frisk_json_key_int(&json, "usedAt", invite->used_at);
        frisk_json_key_bool(&json, "used", invite->used);
        frisk_json_key_bool(&json, "revoked", invite->revoked);
        frisk_json_object_end(&json);
    }
    frisk_json_array_end(&json);

    frisk_json_key_array(&json, "games");
    for (i = 0; i < games_max_slots(); i++) {
        struct FriskGame *slot = games_slot(i);
        int seat;

        if (slot == NULL || !slot->used) {
            continue;
        }
        frisk_json_array_object(&json);
        frisk_json_key_int(&json, "id", slot->game.id);
        frisk_json_key_str(&json, "name", slot->game.name);
        frisk_json_key_str(&json, "map", slot->game.map_pack);
        frisk_json_key_str(&json, "status",
                           game_status_name(slot->game.status));
        frisk_json_key_str(&json, "phase",
                           game_phase_name(slot->game.phase));
        frisk_json_key_str(&json, "host", slot->game.host);
        frisk_json_key_int(&json, "players", slot->game.player_count);
        frisk_json_key_int(&json, "maxPlayers", slot->game.max_players);
        frisk_json_key_int(&json, "updated", (long long)slot->game.updated);
        frisk_json_key_array(&json, "seats");
        for (seat = 0; seat < slot->game.player_count; seat++) {
            frisk_json_array_object(&json);
            frisk_json_key_int(&json, "seat", seat);
            frisk_json_key_str(&json, "user",
                               slot->game.seats[seat].username);
            frisk_json_key_int(&json, "faction",
                               slot->game.seats[seat].faction);
            frisk_json_key_bool(&json, "alive",
                                slot->game.seats[seat].alive);
            frisk_json_object_end(&json);
        }
        frisk_json_array_end(&json);
        frisk_json_object_end(&json);
    }
    frisk_json_array_end(&json);

    frisk_json_key_object(&json, "storage");
    frisk_json_key_int(&json, "games", (long long)games_bytes);
    frisk_json_key_int(&json, "gamesFiles", (long long)games_files);
    frisk_json_key_int(&json, "chat", (long long)chat_bytes);
    frisk_json_key_int(&json, "chatFiles", (long long)chat_files);
    frisk_json_key_int(&json, "clips", (long long)clip_bytes);
    frisk_json_key_int(&json, "clipFiles", (long long)clip_files);
    frisk_json_key_int(&json, "total",
                       (long long)(games_bytes + chat_bytes + clip_bytes));
    frisk_json_object_end(&json);

    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *admin_neutralise_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    unsigned int id = 0;
    unsigned int seat = 0;
    struct FriskGame *game;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    int event_count = 0;
    char error[256];
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!accounts_is_admin(username)) {
        frisk_json_error(rqst, 403, "admin");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_post_uint(rqst, "g", 1, 1000000000u, &id) ||
        !frisk_post_uint(rqst, "seat", 0, GAME_MAX_PLAYERS - 1, &seat)) {
        frisk_json_error(rqst, 400, "params");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    game = games_find((int)id);
    if (game == NULL) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if ((int)seat >= game->game.player_count) {
        frisk_json_error(rqst, 400, "seat");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    memset(&action, 0, sizeof(action));
    action.type = ACT_KICK;
    error[0] = '\0';
    if (game_apply(&game->game, (int)seat, &action, events, &event_count,
                   error, sizeof(error)) != 0) {
        admin_error(rqst, 409, "action", error);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!games_save(game)) {
        AmmServer_Warning("frisk admin: neutralise was accepted but not saved");
    }
    if (event_count > 0) {
        games_append_log(game, events, event_count);
        chat_system_events(game, events, event_count);
    }
    games_bump_lobby();
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_int(&json, "version", game->game.version);
    frisk_json_key_str(&json, "status",
                       game_status_name(game->game.status));
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}
