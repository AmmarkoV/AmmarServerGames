#include "games.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../../AmmServerlib/AmmServerlib.h"
#include "../../UserAccounts/sha256.h"
#include "engine/serialize.h"
#include "frisk.h"
#include "json.h"
#include "store.h"

#define FRISK_FINISHED_TTL_DEFAULT (24L * 60L * 60L)
#define FRISK_HISTORY_LINE 4096

static struct Map *maps[FRISK_MAX_MAPS];
static int map_count;
static struct FriskGame games[FRISK_MAX_GAMES];
static int next_game_id = 1;
static int lobby_version = 1;
static char games_path[FRISK_PATH_SIZE * 2];
static char history_path[FRISK_PATH_SIZE * 2];
static GameEndHook game_end_hook;
static void *game_end_hook_user;

static void games_on_end(struct Game *game, int winner, void *user);

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

static const char *pack_basename(const struct Map *map)
{
    const char *base;

    if (map == NULL) {
        return "";
    }
    base = strrchr(map->dir, '/');
    return (base != NULL) ? base + 1 : map->dir;
}

int games_map_count(void)
{
    return map_count;
}

const char *games_map_name_at(int index)
{
    if (index < 0 || index >= map_count) {
        return NULL;
    }
    return pack_basename(maps[index]);
}

const struct Map *games_map_by_name(const char *name)
{
    int i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < map_count; i++) {
        if (strcmp(pack_basename(maps[i]), name) == 0) {
            return maps[i];
        }
    }
    return NULL;
}

static int load_maps(char *err, size_t err_size)
{
    char maps_root[FRISK_PATH_SIZE];
    char pack_dir[FRISK_PATH_SIZE * 3];
    char map_file[FRISK_PATH_SIZE * 4];
    DIR *directory;
    struct dirent *entry;

    snprintf(maps_root, sizeof(maps_root), "%smaps", frisk_res_root());
    directory = opendir(maps_root);
    if (directory == NULL) {
        set_error(err, err_size, "cannot open map directory %s", maps_root);
        return -1;
    }
    while ((entry = readdir(directory)) != NULL) {
        struct Map *map;
        char reason[256];
        if (entry->d_name[0] == '.' || map_count >= FRISK_MAX_MAPS) {
            continue;
        }
        snprintf(pack_dir, sizeof(pack_dir), "%s/%s", maps_root,
                 entry->d_name);
        snprintf(map_file, sizeof(map_file), "%s/map.json", pack_dir);
        if (access(map_file, R_OK) != 0) {
            continue;
        }
        map = (struct Map *)malloc(sizeof(*map));
        if (map == NULL) {
            closedir(directory);
            set_error(err, err_size, "out of memory loading maps");
            return -1;
        }
        reason[0] = '\0';
        if (map_load(pack_dir, map, reason, sizeof(reason)) != 0 ||
            map_validate(map, reason, sizeof(reason)) != 0) {
            AmmServer_Warning("frisk: skipping map pack %s: %s",
                              entry->d_name, reason);
            free(map);
            continue;
        }
        maps[map_count++] = map;
    }
    closedir(directory);
    if (map_count == 0) {
        set_error(err, err_size, "no valid map packs under %s", maps_root);
        return -1;
    }
    return 0;
}

int games_lobby_version(void)
{
    return lobby_version;
}

void games_bump_lobby(void)
{
    lobby_version++;
}

int games_max_slots(void)
{
    return FRISK_MAX_GAMES;
}

struct FriskGame *games_slot(int index)
{
    if (index < 0 || index >= FRISK_MAX_GAMES) {
        return NULL;
    }
    return &games[index];
}

struct FriskGame *games_find(int id)
{
    int i;

    for (i = 0; i < FRISK_MAX_GAMES; i++) {
        if (games[i].used && games[i].game.id == id) {
            return &games[i];
        }
    }
    return NULL;
}

static void game_filename(int id, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%s/game_%d.json", games_path, id);
    out[out_size - 1] = '\0';
}

static void games_log_filename(int id, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%s/game_%d.log", games_path, id);
    out[out_size - 1] = '\0';
}

