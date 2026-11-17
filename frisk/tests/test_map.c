#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../json.h"
#include "../engine/map.h"
#include "../engine/rules.h"

int test_rules_suite(void);
int test_serialize_suite(const char *directory);
int test_cards_suite(void);
int test_printed_suite(void);

static int failures = 0;
static int checks = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        checks++;                                                            \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                             \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void test_json(void)
{
    char error[256];
    JsonValue *value;
    char compact[256];

    value = json_parse("{\"a\":1,\"b\":[true,\"x\"],\"c\":{}}",
                       error, sizeof(error));
    CHECK(value != NULL);
    CHECK(json_get_string(json_get(json_get(value, "c"), "missing")) == NULL);
    CHECK(json_count(json_get(value, "b")) == 2);
    CHECK(json_write_compact(value, compact, sizeof(compact)) == 0);
    CHECK(strcmp(compact, "{\"a\":1,\"b\":[true,\"x\"],\"c\":{}}") == 0);
    json_free(value);

    CHECK(json_parse("{\"a\":1.5}", error, sizeof(error)) == NULL);
    CHECK(json_parse("{\"a\":1,\"a\":2}", error, sizeof(error)) == NULL);
    CHECK(json_parse("[1,2", error, sizeof(error)) == NULL);
    CHECK(json_parse("{\"a\":null}", error, sizeof(error)) == NULL);
    CHECK(json_parse("{} trailing", error, sizeof(error)) == NULL);
    CHECK(json_parse("\"unterminated", error, sizeof(error)) == NULL);
}

static int count_wild_cards(const struct Map *map)
{
    int i, wilds = 0;

    for (i = 0; i < map->territoryCardCount; i++) {
        if (map->territoryCards[i].wild) {
            wilds++;
        }
    }
    return wilds;
}

static int count_strongholds(const struct Map *map)
{
    int i, count = 0;

    for (i = 0; i < map->territoryCount; i++) {
        if (map->territories[i].stronghold) {
            count++;
        }
    }
    return count;
}

static int count_sites(const struct Map *map)
{
    int i, count = 0;

    for (i = 0; i < map->territoryCount; i++) {
        if (map->territories[i].site) {
            count++;
        }
    }
    return count;
}

static void test_map(const char *directory)
{
    struct Map *map = (struct Map *)malloc(sizeof(*map));
    char error[512];
    int shire, buckland, minhiriath;
    int region;

    CHECK(map != NULL);
    if (map == NULL) {
        return;
    }
    error[0] = '\0';
    CHECK(map_load(directory, map, error, sizeof(error)) == 0);
    if (error[0] != '\0') {
        fprintf(stderr, "map_load error: %s\n", error);
        free(map);
        return;
    }
    error[0] = '\0';
    CHECK(map_validate(map, error, sizeof(error)) == 0);
    if (error[0] != '\0') {
        fprintf(stderr, "map_validate error: %s\n", error);
    }

    CHECK(map->territoryCount == 64);
    CHECK(map->regionCount == 9);
    CHECK(map->factionCount == 4);
    CHECK(map->linkCount == 10);
    CHECK(map->territoryCardCount == 66);
    CHECK(map->adventureCardCount == 50);   /* printed deck, no Fellowship */
    CHECK(map->bridgeCount == 17);
    CHECK(map->mountainCount == 9);
    CHECK(map->rules.adventureStartHand == 4);
    CHECK(map->rules.drawOnLeaderConquest == 1);
    CHECK(map_faction_side(map, 0) == MAP_SIDE_GOOD);
    CHECK(map_faction_side(map, 2) == MAP_SIDE_EVIL);
    {
        int fangorn = map_find_territory(map, "Fangorn");
        int fornost = map_find_territory(map, "Fornost");
        int buckland = map_find_territory(map, "Buckland");
        int shire = map_find_territory(map, "The Shire");
        int harad = map_find_territory(map, "Deep Harad");
        int khand = map_find_territory(map, "Khand");
        int umbar = map_find_territory(map, "Umbar");
        int ithilien = map_find_territory(map, "Ithilien");
        int dunland = map_find_territory(map, "Dunland");
        int moria = map_find_territory(map, "Moria");
        CHECK(map->territories[fangorn].stronghold);   /* Isengard */
        CHECK(map->territories[fornost].site);         /* Bree */
        CHECK(!map->territories[buckland].site);
        CHECK(map_bridge_index(map, shire, buckland) >= 0);
        CHECK(!map_are_adjacent(map, harad, khand));
        CHECK(map_are_adjacent(map, harad, umbar));    /* bridge */
        CHECK(map->territories[umbar].port);
        CHECK(map_same_range(map, ithilien, khand));   /* Mountains of Shadow */
        CHECK(map_same_range(map, dunland, moria));    /* Misty Mountains */
        CHECK(!map_same_range(map, dunland, ithilien));
    }
    CHECK(count_wild_cards(map) == 2);
    CHECK(count_strongholds(map) == 11);
    CHECK(count_sites(map) == 22);
    CHECK(map->rules.setBonusAAA == 4);
    CHECK(map->rules.setBonusBBB == 6);
    CHECK(map->rules.setBonusCCC == 8);
    CHECK(map->rules.setBonusABC == 10);
    CHECK(map->rules.leadersPerPlayer == 2);
    CHECK(map->rules.dieCap == 6);
    CHECK(strcmp(map->image.file, "board.svg") == 0);
    CHECK(map->image.width == 1088);
    CHECK(map->image.height == 1600);

    shire = map_find_territory(map, "The Shire");
    buckland = map_find_territory(map, "Buckland");
    minhiriath = map_find_territory(map, "Minhiriath");
    CHECK(shire >= 0);
    CHECK(buckland >= 0);
    CHECK(minhiriath >= 0);
    CHECK(map_are_adjacent(map, shire, buckland) == 1);
    /* The rulebook explicitly says the Shire and Minhiriath are not adjacent
     * (a river separates them and there is no bridge). */
    CHECK(map_are_adjacent(map, shire, minhiriath) == 0);
    CHECK(map_has_link(map, map_find_territory(map, "Minhiriath"),
                       map_find_territory(map, "Mithlond")) == 1);

    region = map_find_region(map, "Rhûn");
    CHECK(region >= 0);
    if (region >= 0) {
        CHECK(map->regions[region].bonus == 2);
    }
    free(map);
}

