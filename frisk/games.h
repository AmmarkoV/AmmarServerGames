#ifndef FRISK_GAMES_H
#define FRISK_GAMES_H

#include <stddef.h>

#include "engine/rules.h"

#define FRISK_MAX_GAMES 64
#define FRISK_MAX_MAPS 8

struct FriskGame {
    struct Game game;
    const struct Map *map;
    int used;
};

int games_init(GameEndHook end_hook, void *end_hook_user,
               char *err, size_t err_size);
void games_shutdown(void);

int games_lobby_version(void);
void games_bump_lobby(void);

int games_max_slots(void);
struct FriskGame *games_slot(int index);
struct FriskGame *games_find(int id);

int games_map_count(void);
const char *games_map_name_at(int index);
const struct Map *games_map_by_name(const char *name);

struct FriskGame *games_create(const char *name, const char *map_name,
                               const char *host, int max_players,
                               int start_mode, int batch,
                               const char *password,
                               char *err, size_t err_size);
int games_join(struct FriskGame *game, const char *username, int faction,
               const char *password, int *out_seat,
               char *err, size_t err_size);
int games_leave(struct FriskGame *game, const char *username,
                char *err, size_t err_size);
int games_start(struct FriskGame *game, const char *requester,
                char *err, size_t err_size);
int games_kick(struct FriskGame *game, const char *requester,
               const char *target, int requester_is_admin,
               char *err, size_t err_size);
int games_delete(struct FriskGame *game, const char *requester,
                 int requester_is_admin, char *err, size_t err_size);
int games_save(struct FriskGame *game);
/* Frees RAM slots of games finished longer than FRISK_FINISHED_TTL_SECONDS
 * (default 24 h) ago; their files stay on disk (PLAN.md 7.6). */
void games_unload_finished(void);
/* Returns 0 with the user's last `limit` finished-game history entries
 * (oldest first, comma separated JSON objects) in *out, -1 on failure. */
int games_history_for(const char *username, int limit,
                      char **out, size_t *out_size);
int games_append_log(struct FriskGame *game, const struct Event *events,
                     int count);
/* Returns 0 with the last `limit` matching raw event JSON objects (comma
 * separated) in *out, 1 when no durable log file exists, -1 on failure. */
int games_read_log_after(int id, int after_version, int limit,
                         char **out, size_t *out_size,
                         char *err, size_t err_size);

#endif