int games_append_log(struct FriskGame *game, const struct Event *events,
                     int count)
{
    char logname[FRISK_PATH_SIZE * 3 + 8];
    char buffer[32768];
    FILE *file;
    int i;
    int ok = 1;

    if (game == NULL || !game->used || events == NULL || count <= 0) {
        return 0;
    }
    games_log_filename(game->game.id, logname, sizeof(logname));
    file = fopen(logname, "a");
    if (file == NULL) {
        AmmServer_Warning("frisk: cannot append %s", logname);
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (game_event_to_json(&events[i], buffer, sizeof(buffer)) != 0) {
            AmmServer_Warning("frisk: event %d does not fit the log line",
                              i);
            ok = 0;
            break;
        }
        if (fputs(buffer, file) == EOF || fputc('\n', file) == EOF) {
            ok = 0;
            break;
        }
    }
    if (fclose(file) != 0) {
        ok = 0;
    }
    if (!ok) {
        AmmServer_Warning("frisk: could not append to %s", logname);
    }
    return ok;
}

static int log_line_version(const char *line, int *version)
{
    const char *marker;
    char *end;
    long value;

    if (line == NULL || line[0] != '{') {
        return 0;
    }
    marker = strstr(line, "\"version\":");
    if (marker == NULL) {
        return 0;
    }
    marker += 10;
    while (*marker == ' ') {
        marker++;
    }
    value = strtol(marker, &end, 10);
    if (end == marker || value < 0 || value > 1000000000L) {
        return 0;
    }
    *version = (int)value;
    return 1;
}

int games_read_log_after(int id, int after_version, int limit,
                         char **out, size_t *out_size,
                         char *err, size_t err_size)
{
    char logname[FRISK_PATH_SIZE * 3 + 8];
    char line[32768];
    FILE *file;
    int count = 0;
    int skip;
    int version;
    size_t capacity = 65536;
    size_t used = 0;
    char *buffer;
    int first = 1;

    if (out == NULL || out_size == NULL) {
        set_error(err, err_size, "invalid log output");
        return -1;
    }
    *out = NULL;
    *out_size = 0;
    if (limit < 1) {
        limit = 1;
    }
    if (limit > 100) {
        limit = 100;
    }
    games_log_filename(id, logname, sizeof(logname));
    file = fopen(logname, "r");
    if (file == NULL) {
        return 1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (log_line_version(line, &version) && version > after_version) {
            count++;
        }
    }
    if (count == 0) {
        fclose(file);
        return 0;
    }
    skip = (count > limit) ? count - limit : 0;
    buffer = (char *)malloc(capacity);
    if (buffer == NULL) {
        fclose(file);
        set_error(err, err_size, "out of memory reading the log");
        return -1;
    }
    rewind(file);
    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length;
        if (!log_line_version(line, &version) ||
            version <= after_version) {
            continue;
        }
        if (skip > 0) {
            skip--;
            continue;
        }
        length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' ||
                              line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (length == 0 || line[length - 1] != '}') {
            continue;
        }
        if (used + length + 2 > capacity) {
            size_t bigger = capacity * 2;
            char *grown;
            if (bigger > 4u * 1024u * 1024u) {
                free(buffer);
                fclose(file);
                set_error(err, err_size, "game log is too large");
                return -1;
            }
            grown = (char *)realloc(buffer, bigger);
            if (grown == NULL) {
                free(buffer);
                fclose(file);
                set_error(err, err_size, "out of memory reading the log");
                return -1;
            }
            buffer = grown;
            capacity = bigger;
        }
        if (!first) {
            buffer[used++] = ',';
        }
        memcpy(buffer + used, line, length);
        used += length;
        first = 0;
    }
    fclose(file);
    buffer[used] = '\0';
    *out = buffer;
    *out_size = used;
    return 0;
}