static void test_lotr_game_view(const char *directory)
{
    struct Map *map = (struct Map *)malloc(sizeof(*map));
    struct Game *game = (struct Game *)malloc(sizeof(*game));
    char error[512];
    char *view = (char *)malloc(512 * 1024);
    char tiny[16];

    CHECK(map != NULL && game != NULL && view != NULL);
    if (map == NULL || game == NULL || view == NULL) {
        free(map);
        free(game);
        free(view);
        return;
    }
    CHECK(map_load(directory, map, error, sizeof(error)) == 0);
    game_init(game, map, 7, "lotr test", "p0", 2026);
    CHECK(game_join(game, "p0", -1, error, sizeof(error)) == 0);
    CHECK(game_join(game, "p1", -1, error, sizeof(error)) == 0);
    CHECK(game_start(game, GAME_START_DEAL, 5, error, sizeof(error)) == 0);
    CHECK(game_view(game, 0, view, 512 * 1024) == 0);
    CHECK(strstr(view, "\"map\":\"lotr\"") != NULL);
    CHECK(strstr(view, "\"players\"") != NULL);
    CHECK(strstr(view, "\"territories\"") != NULL);
    CHECK(strstr(view, "\"legal\":{") != NULL);
    CHECK(game_view(game, -1, view, 512 * 1024) == 0);
    CHECK(strstr(view, "\"hand\":null") != NULL);
    CHECK(game_view(game, 0, tiny, sizeof(tiny)) == -1);
    free(view);
    free(game);
    free(map);
}

int main(int argc, char **argv)
{
    const char *directory = (argc > 1) ? argv[1] : "res/maps/lotr";
    int rules_failures;
    int serialize_failures;
    int card_failures;

    test_json();
    test_map(directory);
    test_lotr_game_view(directory);
    rules_failures = test_rules_suite();
    serialize_failures = test_serialize_suite(directory);
    card_failures = test_cards_suite();
    card_failures += test_printed_suite();
    printf("%s: %d map checks, %d map failures, %d rules failures, "
           "%d serialize failures, %d card failures\n",
           (failures == 0 && rules_failures == 0 &&
            serialize_failures == 0 && card_failures == 0) ? "PASS" : "FAIL",
           checks, failures, rules_failures, serialize_failures,
           card_failures);
    return (failures == 0 && rules_failures == 0 &&
            serialize_failures == 0 && card_failures == 0) ? 0 : 1;
}
