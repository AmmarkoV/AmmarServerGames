#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/map.h"
#include "../engine/rules.h"
#include "../engine/serialize.h"

static int failures;
static int checks;

#define CHECK(condition)                                                     \
    do {                                                                     \
        checks++;                                                            \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                             \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define SAVE_SIZE (2u * 1024u * 1024u)

static int make_game(const char *directory, struct Map **out_map,
                     struct Game **out_game, char *err, size_t err_size)
{
    struct Map *map = (struct Map *)malloc(sizeof(*map));
    struct Game *game = (struct Game *)malloc(sizeof(*game));

    if (map == NULL || game == NULL) {
        free(map);
        free(game);
        return -1;
    }
    if (map_load(directory, map, err, err_size) != 0) {
        free(map);
        free(game);
        return -1;
    }
    game_init(game, map, 9, "serialize test", "p0", 424242);
    if (game_join(game, "p0", -1, err, err_size) != 0 ||
        game_join(game, "p1", -1, err, err_size) != 0 ||
        game_start(game, GAME_START_DEAL, 5, err, err_size) != 0) {
        free(map);
        free(game);
        return -1;
    }
    *out_map = map;
    *out_game = game;
    return 0;
}

static void test_round_trip(const char *directory)
{
    struct Map *map = NULL;
    struct Game *game = NULL;
    struct Game *loaded = (struct Game *)malloc(sizeof(*loaded));
    char error[512];
    char *first = (char *)malloc(SAVE_SIZE);
    char *second = (char *)malloc(SAVE_SIZE);

    error[0] = '\0';
    if (make_game(directory, &map, &game, error, sizeof(error)) != 0) {
        fprintf(stderr, "serialize setup failed: %s\n", error);
        CHECK(0);
        free(loaded);
        free(first);
        free(second);
        return;
    }
    CHECK(first != NULL && second != NULL && loaded != NULL);
    if (first == NULL || second == NULL || loaded == NULL) {
        free(map);
        free(game);
        free(loaded);
        free(first);
        free(second);
        return;
    }
    game->kick_target = 1;
    game->kick_votes = 1u;
    game->kick_started = 1700000000;
    CHECK(game->log.count > 0);
    if (game->log.count > 0) {
        game->log.events[0].version = game->version;
    }
    CHECK(game_to_json(game, first, SAVE_SIZE) == 0);
    CHECK(strstr(first, "\"format\":1") != NULL);
    memset(loaded, 0, sizeof(*loaded));
    CHECK(game_from_json(first, loaded, map, error, sizeof(error)) == 0);
    if (error[0] != '\0') {
        fprintf(stderr, "serialize load error: %s\n", error);
    }
    CHECK(loaded->id == game->id);
    CHECK(strcmp(loaded->name, game->name) == 0);
    CHECK(strcmp(loaded->map_pack, game->map_pack) == 0);
    CHECK(loaded->status == game->status);
    CHECK(loaded->phase == game->phase);
    CHECK(loaded->version == game->version);
    CHECK(loaded->player_count == game->player_count);
    CHECK(loaded->territory_count == game->territory_count);
    CHECK(loaded->deck_count == game->deck_count);
    CHECK(loaded->adventure_deck_count == game->adventure_deck_count);
    CHECK(loaded->rng.state == game->rng.state);
    CHECK(loaded->log.count == game->log.count);
    CHECK(loaded->kick_target == 1);
    CHECK(loaded->kick_votes == 1u);
    CHECK(loaded->kick_started == 1700000000);
    if (loaded->log.count > 0) {
        CHECK(loaded->log.events[0].version == game->log.events[0].version);
    }
    CHECK(strcmp(loaded->seats[0].username, game->seats[0].username) == 0);

    CHECK(game_to_json(loaded, second, SAVE_SIZE) == 0);
    CHECK(strcmp(first, second) == 0);

    /* Truncated files must be rejected, not half-loaded. */
    {
        size_t length = strlen(first);
        size_t step = length / 10 + 1;
        size_t cut;
        char *piece = (char *)malloc(length + 1);
        CHECK(piece != NULL);
        if (piece != NULL) {
            for (cut = 0; cut < length; cut += step) {
                memcpy(piece, first, cut);
                piece[cut] = '\0';
                CHECK(game_from_json(piece, loaded, map, error,
                                     sizeof(error)) == -1);
            }
            free(piece);
        }
    }

    /* Syntax corruption. */
    {
        char *bad = (char *)malloc(strlen(first) + 2);
        CHECK(bad != NULL);
        if (bad != NULL) {
            strcpy(bad, first);
            bad[0] = '[';
            CHECK(game_from_json(bad, loaded, map, error,
                                 sizeof(error)) == -1);
            strcpy(bad, first);
            {
                char *format = strstr(bad, "\"format\":1");
                CHECK(format != NULL);
                if (format != NULL) {
                    format[9] = '2';
                    CHECK(game_from_json(bad, loaded, map, error,
                                         sizeof(error)) == -1);
                }
            }
            free(bad);
        }
    }

    /* Unknown fields are ignored and do not change the next save. */
    {
        size_t length = strlen(first);
        char *extra = (char *)malloc(length + 32);
        CHECK(extra != NULL);
        if (extra != NULL) {
            extra[0] = '{';
            strcpy(extra + 1, "\"unknownField\":123,");
            strcat(extra + 1, first + 1);
            memset(loaded, 0, sizeof(*loaded));
            CHECK(game_from_json(extra, loaded, map, error,
                                 sizeof(error)) == 0);
            CHECK(game_to_json(loaded, second, SAVE_SIZE) == 0);
            CHECK(strcmp(first, second) == 0);
            free(extra);
        }
    }

    free(first);
    free(second);
    free(loaded);
    free(game);
    free(map);
}

int test_serialize_suite(const char *directory)
{
    failures = 0;
    checks = 0;
    test_round_trip(directory);
    printf("%s: %d serialize checks, %d failures\n",
           failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures;
}
