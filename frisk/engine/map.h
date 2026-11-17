#ifndef FRISK_MAP_H
#define FRISK_MAP_H

#include <stddef.h>

#define MAP_MAX_TERRITORIES 96
#define MAP_MAX_REGIONS 16
#define MAP_MAX_ADJ 10
#define MAP_MAX_CARDS 128
#define MAP_MAX_FACTIONS 8
#define MAP_MAX_LINKS 64
#define MAP_MAX_PLAYERS 4

#define MAP_NAME_SIZE 128
#define MAP_TERRITORY_NAME_SIZE 64
#define MAP_COLOR_SIZE 16
#define MAP_PATH_SIZE 1024
#define MAP_DIR_SIZE 512
#define MAP_TEXT_SIZE 512
#define MAP_TITLE_SIZE 128
#define MAP_ID_SIZE 32
#define MAP_KIND_SIZE 16
#define MAP_EFFECT_SIZE 32
#define MAP_ARGS_SIZE 512

struct MapImage {
    char file[MAP_NAME_SIZE];
    int width;
    int height;
};

struct MapFaction {
    int id;
    char name[MAP_TERRITORY_NAME_SIZE];
    char color[MAP_COLOR_SIZE];
    char unit1[MAP_PATH_SIZE];
    char unit3[MAP_PATH_SIZE];
    char unit5[MAP_PATH_SIZE];
    char leader[MAP_PATH_SIZE];
};

struct MapRegion {
    int id;
    char name[MAP_TERRITORY_NAME_SIZE];
    int bonus;
    char color[MAP_COLOR_SIZE];
};

struct MapTerritory {
    int id;
    char name[MAP_TERRITORY_NAME_SIZE];
    int region;
    char path[MAP_PATH_SIZE];
    int labelX;
    int labelY;
    int stronghold;
    int site;
    int adjacent[MAP_MAX_ADJ];
    int adjacentCount;
};

struct MapLink {
    int a;
    int b;
    char kind[MAP_KIND_SIZE];
    char path[MAP_PATH_SIZE];
};

struct MapTerritoryCard {
    int territory;        /* -1 for a wild card */
    char symbol;          /* 'A', 'B' or 'C'; 0 for a wild card */
    int wild;
};

struct MapAdventureCard {
    char id[MAP_ID_SIZE];
    char kind[MAP_KIND_SIZE];
    char title[MAP_TITLE_SIZE];
    char text[MAP_TEXT_SIZE];
    char effect[MAP_EFFECT_SIZE];
    char args[MAP_ARGS_SIZE];
};

struct MapSetup {
    int present;
    int armies;
    int neutral;
    int neutralArmies;
    int neutralSharePercent;
};

struct MapRules {
    int setBonusAAA;
    int setBonusBBB;
    int setBonusCCC;
    int setBonusABC;
    int minReinforce;
    int territoriesPerArmy;
    int strongholdReinforce;
    int leadersPerPlayer;
    int leaderCost;       /* armies to recruit a replacement leader */
    int dieCap;           /* 0 means uncapped */
};

struct Map {
    char name[MAP_NAME_SIZE];
    int version;
    struct MapImage image;
    int factionCount;
    struct MapFaction factions[MAP_MAX_FACTIONS];
    int regionCount;
    struct MapRegion regions[MAP_MAX_REGIONS];
    int territoryCount;
    struct MapTerritory territories[MAP_MAX_TERRITORIES];
    int linkCount;
    struct MapLink links[MAP_MAX_LINKS];
    int territoryCardCount;
    struct MapTerritoryCard territoryCards[MAP_MAX_CARDS];
    int adventureCardCount;
    struct MapAdventureCard adventureCards[MAP_MAX_CARDS];
    struct MapSetup setup[MAP_MAX_PLAYERS + 1];
    struct MapRules rules;
    char dir[MAP_DIR_SIZE];
};

/* Load <directory>/map.json and the referenced board file.  Returns 0 on
 * success, -1 on failure with a diagnostic in err. */
int map_load(const char *directory, struct Map *map, char *err, size_t err_size);

/* Validate the semantic rules of PLAN.md step 2.3.  Returns 0 when the pack
 * is usable.  Every failure names the offending territory where possible. */
int map_validate(const struct Map *map, char *err, size_t err_size);

/* Convenience helpers for the engine and the HTTP layer. */
int map_find_territory(const struct Map *map, const char *name);
int map_find_region(const struct Map *map, const char *name);
int map_are_adjacent(const struct Map *map, int a, int b);
int map_has_link(const struct Map *map, int a, int b);

#endif