static void games_seed_log(struct FriskGame *game)
{
    char logname[FRISK_PATH_SIZE * 3 + 8];
    int count;
    int i;

    if (game == NULL || !game->used) {
        return;
    }
    games_log_filename(game->game.id, logname, sizeof(logname));
    if (access(logname, F_OK) == 0) {
        return;
    }
    count = game_log_count(&game->game);
    for (i = 0; i < count; i++) {
        const struct Event *event = game_log_event(&game->game, i);
        if (event != NULL) {
            games_append_log(game, event, 1);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Finished-game history (PLAN.md 7.6)                                 */
/* ------------------------------------------------------------------ */

static long finished_ttl_seconds(void)
{
    static long cached = -1;
    const char *text;
    long value;

    if (cached >= 0) {
        return cached;
    }
    text = getenv("FRISK_FINISHED_TTL_SECONDS");
    value = (text != NULL && text[0] != '\0')
        ? strtol(text, NULL, 10) : FRISK_FINISHED_TTL_DEFAULT;
    if (value < 0) {
        value = FRISK_FINISHED_TTL_DEFAULT;
    }
    cached = value;
    return cached;
}

static void history_put_string(char *out, size_t out_size, size_t *used,
                               const char *text)
{
    const unsigned char *p = (const unsigned char *)(text ? text : "");

    if (*used + 1 < out_size) {
        out[(*used)++] = '"';
    }
    for (; *p != '\0' && *used + 3 < out_size; p++) {
        if (*p == '"' || *p == '\\') {
            out[(*used)++] = '\\';
            out[(*used)++] = (char)*p;
        } else if (*p >= 0x20 && *p != 0x7f) {
            out[(*used)++] = (char)*p;
        }
    }
    if (*used + 1 < out_size) {
        out[(*used)++] = '"';
    }
    out[*used] = '\0';
}

static void history_put_fmt(char *out, size_t out_size, size_t *used,
                            const char *fmt, ...)
{
    va_list ap;
    int written;

    if (*used >= out_size) {
        return;
    }
    va_start(ap, fmt);
    written = vsnprintf(out + *used, out_size - *used, fmt, ap);
    va_end(ap);
    if (written > 0) {
        *used += (size_t)written;
        if (*used >= out_size) {
            *used = out_size - 1;
        }
    }
}

static int history_line(const struct Game *game, long finished,
                        char *out, size_t out_size)
{
    size_t used = 0;
    int i;

    out[0] = '\0';
    history_put_fmt(out, out_size, &used, "{\"id\":%d,\"name\":", game->id);
    history_put_string(out, out_size, &used, game->name);
    history_put_fmt(out, out_size, &used, ",\"map\":");
    history_put_string(out, out_size, &used, game->map_pack);
    history_put_fmt(out, out_size, &used,
                    ",\"finished\":%ld,\"turns\":%d,\"winner\":",
                    finished, game->turn);
    history_put_string(out, out_size, &used,
                       (game->winner >= 0 &&
                        game->winner < game->player_count)
                       ? game->seats[game->winner].username : "");
    history_put_fmt(out, out_size, &used, ",\"players\":[");
    for (i = 0; i < game->player_count; i++) {
        const struct Seat *seat = &game->seats[i];
        history_put_fmt(out, out_size, &used, "%s{\"user\":",
                        i > 0 ? "," : "");
        history_put_string(out, out_size, &used, seat->username);
        history_put_fmt(out, out_size, &used,
                        ",\"faction\":%d,\"won\":%s,\"taken\":%d,"
                        "\"killed\":%d,\"lost\":%d}",
                        seat->faction, i == game->winner ? "true" : "false",
                        seat->stats.territories_taken,
                        seat->stats.armies_killed, seat->stats.armies_lost);
    }
    history_put_fmt(out, out_size, &used, "]}");
    return used + 1 < out_size;
}

static void history_append(const struct Game *game, long finished)
{
    char line[FRISK_HISTORY_LINE];
    FILE *file;

    if (game == NULL || history_path[0] == '\0' ||
        !history_line(game, finished, line, sizeof(line))) {
        return;
    }
    file = fopen(history_path, "a");
    if (file == NULL) {
        AmmServer_Warning("frisk: cannot append %s", history_path);
        return;
    }
    if (fputs(line, file) == EOF || fputc('\n', file) == EOF) {
        AmmServer_Warning("frisk: could not write %s", history_path);
    }
    fclose(file);
}

static int history_line_id(const char *line)
{
    int id = 0;

    if (sscanf(line, "{\"id\":%d,", &id) != 1) {
        return 0;
    }
    return id;
}

static int history_has(int id)
{
    char line[FRISK_HISTORY_LINE];
    FILE *file = fopen(history_path, "r");
    int found = 0;

    if (file == NULL) {
        return 0;
    }
    while (!found && fgets(line, sizeof(line), file) != NULL) {
        found = history_line_id(line) == id;
    }
    fclose(file);
    return found;
}

static int history_line_has_user(const char *line, const char *username)
{
    char error[128];
    JsonValue *root = json_parse(line, error, sizeof(error));
    const JsonValue *players;
    size_t i;
    int found = 0;

    if (root == NULL) {
        return 0;
    }
    players = json_get(root, "players");
    for (i = 0; i < json_count(players) && !found; i++) {
        const char *user = json_get_string(json_get(json_at(players, i),
                                                    "user"));
        found = user != NULL && strcmp(user, username) == 0;
    }
    json_free(root);
    return found;
}

int games_history_for(const char *username, int limit,
                      char **out, size_t *out_size)
{
    char line[FRISK_HISTORY_LINE];
    char *buffer;
    size_t capacity;
    size_t used = 0;
    long *offsets;
    int count = 0;
    int start;
    int i;
    FILE *file;

    *out = NULL;
    *out_size = 0;
    if (username == NULL || limit < 1) {
        return 0;
    }
    file = fopen(history_path, "r");
    if (file == NULL) {
        return 0;
    }
    offsets = (long *)calloc((size_t)limit, sizeof(long));
    if (offsets == NULL) {
        fclose(file);
        return -1;
    }
    for (;;) {
        long offset = ftell(file);
        if (fgets(line, sizeof(line), file) == NULL) {
            break;
        }
        if (history_line_id(line) > 0 &&
            history_line_has_user(line, username)) {
            offsets[count % limit] = offset;
            count++;
        }
    }
    if (count == 0) {
        free(offsets);
        fclose(file);
        return 0;
    }
    capacity = (size_t)limit * FRISK_HISTORY_LINE + 1;
    buffer = (char *)malloc(capacity);
    if (buffer == NULL) {
        free(offsets);
        fclose(file);
        return -1;
    }
    start = count > limit ? count - limit : 0;
    for (i = start; i < count; i++) {
        size_t length;
        if (fseek(file, offsets[i % limit], SEEK_SET) != 0 ||
            fgets(line, sizeof(line), file) == NULL) {
            continue;
        }
        length = strcspn(line, "\r\n");
        if (used > 0) {
            buffer[used++] = ',';
        }
        memcpy(buffer + used, line, length);
        used += length;
    }
    buffer[used] = '\0';
    free(offsets);
    fclose(file);
    *out = buffer;
    *out_size = used;
    return 0;
}

static void games_on_end(struct Game *game, int winner, void *user)
{
    history_append(game, (long)time(NULL));
    if (game_end_hook != NULL) {
        game_end_hook(game, winner, user);
    }
}

static void install_end_hook(struct Game *game)
{
    game->end_hook = games_on_end;
    game->end_hook_user = game_end_hook_user;
}

static int finished_and_stale(const struct Game *game, time_t now)
{
    return game->status == GAME_FINISHED &&
           (long)(now - game->updated) >= finished_ttl_seconds();
}

void games_unload_finished(void)
{
    time_t now = time(NULL);
    int i;

    for (i = 0; i < FRISK_MAX_GAMES; i++) {
        if (games[i].used && finished_and_stale(&games[i].game, now)) {
            /* The game file and log stay on disk; only the RAM slot goes. */
            memset(&games[i], 0, sizeof(games[i]));
            lobby_version++;
        }
    }
}

int games_save(struct FriskGame *game)
{
    char filename[FRISK_PATH_SIZE * 3];
    char *buffer;
    char error[256];
    const size_t buffer_size = 2u * 1024u * 1024u;
    int ok;

    if (game == NULL || !game->used) {
        return 0;
    }
    game->game.updated = time(NULL);
    buffer = (char *)malloc(buffer_size);
    if (buffer == NULL) {
        return 0;
    }
    if (game_to_json(&game->game, buffer, buffer_size) != 0) {
        AmmServer_Warning("frisk: game %d state does not fit in %zu bytes",
                          game->game.id, buffer_size);
        free(buffer);
        return 0;
    }
    game_filename(game->game.id, filename, sizeof(filename));
    ok = store_write_atomic(filename, buffer, strlen(buffer),
                            error, sizeof(error)) == 0;
    if (!ok) {
        AmmServer_Warning("frisk: could not save game %d: %s",
                          game->game.id, error);
    }
    free(buffer);
    return ok;
}

static int valid_game_name(const char *name)
{
    size_t length;
    size_t i;

    if (name == NULL) {
        return 0;
    }
    length = strlen(name);
    if (length < 1 || length >= GAME_NAME_SIZE) {
        return 0;
    }
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c == 0x7f) {
            return 0;
        }
    }
    return 1;
}

static void hash_password(const char *password, char *out, size_t out_size)
{
    char digest[65];

    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (password == NULL || password[0] == '\0') {
        return;
    }
    SHA256_ToHex((const unsigned char *)password,
                 (unsigned int)strlen(password), digest, sizeof(digest));
    snprintf(out, out_size, "%s", digest);
}

static int password_matches(const struct Game *game, const char *password)
{
    char digest[65];

    if (game->password_hash[0] == '\0') {
        return 1;
    }
    if (password == NULL || password[0] == '\0') {
        return 0;
    }
    hash_password(password, digest, sizeof(digest));
    return strcmp(digest, game->password_hash) == 0;
}

static struct FriskGame *free_slot(void)
{
    int i;

    games_unload_finished();
    for (i = 0; i < FRISK_MAX_GAMES; i++) {
        if (!games[i].used) {
            return &games[i];
        }
    }
    return NULL;
}

struct FriskGame *games_create(const char *name, const char *map_name,
                               const char *host, int max_players,
                               int start_mode, int batch,
                               const char *password,
                               char *err, size_t err_size)
{
    const struct Map *map;
    struct FriskGame *slot;
    char join_error[128];

    if (!valid_game_name(name)) {
        set_error(err, err_size, "name");
        return NULL;
    }
    map = games_map_by_name(map_name != NULL ? map_name : "lotr");
    if (map == NULL) {
        set_error(err, err_size, "map");
        return NULL;
    }
    if (max_players < 2 || max_players > GAME_MAX_PLAYERS) {
        set_error(err, err_size, "seats");
        return NULL;
    }
    if (start_mode != GAME_START_DEAL && start_mode != GAME_START_DRAFT) {
        set_error(err, err_size, "mode");
        return NULL;
    }
    if (batch < 1) {
        batch = 5;
    }
    if (batch > 1000) {
        batch = 1000;
    }
    slot = free_slot();
    if (slot == NULL) {
        set_error(err, err_size, "full");
        return NULL;
    }
    game_init(&slot->game, map, next_game_id, name, host, 0);
    slot->game.max_players = max_players;
    slot->game.start_mode = start_mode;
    slot->game.setup_batch_size = batch;
    install_end_hook(&slot->game);
    hash_password(password, slot->game.password_hash,
                  sizeof(slot->game.password_hash));
    if (game_join(&slot->game, host, -1, join_error,
                  sizeof(join_error)) != 0) {
        set_error(err, err_size, "host: %s", join_error);
        memset(slot, 0, sizeof(*slot));
        return NULL;
    }
    slot->map = map;
    slot->used = 1;
    next_game_id++;
    lobby_version++;
    games_save(slot);
    return slot;
}

int games_join(struct FriskGame *game, const char *username, int faction,
               const char *password, int *out_seat,
               char *err, size_t err_size)
{
    char join_error[128];
    int seat;

    if (game == NULL || !game->used) {
        set_error(err, err_size, "notfound");
        return 0;
    }
    if (game->game.status != GAME_OPEN ||
        game->game.phase != PHASE_LOBBY) {
        set_error(err, err_size, "started");
        return 0;
    }
    if (!password_matches(&game->game, password)) {
        set_error(err, err_size, "password");
        return 0;
    }
    seat = game_find_seat(&game->game, username);
    if (seat >= 0) {
        /* Already seated: a faction asks to switch to a free one (8.4). */
        if (faction >= 0 && faction != game->game.seats[seat].faction) {
            int i;
            if (game->map == NULL || faction >= game->map->factionCount) {
                set_error(err, err_size, "faction");
                return 0;
            }
            for (i = 0; i < game->game.player_count; i++) {
                if (game->game.seats[i].faction == faction) {
                    set_error(err, err_size, "faction");
                    return 0;
                }
            }
            game->game.seats[seat].faction = faction;
            game->game.version++;
            lobby_version++;
            games_save(game);
        }
        if (out_seat != NULL) {
            *out_seat = seat;
        }
        return 1;
    }
    if (game->game.player_count >= game->game.max_players) {
        set_error(err, err_size, "full");
        return 0;
    }
    if (game_join(&game->game, username, faction, join_error,
                  sizeof(join_error)) != 0) {
        set_error(err, err_size, "join: %s", join_error);
        return 0;
    }
    seat = game_find_seat(&game->game, username);
    if (out_seat != NULL) {
        *out_seat = seat;
    }
    lobby_version++;
    games_save(game);
    return 1;
}

static void clear_game(struct FriskGame *game)
{
    char filename[FRISK_PATH_SIZE * 3];

    if (game == NULL || !game->used) {
        return;
    }
    game_filename(game->game.id, filename, sizeof(filename));
    unlink(filename);
    memset(game, 0, sizeof(*game));
}

static int remove_seat(struct FriskGame *game, const char *username,
                       char *err, size_t err_size)
{
    int seat = game_find_seat(&game->game, username);
    int i;

    if (seat < 0) {
        set_error(err, err_size, "seat");
        return 0;
    }
    if (game->game.player_count == 1) {
        clear_game(game);
        lobby_version++;
        return 1;
    }
    for (i = seat; i + 1 < game->game.player_count; i++) {
        game->game.seats[i] = game->game.seats[i + 1];
    }
    memset(&game->game.seats[game->game.player_count - 1], 0,
           sizeof(game->game.seats[0]));
    game->game.player_count--;
    game->game.version++;
    if (strcmp(game->game.host, username) == 0) {
        snprintf(game->game.host, sizeof(game->game.host), "%s",
                 game->game.seats[0].username);
    }
    lobby_version++;
    games_save(game);
    return 1;
}

int games_leave(struct FriskGame *game, const char *username,
                char *err, size_t err_size)
{
    if (game == NULL || !game->used) {
        set_error(err, err_size, "notfound");
        return 0;
    }
    if (game->game.status != GAME_OPEN ||
        game->game.phase != PHASE_LOBBY) {
        set_error(err, err_size, "started");
        return 0;
    }
    return remove_seat(game, username, err, err_size);
}

int games_start(struct FriskGame *game, const char *requester,
                char *err, size_t err_size)
{
    char start_error[256];

    if (game == NULL || !game->used) {
        set_error(err, err_size, "notfound");
        return 0;
    }
    if (strcmp(game->game.host, requester) != 0) {
        set_error(err, err_size, "host");
        return 0;
    }
    if (game->game.status != GAME_OPEN ||
        game->game.phase != PHASE_LOBBY) {
        set_error(err, err_size, "started");
        return 0;
    }
    install_end_hook(&game->game);
    if (game_start(&game->game, game->game.start_mode,
                   game->game.setup_batch_size, start_error,
                   sizeof(start_error)) != 0) {
        set_error(err, err_size, "start: %s", start_error);
        return 0;
    }
    lobby_version++;
    games_save(game);
    games_seed_log(game);
    return 1;
}

int games_kick(struct FriskGame *game, const char *requester,
               const char *target, int requester_is_admin,
               char *err, size_t err_size)
{
    if (game == NULL || !game->used) {
        set_error(err, err_size, "notfound");
        return 0;
    }
    if (!requester_is_admin &&
        strcmp(game->game.host, requester) != 0) {
        set_error(err, err_size, "host");
        return 0;
    }
    if (game->game.status != GAME_OPEN ||
        game->game.phase != PHASE_LOBBY) {
        set_error(err, err_size, "started");
        return 0;
    }
    return remove_seat(game, target, err, err_size);
}

int games_delete(struct FriskGame *game, const char *requester,
                 int requester_is_admin, char *err, size_t err_size)
{
    if (game == NULL || !game->used) {
        set_error(err, err_size, "notfound");
        return 0;
    }
    if (!requester_is_admin &&
        strcmp(game->game.host, requester) != 0) {
        set_error(err, err_size, "host");
        return 0;
    }
    if (game->game.status == GAME_RUNNING && !requester_is_admin) {
        set_error(err, err_size, "running");
        return 0;
    }
    clear_game(game);
    lobby_version++;
    return 1;
}

static void load_games(void)
{
    DIR *directory;
    struct dirent *entry;

    snprintf(games_path, sizeof(games_path), "%s/games", frisk_data_dir());
    snprintf(history_path, sizeof(history_path), "%s/history.log",
             frisk_data_dir());
    directory = opendir(games_path);
    if (directory == NULL) {
        AmmServer_Warning("frisk: no game directory at %s", games_path);
        return;
    }
    while ((entry = readdir(directory)) != NULL) {
        char filename[FRISK_PATH_SIZE * 3];
        char *text = NULL;
        size_t text_size = 0;
        char error[256];
        struct Game loaded;
        const struct Map *map = NULL;
        int i;
        struct FriskGame *slot;
        size_t name_length;

        name_length = strlen(entry->d_name);
        if (name_length > 4 &&
            strcmp(entry->d_name + name_length - 4, ".tmp") == 0) {
            snprintf(filename, sizeof(filename), "%s/%s", games_path,
                     entry->d_name);
            unlink(filename);
            continue;
        }
        if (name_length < 10 ||
            strncmp(entry->d_name, "game_", 5) != 0 ||
            strcmp(entry->d_name + name_length - 5, ".json") != 0) {
            continue;
        }
        snprintf(filename, sizeof(filename), "%s/%s", games_path,
                 entry->d_name);
        if (store_read_file(filename, &text, &text_size,
                            error, sizeof(error)) != 0) {
            AmmServer_Warning("frisk: cannot read %s: %s", filename, error);
            continue;
        }
        for (i = 0; i < map_count; i++) {
            memset(&loaded, 0, sizeof(loaded));
            if (game_from_json(text, &loaded, maps[i], error,
                               sizeof(error)) == 0) {
                map = maps[i];
                break;
            }
        }
        if (map == NULL) {
            char corrupt[FRISK_PATH_SIZE * 4];
            AmmServer_Warning("frisk: corrupt game file %s", filename);
            snprintf(corrupt, sizeof(corrupt), "%s.corrupt", filename);
            rename(filename, corrupt);
            free(text);
            continue;
        }
        if (loaded.id >= next_game_id) {
            next_game_id = loaded.id + 1;
        }
        if (loaded.status == GAME_FINISHED) {
            if (!history_has(loaded.id)) {
                history_append(&loaded, (long)loaded.updated);
            }
            if (finished_and_stale(&loaded, time(NULL))) {
                /* Kept on disk and in the history; not loaded into RAM. */
                free(text);
                continue;
            }
        }
        slot = free_slot();
        if (slot == NULL) {
            AmmServer_Warning("frisk: too many games, skipping %s",
                              filename);
            free(text);
            continue;
        }
        slot->game = loaded;
        install_end_hook(&slot->game);
        slot->map = map;
        slot->used = 1;
        games_seed_log(slot);
        lobby_version++;
        free(text);
    }
    closedir(directory);
}

int games_init(GameEndHook end_hook, void *end_hook_user,
               char *err, size_t err_size)
{
    memset(games, 0, sizeof(games));
    map_count = 0;
    next_game_id = 1;
    lobby_version = 1;
    game_end_hook = end_hook;
    game_end_hook_user = end_hook_user;
    if (load_maps(err, err_size) != 0) {
        return -1;
    }
    load_games();
    AmmServer_Warning("frisk: loaded %d map packs and %d games",
                      map_count, next_game_id - 1);
    return 0;
}

void games_shutdown(void)
{
    int i;

    for (i = 0; i < FRISK_MAX_GAMES; i++) {
        if (games[i].used) {
            games_save(&games[i]);
        }
    }
    for (i = 0; i < map_count; i++) {
        free(maps[i]);
        maps[i] = NULL;
    }
    map_count = 0;
    memset(games, 0, sizeof(games));
}
