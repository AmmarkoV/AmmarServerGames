#ifndef FRISK_SERIALIZE_H
#define FRISK_SERIALIZE_H

#include <stddef.h>

#include "rules.h"

#define GAME_SERIALIZE_FORMAT 1

/* Serialise one log event as a JSON object (used by the append-only game
 * log).  Returns 0 on success or -1 if the buffer is too small. */
int game_event_to_json(const struct Event *event, char *out, size_t out_size);

/* Serialise every field needed to resume a game.  Returns 0 on success and
 * -1 if the output buffer is too small. */
int game_to_json(const struct Game *g, char *out, size_t out_size);

/* Load a game previously written by game_to_json.  `map` is the already
 * loaded pack.  The destination is untouched on failure. */
int game_from_json(const char *json, struct Game *g, const struct Map *map,
                   char *err, size_t err_size);

#endif
