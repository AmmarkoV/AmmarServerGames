#ifndef FRISK_CHAT_H
#define FRISK_CHAT_H

#include <stddef.h>

#include "../../AmmServerlib/AmmServerlib.h"

#define FRISK_TAUNT_COUNT 8
#define FRISK_MAX_CLIP_BYTES (512u * 1024u)

struct FriskGame;
struct Event;

/* Must be called after frisk_config_init() and store_init(); creates the
 * private clip directory and starts the hourly housekeeping thread. */
int chat_init(char *err, size_t err_size);
void chat_shutdown(void);
const char *chat_clips_dir(void);

/* System lines are appended to a game's room.  The caller holds friskLock,
 * as it does for every other store access. */
void chat_system_game(struct FriskGame *game, const char *text);
void chat_system_events(struct FriskGame *game, const struct Event *events,
                        int count);

void *chat_tail_callback(struct AmmServer_DynamicRequest *rqst);
void *chat_say_callback(struct AmmServer_DynamicRequest *rqst);
void *chat_clip_callback(struct AmmServer_DynamicRequest *rqst);
void *chat_clip_get_callback(struct AmmServer_DynamicRequest *rqst);

#endif
