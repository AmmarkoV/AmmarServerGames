#include <stdio.h>
#include <stdlib.h>

#include "engine/map.h"

int main(int argc, char **argv)
{
    const char *directory = (argc > 1) ? argv[1] : "res/maps/lotr";
    struct Map *map = (struct Map *)malloc(sizeof(*map));
    char error[512];

    if (map == NULL) {
        fprintf(stderr, "mapcheck: out of memory\n");
        return 1;
    }
    if (map_load(directory, map, error, sizeof(error)) != 0) {
        fprintf(stderr, "mapcheck: %s\n", error);
        free(map);
        return 1;
    }
    if (map_validate(map, error, sizeof(error)) != 0) {
        fprintf(stderr, "mapcheck: %s\n", error);
        free(map);
        return 1;
    }
    printf("OK %s: %d territories, %d regions, %d factions, %d links, "
           "%d territory cards, %d adventure cards\n",
           map->name, map->territoryCount, map->regionCount,
           map->factionCount, map->linkCount, map->territoryCardCount,
           map->adventureCardCount);
    free(map);
    return 0;
}
