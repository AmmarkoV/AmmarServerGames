#include "map.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../json.h"
#include "cards.h"

#define MAP_MAX_FILE_BYTES (16u * 1024u * 1024u)
#define MAP_MAX_LABEL 1000000

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

static int read_text_file(const char *path, char **out,
                          char *err, size_t err_size)
{
    FILE *file;
    long length;
    char *buffer;
    size_t got;

    file = fopen(path, "rb");
    if (file == NULL) {
        set_error(err, err_size, "cannot open %s", path);
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0) {
        set_error(err, err_size, "cannot determine size of %s", path);
        fclose(file);
        return -1;
    }
    if ((unsigned long)length > MAP_MAX_FILE_BYTES) {
        set_error(err, err_size, "%s is too large (%ld bytes)", path, length);
        fclose(file);
        return -1;
    }
    rewind(file);
    buffer = (char *)malloc((size_t)length + 1);
    if (buffer == NULL) {
        set_error(err, err_size, "out of memory reading %s", path);
        fclose(file);
        return -1;
    }
    got = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (got != (size_t)length) {
        set_error(err, err_size, "short read on %s", path);
        free(buffer);
        return -1;
    }
    buffer[length] = '\0';
    *out = buffer;
    return 0;
}

static int copy_string(char *dst, size_t dst_size, const char *src,
                       const char *context, char *err, size_t err_size)
{
    size_t length;

    if (src == NULL) {
        set_error(err, err_size, "%s: expected a string", context);
        return -1;
    }
    length = strlen(src);
    if (length >= dst_size) {
        set_error(err, err_size, "%s: string is longer than %zu bytes",
                  context, dst_size - 1);
        return -1;
    }
    memcpy(dst, src, length + 1);
    return 0;
}

static int required_string(const JsonValue *object, const char *key,
                           char *dst, size_t dst_size, const char *context,
                           char *err, size_t err_size)
{
    const char *value = json_get_string(json_get(object, key));
    char field[128];

    if (value == NULL) {
        snprintf(field, sizeof(field), "%s: field \"%s\" must be a string",
                 context, key);
        set_error(err, err_size, "%s", field);
        return -1;
    }
    snprintf(field, sizeof(field), "%s: field \"%s\"", context, key);
    return copy_string(dst, dst_size, value, field, err, err_size);
}

static int required_int(const JsonValue *object, const char *key,
                        int minimum, int maximum, int *out,
                        const char *context, char *err, size_t err_size)
{
    long long value;

    if (json_get_int(json_get(object, key), &value) != 0) {
        set_error(err, err_size,
                  "%s: field \"%s\" must be an integer", context, key);
        return -1;
    }
    if (value < minimum || value > maximum) {
        set_error(err, err_size,
                  "%s: field \"%s\" is out of range (%lld, expected %d..%d)",
                  context, key, value, minimum, maximum);
        return -1;
    }
    *out = (int)value;
    return 0;
}

static int optional_int(const JsonValue *object, const char *key,
                        int minimum, int maximum, int fallback, int *out,
                        const char *context, char *err, size_t err_size)
{
    const JsonValue *value = json_get(object, key);

    if (value == NULL) {
        *out = fallback;
        return 0;
    }
    return required_int(object, key, minimum, maximum, out, context,
                        err, err_size);
}

static int optional_bool(const JsonValue *object, const char *key,
                         int fallback, int *out, const char *context,
                         char *err, size_t err_size)
{
    const JsonValue *value = json_get(object, key);
    int boolean;

    if (value == NULL) {
        *out = fallback;
        return 0;
    }
    if (json_get_bool(value, &boolean) != 0) {
        set_error(err, err_size,
                  "%s: field \"%s\" must be a boolean", context, key);
        return -1;
    }
    *out = boolean;
    return 0;
}

static int load_array(const JsonValue *root, const char *key, size_t maximum,
                      const JsonValue **out, const char *context,
                      char *err, size_t err_size)
{
    const JsonValue *value = json_get(root, key);

    if (value == NULL || value->type != JSON_ARRAY) {
        set_error(err, err_size, "%s: field \"%s\" must be an array",
                  context, key);
        return -1;
    }
    if (json_count(value) > maximum) {
        set_error(err, err_size, "%s: \"%s\" has more than %zu entries",
                  context, key, maximum);
        return -1;
    }
    *out = value;
    return 0;
}

static int load_object(const JsonValue *root, const char *key,
                       const JsonValue **out, const char *context,
                       char *err, size_t err_size)
{
    const JsonValue *value = json_get(root, key);

    if (value == NULL || value->type != JSON_OBJECT) {
        set_error(err, err_size, "%s: field \"%s\" must be an object",
                  context, key);
        return -1;
    }
    *out = value;
    return 0;
}

static int load_factions(const JsonValue *root, struct Map *map,
                         char *err, size_t err_size)
{
    const JsonValue *array;
    size_t i, count;

    if (load_array(root, "factions", MAP_MAX_FACTIONS, &array,
                   "map.json", err, err_size) != 0) {
        return -1;
    }
    count = json_count(array);
    if (count < 2) {
        set_error(err, err_size, "map.json: at least 2 factions are required");
        return -1;
    }
    map->factionCount = (int)count;
    for (i = 0; i < count; i++) {
        const JsonValue *faction = json_at(array, i);
        const JsonValue *sprites;
        struct MapFaction *out = &map->factions[i];
        char context[64];

        snprintf(context, sizeof(context), "faction %zu", i);
        if (faction == NULL || faction->type != JSON_OBJECT) {
            set_error(err, err_size, "%s: must be an object", context);
            return -1;
        }
        if (required_int(faction, "id", 0, (int)count - 1, &out->id,
                         context, err, err_size) != 0) {
            return -1;
        }
        if (out->id != (int)i) {
            set_error(err, err_size,
                      "%s: ids must be contiguous starting at 0 (got %d)",
                      context, out->id);
            return -1;
        }
        if (required_string(faction, "name", out->name, sizeof(out->name),
                            context, err, err_size) != 0 ||
            required_string(faction, "color", out->color, sizeof(out->color),
                            context, err, err_size) != 0) {
            return -1;
        }
        if (load_object(faction, "sprites", &sprites, context,
                        err, err_size) != 0) {
            return -1;
        }
        {
            const JsonValue *side = json_get(faction, "side");
            const char *text = side != NULL ? json_get_string(side) : NULL;
            if (side == NULL) {
                out->side = MAP_SIDE_NONE;
            } else if (text != NULL && strcmp(text, "good") == 0) {
                out->side = MAP_SIDE_GOOD;
            } else if (text != NULL && strcmp(text, "evil") == 0) {
                out->side = MAP_SIDE_EVIL;
            } else {
                set_error(err, err_size,
                          "%s: \"side\" must be \"good\" or \"evil\"",
                          context);
                return -1;
            }
        }
        if (required_string(sprites, "unit1", out->unit1, sizeof(out->unit1),
                            context, err, err_size) != 0 ||
            required_string(sprites, "unit3", out->unit3, sizeof(out->unit3),
                            context, err, err_size) != 0 ||
            required_string(sprites, "unit5", out->unit5, sizeof(out->unit5),
                            context, err, err_size) != 0 ||
            required_string(sprites, "leader", out->leader,
                            sizeof(out->leader), context,
                            err, err_size) != 0) {
            return -1;
        }
    }
    return 0;
}

static int load_regions(const JsonValue *root, struct Map *map,
                        char *err, size_t err_size)
{
    const JsonValue *array;
    size_t i, count;

    if (load_array(root, "regions", MAP_MAX_REGIONS, &array,
                   "map.json", err, err_size) != 0) {
        return -1;
    }
    count = json_count(array);
    if (count == 0) {
        set_error(err, err_size, "map.json: at least one region is required");
        return -1;
    }
    map->regionCount = (int)count;
    for (i = 0; i < count; i++) {
        const JsonValue *region = json_at(array, i);
        struct MapRegion *out = &map->regions[i];
        char context[64];

        snprintf(context, sizeof(context), "region %zu", i);
        if (region == NULL || region->type != JSON_OBJECT) {
            set_error(err, err_size, "%s: must be an object", context);
            return -1;
        }
        if (required_int(region, "id", 0, (int)count - 1, &out->id,
                         context, err, err_size) != 0) {
            return -1;
        }
        if (out->id != (int)i) {
            set_error(err, err_size,
                      "%s: ids must be contiguous starting at 0 (got %d)",
                      context, out->id);
            return -1;
        }
        if (required_string(region, "name", out->name, sizeof(out->name),
                            context, err, err_size) != 0 ||
            required_string(region, "color", out->color, sizeof(out->color),
                            context, err, err_size) != 0 ||
            required_int(region, "bonus", 0, 1000, &out->bonus,
                         context, err, err_size) != 0) {
            return -1;
        }
    }
    return 0;
}

static int load_territories(const JsonValue *root, struct Map *map,
                            char *err, size_t err_size)
{
    const JsonValue *array;
    size_t i, count;

    if (load_array(root, "territories", MAP_MAX_TERRITORIES, &array,
                   "map.json", err, err_size) != 0) {
        return -1;
    }
    count = json_count(array);
    if (count < 2) {
        set_error(err, err_size,
                  "map.json: at least two territories are required");
        return -1;
    }
    map->territoryCount = (int)count;
    for (i = 0; i < count; i++) {
        const JsonValue *territory = json_at(array, i);
        const JsonValue *label;
        const JsonValue *adjacent;
        struct MapTerritory *out = &map->territories[i];
        char context[128];
        size_t k, adj_count;

        if (territory == NULL || territory->type != JSON_OBJECT) {
            set_error(err, err_size, "territory %zu: must be an object", i);
            return -1;
        }
        if (required_int(territory, "id", 0, (int)count - 1, &out->id,
                         "territory", err, err_size) != 0) {
            return -1;
        }
        if (out->id != (int)i) {
            set_error(err, err_size,
                      "territory %zu: ids must be contiguous starting at 0 "
                      "(got %d)", i, out->id);
            return -1;
        }
        if (required_string(territory, "name", out->name, sizeof(out->name),
                            "territory", err, err_size) != 0) {
            return -1;
        }
        snprintf(context, sizeof(context), "territory %d (%s)",
                 out->id, out->name);
        if (required_int(territory, "region", 0, map->regionCount - 1,
                         &out->region, context, err, err_size) != 0 ||
            required_string(territory, "path", out->path, sizeof(out->path),
                            context, err, err_size) != 0) {
            return -1;
        }
        label = json_get(territory, "label");
        if (label == NULL || label->type != JSON_ARRAY ||
            json_count(label) != 2) {
            set_error(err, err_size,
                      "%s: field \"label\" must be an [x,y] array", context);
            return -1;
        }
        {
            long long x, y;
            if (json_get_int(json_at(label, 0), &x) != 0 ||
                json_get_int(json_at(label, 1), &y) != 0) {
                set_error(err, err_size,
                          "%s: field \"label\" must contain two integers",
                          context);
                return -1;
            }
            if (x < -MAP_MAX_LABEL || x > MAP_MAX_LABEL ||
                y < -MAP_MAX_LABEL || y > MAP_MAX_LABEL) {
                set_error(err, err_size,
                          "%s: field \"label\" is out of range", context);
                return -1;
            }
            out->labelX = (int)x;
            out->labelY = (int)y;
        }
        if (optional_bool(territory, "stronghold", 0, &out->stronghold,
                          context, err, err_size) != 0 ||
            optional_bool(territory, "site", 0, &out->site,
                          context, err, err_size) != 0 ||
            optional_bool(territory, "port", 0, &out->port,
                          context, err, err_size) != 0) {
            return -1;
        }
        adjacent = json_get(territory, "adjacent");
        if (adjacent == NULL || adjacent->type != JSON_ARRAY) {
            set_error(err, err_size,
                      "%s: field \"adjacent\" must be an array", context);
            return -1;
        }
        adj_count = json_count(adjacent);
        if (adj_count > MAP_MAX_ADJ) {
            set_error(err, err_size,
                      "%s: more than %d adjacent territories", context,
                      MAP_MAX_ADJ);
            return -1;
        }
        out->adjacentCount = (int)adj_count;
        for (k = 0; k < adj_count; k++) {
            long long neighbor;
            if (json_get_int(json_at(adjacent, k), &neighbor) != 0) {
                set_error(err, err_size,
                          "%s: adjacent entry %zu is not an integer",
                          context, k);
                return -1;
            }
            if (neighbor < 0 || neighbor >= MAP_MAX_TERRITORIES) {
                set_error(err, err_size,
                          "%s: adjacent id %lld is out of range",
                          context, neighbor);
                return -1;
            }
            out->adjacent[k] = (int)neighbor;
        }
    }
    return 0;
}

static int load_links(const JsonValue *root, struct Map *map,
                      char *err, size_t err_size)
{
    const JsonValue *array;
    size_t i, count;

    if (load_array(root, "links", MAP_MAX_LINKS, &array,
                   "map.json", err, err_size) != 0) {
        return -1;
    }
    count = json_count(array);
    map->linkCount = (int)count;
    for (i = 0; i < count; i++) {
        const JsonValue *link = json_at(array, i);
        struct MapLink *out = &map->links[i];
        char context[64];

        snprintf(context, sizeof(context), "link %zu", i);
        if (link == NULL || link->type != JSON_OBJECT) {
            set_error(err, err_size, "%s: must be an object", context);
            return -1;
        }
        if (required_int(link, "a", 0, MAP_MAX_TERRITORIES - 1, &out->a,
                         context, err, err_size) != 0 ||
            required_int(link, "b", 0, MAP_MAX_TERRITORIES - 1, &out->b,
                         context, err, err_size) != 0 ||
            required_string(link, "kind", out->kind, sizeof(out->kind),
                            context, err, err_size) != 0 ||
            required_string(link, "path", out->path, sizeof(out->path),
                            context, err, err_size) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Optional "bridges": [{"a":0,"b":1}, ...] and "mountains":
 * [{"name":"...","territories":[ids]}, ...] (PLAN.md 10b.2). */
static int load_bridges_and_mountains(const JsonValue *root, struct Map *map,
                                      char *err, size_t err_size)
{
    const JsonValue *array;
    size_t i, count;

    map->bridgeCount = 0;
    map->mountainCount = 0;
    if (json_get(root, "bridges") != NULL) {
        if (load_array(root, "bridges", MAP_MAX_BRIDGES, &array,
                       "map.json", err, err_size) != 0) {
            return -1;
        }
        count = json_count(array);
        for (i = 0; i < count; i++) {
            char context[64];
            struct MapBridge *out = &map->bridges[i];
            snprintf(context, sizeof(context), "bridge %zu", i);
            if (required_int(json_at(array, i), "a", 0,
                             MAP_MAX_TERRITORIES - 1, &out->a, context,
                             err, err_size) != 0 ||
                required_int(json_at(array, i), "b", 0,
                             MAP_MAX_TERRITORIES - 1, &out->b, context,
                             err, err_size) != 0) {
                return -1;
            }
        }
        map->bridgeCount = (int)count;
    }
    if (json_get(root, "mountains") != NULL) {
        if (load_array(root, "mountains", MAP_MAX_MOUNTAINS, &array,
                       "map.json", err, err_size) != 0) {
            return -1;
        }
        count = json_count(array);
        for (i = 0; i < count; i++) {
            const JsonValue *range = json_at(array, i);
            const JsonValue *members;
            struct MapMountain *out = &map->mountains[i];
            char context[64];
            size_t k;

            snprintf(context, sizeof(context), "mountain range %zu", i);
            if (range == NULL || range->type != JSON_OBJECT ||
                required_string(range, "name", out->name, sizeof(out->name),
                                context, err, err_size) != 0) {
                if (range == NULL || range->type != JSON_OBJECT) {
                    set_error(err, err_size, "%s: must be an object",
                              context);
                }
                return -1;
            }
            if (load_array(range, "territories", MAP_MAX_TERRITORIES,
                           &members, context, err, err_size) != 0) {
                return -1;
            }
            out->count = (int)json_count(members);
            for (k = 0; k < json_count(members); k++) {
                long long id;
                if (json_get_int(json_at(members, k), &id) != 0 ||
                    id < 0 || id >= MAP_MAX_TERRITORIES) {
                    set_error(err, err_size,
                              "%s: territories must be territory ids",
                              context);
                    return -1;
                }
                out->territories[k] = (int)id;
            }
        }
        map->mountainCount = (int)count;
    }
    return 0;
}

static int load_territory_cards(const JsonValue *root, struct Map *map,
                                char *err, size_t err_size)
{
    const JsonValue *array;
    size_t i, count;

    if (load_array(root, "territoryCards", MAP_MAX_CARDS, &array,
                   "map.json", err, err_size) != 0) {
        return -1;
    }
    count = json_count(array);
    map->territoryCardCount = (int)count;
    for (i = 0; i < count; i++) {
        const JsonValue *card = json_at(array, i);
        struct MapTerritoryCard *out = &map->territoryCards[i];
        const JsonValue *wild_value;
        char context[64];

        snprintf(context, sizeof(context), "territory card %zu", i);
        if (card == NULL || card->type != JSON_OBJECT) {
            set_error(err, err_size, "%s: must be an object", context);
            return -1;
        }
        wild_value = json_get(card, "wild");
        if (wild_value != NULL) {
            int wild;
            if (json_get_bool(wild_value, &wild) != 0) {
                set_error(err, err_size,
                          "%s: \"wild\" must be a boolean", context);
                return -1;
            }
            if (!wild) {
                set_error(err, err_size,
                          "%s: \"wild\" must be true when present", context);
                return -1;
            }
            out->wild = 1;
            out->territory = -1;
            out->symbol = '\0';
            continue;
        }
        if (required_int(card, "territory", 0, MAP_MAX_TERRITORIES - 1,
                         &out->territory, context, err, err_size) != 0) {
            return -1;
        }
        {
            char symbol[4];
            if (required_string(card, "symbol", symbol, sizeof(symbol),
                                context, err, err_size) != 0) {
                return -1;
            }
            if (symbol[0] == '\0' || symbol[1] != '\0' ||
                (symbol[0] != 'A' && symbol[0] != 'B' &&
                 symbol[0] != 'C')) {
                set_error(err, err_size,
                          "%s: symbol must be one of A, B, C", context);
                return -1;
            }
            out->symbol = symbol[0];
        }
    }
    return 0;
}

static int load_adventure_cards(const JsonValue *root, struct Map *map,
                                char *err, size_t err_size)
{
    const JsonValue *array;
    size_t i, count;

    if (load_array(root, "adventureCards", MAP_MAX_CARDS, &array,
                   "map.json", err, err_size) != 0) {
        return -1;
    }
    count = json_count(array);
    map->adventureCardCount = (int)count;
    for (i = 0; i < count; i++) {
        const JsonValue *card = json_at(array, i);
        const JsonValue *args;
        struct MapAdventureCard *out = &map->adventureCards[i];
        char context[64];

        snprintf(context, sizeof(context), "adventure card %zu", i);
        if (card == NULL || card->type != JSON_OBJECT) {
            set_error(err, err_size, "%s: must be an object", context);
            return -1;
        }
        if (required_string(card, "id", out->id, sizeof(out->id),
                            context, err, err_size) != 0 ||
            required_string(card, "kind", out->kind, sizeof(out->kind),
                            context, err, err_size) != 0 ||
            required_string(card, "title", out->title, sizeof(out->title),
                            context, err, err_size) != 0 ||
            required_string(card, "text", out->text, sizeof(out->text),
                            context, err, err_size) != 0 ||
            required_string(card, "effect", out->effect,
                            sizeof(out->effect), context,
                            err, err_size) != 0) {
            return -1;
        }
        args = json_get(card, "args");
        if (args == NULL) {
            strcpy(out->args, "{}");
        } else if (args->type != JSON_OBJECT) {
            set_error(err, err_size, "%s: \"args\" must be an object",
                      context);
            return -1;
        } else if (json_write_compact(args, out->args,
                                      sizeof(out->args)) != 0) {
            set_error(err, err_size,
                      "%s: serialised args do not fit in %zu bytes",
                      context, sizeof(out->args));
            return -1;
        }
    }
    return 0;
}

static int load_setup_and_rules(const JsonValue *root, struct Map *map,
                                char *err, size_t err_size)
{
    const JsonValue *setup;
    const JsonValue *rules;
    const JsonValue *set_bonus;
    int players;

    if (load_object(root, "setup", &setup, "map.json",
                    err, err_size) != 0 ||
        load_object(root, "rules", &rules, "map.json",
                    err, err_size) != 0) {
        return -1;
    }
    for (players = 2; players <= MAP_MAX_PLAYERS; players++) {
        char key[8];
        const JsonValue *entry;
        struct MapSetup *out = &map->setup[players];
        char context[64];

        snprintf(key, sizeof(key), "%d", players);
        snprintf(context, sizeof(context), "setup for %d players", players);
        entry = json_get(setup, key);
        if (entry == NULL || entry->type != JSON_OBJECT) {
            set_error(err, err_size, "map.json: %s is missing", context);
            return -1;
        }
        out->present = 1;
        if (required_int(entry, "armies", 1, 1000000, &out->armies,
                         context, err, err_size) != 0 ||
            optional_bool(entry, "neutral", 0, &out->neutral,
                          context, err, err_size) != 0 ||
            optional_int(entry, "neutralArmies", 0, 1000000, 0,
                         &out->neutralArmies, context,
                         err, err_size) != 0 ||
            optional_int(entry, "neutralShare", 0, 100, 0,
                         &out->neutralSharePercent, context,
                         err, err_size) != 0) {
            return -1;
        }
    }
    if (load_object(rules, "setBonus", &set_bonus, "rules",
                    err, err_size) != 0) {
        return -1;
    }
    if (required_int(set_bonus, "AAA", 0, 1000, &map->rules.setBonusAAA,
                     "rules.setBonus", err, err_size) != 0 ||
        required_int(set_bonus, "BBB", 0, 1000, &map->rules.setBonusBBB,
                     "rules.setBonus", err, err_size) != 0 ||
        required_int(set_bonus, "CCC", 0, 1000, &map->rules.setBonusCCC,
                     "rules.setBonus", err, err_size) != 0 ||
        required_int(set_bonus, "ABC", 0, 1000, &map->rules.setBonusABC,
                     "rules.setBonus", err, err_size) != 0 ||
        required_int(rules, "minReinforce", 0, 1000,
                     &map->rules.minReinforce, "rules",
                     err, err_size) != 0 ||
        required_int(rules, "territoriesPerArmy", 1, 1000,
                     &map->rules.territoriesPerArmy, "rules",
                     err, err_size) != 0 ||
        required_int(rules, "strongholdReinforce", 0, 1000,
                     &map->rules.strongholdReinforce, "rules",
                     err, err_size) != 0 ||
        required_int(rules, "leadersPerPlayer", 1, 100,
                     &map->rules.leadersPerPlayer, "rules",
                     err, err_size) != 0 ||
        optional_int(rules, "leaderCost", 0, 1000, 3,
                     &map->rules.leaderCost, "rules",
                     err, err_size) != 0 ||
        optional_int(rules, "adventureStartHand", 0, 8, 0,
                     &map->rules.adventureStartHand, "rules",
                     err, err_size) != 0 ||
        optional_bool(rules, "drawOnLeaderConquest", 0,
                      &map->rules.drawOnLeaderConquest, "rules",
                      err, err_size) != 0 ||
        optional_int(rules, "dieCap", 0, 100, 6, &map->rules.dieCap,
                     "rules", err, err_size) != 0) {
        return -1;
    }
    return 0;
}

int map_load(const char *directory, struct Map *map,
             char *err, size_t err_size)
{
    char json_path[MAP_DIR_SIZE + 32];
    char image_path[MAP_DIR_SIZE + MAP_NAME_SIZE + 2];
    char parse_error[256];
    char *text = NULL;
    JsonValue *root = NULL;
    const JsonValue *image;
    struct stat st;
    int rc = -1;

    if (map == NULL || directory == NULL) {
        set_error(err, err_size, "map_load: invalid arguments");
        return -1;
    }
    memset(map, 0, sizeof(*map));
    if (strlen(directory) >= sizeof(map->dir)) {
        set_error(err, err_size, "map_load: pack directory is too long");
        return -1;
    }
    strcpy(map->dir, directory);
    snprintf(json_path, sizeof(json_path), "%s/map.json", directory);

    if (read_text_file(json_path, &text, err, err_size) != 0) {
        return -1;
    }
    root = json_parse(text, parse_error, sizeof(parse_error));
    free(text);
    if (root == NULL) {
        set_error(err, err_size, "%s: %s", json_path, parse_error);
        return -1;
    }
    if (root->type != JSON_OBJECT) {
        set_error(err, err_size, "%s: top-level value must be an object",
                  json_path);
        goto done;
    }

    if (required_string(root, "name", map->name, sizeof(map->name),
                        "map.json", err, err_size) != 0 ||
        required_int(root, "version", 1, 1000, &map->version,
                     "map.json", err, err_size) != 0) {
        goto done;
    }
    if (load_object(root, "image", &image, "map.json",
                    err, err_size) != 0 ||
        required_string(image, "file", map->image.file,
                        sizeof(map->image.file), "image",
                        err, err_size) != 0 ||
        required_int(image, "width", 1, 100000, &map->image.width,
                     "image", err, err_size) != 0 ||
        required_int(image, "height", 1, 100000, &map->image.height,
                     "image", err, err_size) != 0) {
        goto done;
    }
    if (load_factions(root, map, err, err_size) != 0 ||
        load_regions(root, map, err, err_size) != 0 ||
        load_territories(root, map, err, err_size) != 0 ||
        load_links(root, map, err, err_size) != 0 ||
        load_bridges_and_mountains(root, map, err, err_size) != 0 ||
        load_territory_cards(root, map, err, err_size) != 0 ||
        load_adventure_cards(root, map, err, err_size) != 0 ||
        load_setup_and_rules(root, map, err, err_size) != 0) {
        goto done;
    }

    snprintf(image_path, sizeof(image_path), "%s/%s",
             directory, map->image.file);
    if (stat(image_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        set_error(err, err_size,
                  "map.json: board file %s does not exist or is not a file",
                  image_path);
        goto done;
    }
    rc = 0;

done:
    json_free(root);
    return rc;
}

static const char *territory_name(const struct Map *map, int id)
{
    if (map == NULL || id < 0 || id >= map->territoryCount) {
        return "?";
    }
    return map->territories[id].name;
}

int map_find_territory(const struct Map *map, const char *name)
{
    int i;

    if (map == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < map->territoryCount; i++) {
        if (strcmp(map->territories[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int map_find_region(const struct Map *map, const char *name)
{
    int i;

    if (map == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < map->regionCount; i++) {
        if (strcmp(map->regions[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int map_are_adjacent(const struct Map *map, int a, int b)
{
    int i;

    if (map == NULL || a < 0 || b < 0 ||
        a >= map->territoryCount || b >= map->territoryCount) {
        return 0;
    }
    for (i = 0; i < map->territories[a].adjacentCount; i++) {
        if (map->territories[a].adjacent[i] == b) {
            return 1;
        }
    }
    return map_has_link(map, a, b);
}

int map_bridge_index(const struct Map *map, int a, int b)
{
    int i;

    if (map == NULL) {
        return -1;
    }
    for (i = 0; i < map->bridgeCount; i++) {
        if ((map->bridges[i].a == a && map->bridges[i].b == b) ||
            (map->bridges[i].a == b && map->bridges[i].b == a)) {
            return i;
        }
    }
    return -1;
}

int map_same_range(const struct Map *map, int a, int b)
{
    int r;

    if (map == NULL || a == b) {
        return 0;
    }
    for (r = 0; r < map->mountainCount; r++) {
        int k, has_a = 0, has_b = 0;
        for (k = 0; k < map->mountains[r].count; k++) {
            has_a |= map->mountains[r].territories[k] == a;
            has_b |= map->mountains[r].territories[k] == b;
        }
        if (has_a && has_b) {
            return 1;
        }
    }
    return 0;
}

int map_faction_side(const struct Map *map, int faction)
{
    if (map == NULL || faction < 0 || faction >= map->factionCount) {
        return MAP_SIDE_NONE;
    }
    return map->factions[faction].side;
}

int map_has_link(const struct Map *map, int a, int b)
{
    int i;

    if (map == NULL) {
        return 0;
    }
    for (i = 0; i < map->linkCount; i++) {
        if ((map->links[i].a == a && map->links[i].b == b) ||
            (map->links[i].a == b && map->links[i].b == a)) {
            return 1;
        }
    }
    return 0;
}

static int check_unique_names(const struct Map *map, char *err, size_t err_size)
{
    int i, j;

    for (i = 0; i < map->territoryCount; i++) {
        for (j = i + 1; j < map->territoryCount; j++) {
            if (strcmp(map->territories[i].name,
                       map->territories[j].name) == 0) {
                set_error(err, err_size,
                          "territory %d and %d share the name \"%s\"",
                          i, j, map->territories[i].name);
                return -1;
            }
        }
    }
    for (i = 0; i < map->regionCount; i++) {
        for (j = i + 1; j < map->regionCount; j++) {
            if (strcmp(map->regions[i].name, map->regions[j].name) == 0) {
                set_error(err, err_size,
                          "region %d and %d share the name \"%s\"",
                          i, j, map->regions[i].name);
                return -1;
            }
        }
    }
    for (i = 0; i < map->adventureCardCount; i++) {
        for (j = i + 1; j < map->adventureCardCount; j++) {
            if (strcmp(map->adventureCards[i].id,
                       map->adventureCards[j].id) == 0) {
                set_error(err, err_size,
                          "adventure card id \"%s\" appears more than once",
                          map->adventureCards[i].id);
                return -1;
            }
        }
    }
    return 0;
}

int map_validate(const struct Map *map, char *err, size_t err_size)
{
    unsigned char edge[MAP_MAX_TERRITORIES][MAP_MAX_TERRITORIES];
    int covered[MAP_MAX_TERRITORIES];
    int region_used[MAP_MAX_REGIONS];
    int queue[MAP_MAX_TERRITORIES];
    int seen[MAP_MAX_TERRITORIES];
    int strongholds = 0, sites = 0, wilds = 0;
    int symbol_a = 0, symbol_b = 0, symbol_c = 0;
    int i, j, k, head, tail;

    if (map == NULL) {
        set_error(err, err_size, "map_validate: null map");
        return -1;
    }
    memset(edge, 0, sizeof(edge));
    memset(covered, 0, sizeof(covered));
    memset(region_used, 0, sizeof(region_used));
    memset(seen, 0, sizeof(seen));

    if (map->name[0] == '\0') {
        set_error(err, err_size, "map has no name");
        return -1;
    }
    if (map->version < 1) {
        set_error(err, err_size, "map version must be at least 1");
        return -1;
    }
    if (map->image.file[0] == '\0' || map->image.width <= 0 ||
        map->image.height <= 0) {
        set_error(err, err_size, "map image metadata is invalid");
        return -1;
    }
    if (map->factionCount < 2) {
        set_error(err, err_size, "map has fewer than 2 factions");
        return -1;
    }
    if (map->regionCount < 1) {
        set_error(err, err_size, "map has no regions");
        return -1;
    }
    if (map->territoryCount < 2) {
        set_error(err, err_size, "map has fewer than 2 territories");
        return -1;
    }
    if (map->territoryCount < 4 * map->rules.leadersPerPlayer) {
        set_error(err, err_size,
                  "map has only %d territories; need at least %d for "
                  "%d leaders per player", map->territoryCount,
                  4 * map->rules.leadersPerPlayer,
                  map->rules.leadersPerPlayer);
        return -1;
    }

    for (i = 0; i < map->regionCount; i++) {
        if (map->regions[i].id != i) {
            set_error(err, err_size, "region %d has id %d", i,
                      map->regions[i].id);
            return -1;
        }
        if (map->regions[i].bonus <= 0) {
            set_error(err, err_size, "region \"%s\" has a non-positive bonus",
                      map->regions[i].name);
            return -1;
        }
    }
    for (i = 0; i < map->factionCount; i++) {
        if (map->factions[i].id != i) {
            set_error(err, err_size, "faction %d has id %d", i,
                      map->factions[i].id);
            return -1;
        }
    }
    if (check_unique_names(map, err, err_size) != 0) {
        return -1;
    }

    for (i = 0; i < map->territoryCount; i++) {
        const struct MapTerritory *t = &map->territories[i];

        if (t->id != i) {
            set_error(err, err_size, "territory %d has id %d", i, t->id);
            return -1;
        }
        if (t->name[0] == '\0') {
            set_error(err, err_size, "territory %d has no name", i);
            return -1;
        }
        if (t->region < 0 || t->region >= map->regionCount) {
            set_error(err, err_size,
                      "territory \"%s\" references region %d, which does "
                      "not exist", t->name, t->region);
            return -1;
        }
        region_used[t->region] = 1;
        if (t->path[0] == '\0') {
            set_error(err, err_size, "territory \"%s\" has an empty path",
                      t->name);
            return -1;
        }
        if (t->labelX < 0 || t->labelX >= map->image.width ||
            t->labelY < 0 || t->labelY >= map->image.height) {
            set_error(err, err_size,
                      "territory \"%s\" label [%d,%d] is outside the %dx%d "
                      "board", t->name, t->labelX, t->labelY,
                      map->image.width, map->image.height);
            return -1;
        }
        if (t->stronghold) {
            strongholds++;
        }
        if (t->site) {
            sites++;
        }
        if (t->adjacentCount > MAP_MAX_ADJ) {
            set_error(err, err_size,
                      "territory \"%s\" has too many neighbours", t->name);
            return -1;
        }
        for (k = 0; k < t->adjacentCount; k++) {
            j = t->adjacent[k];
            if (j < 0 || j >= map->territoryCount) {
                set_error(err, err_size,
                          "territory \"%s\" is adjacent to id %d, which "
                          "does not exist", t->name, j);
                return -1;
            }
            if (j == i) {
                set_error(err, err_size,
                          "territory \"%s\" is adjacent to itself", t->name);
                return -1;
            }
            if (edge[i][j]) {
                set_error(err, err_size,
                          "territory \"%s\" lists neighbour \"%s\" twice",
                          t->name, territory_name(map, j));
                return -1;
            }
            edge[i][j] = 1;
        }
    }
    if (strongholds < 1) {
        set_error(err, err_size, "map has no strongholds");
        return -1;
    }
    if (sites < 1) {
        set_error(err, err_size, "map has no sites of power");
        return -1;
    }
    for (i = 0; i < map->regionCount; i++) {
        if (!region_used[i]) {
            set_error(err, err_size, "region \"%s\" contains no territories",
                      map->regions[i].name);
            return -1;
        }
    }

    /* Adjacency must be symmetric and connected (links count too). */
    for (i = 0; i < map->territoryCount; i++) {
        for (j = 0; j < map->territoryCount; j++) {
            if (edge[i][j] && !edge[j][i]) {
                set_error(err, err_size,
                          "adjacency %s -> %s is not symmetric",
                          territory_name(map, i), territory_name(map, j));
                return -1;
            }
        }
    }
    for (k = 0; k < map->linkCount; k++) {
        int a = map->links[k].a;
        int b = map->links[k].b;

        if (a < 0 || a >= map->territoryCount ||
            b < 0 || b >= map->territoryCount) {
            set_error(err, err_size,
                      "link %d references a territory that does not exist",
                      k);
            return -1;
        }
        if (a == b) {
            set_error(err, err_size, "link %d connects \"%s\" to itself",
                      k, territory_name(map, a));
            return -1;
        }
        if (map->links[k].kind[0] == '\0' ||
            map->links[k].path[0] == '\0') {
            set_error(err, err_size, "link %d is missing a kind or path", k);
            return -1;
        }
        for (i = 0; i < k; i++) {
            if ((map->links[i].a == a && map->links[i].b == b) ||
                (map->links[i].a == b && map->links[i].b == a)) {
                set_error(err, err_size,
                          "link between \"%s\" and \"%s\" appears twice",
                          territory_name(map, a), territory_name(map, b));
                return -1;
            }
        }
        edge[a][b] = 1;
        edge[b][a] = 1;
    }

    head = 0;
    tail = 0;
    queue[tail++] = 0;
    seen[0] = 1;
    while (head < tail) {
        int current = queue[head++];
        for (j = 0; j < map->territoryCount; j++) {
            if (edge[current][j] && !seen[j]) {
                seen[j] = 1;
                queue[tail++] = j;
            }
        }
    }
    for (i = 0; i < map->territoryCount; i++) {
        if (!seen[i]) {
            set_error(err, err_size,
                      "adjacency graph is disconnected: \"%s\" cannot be "
                      "reached from \"%s\"", territory_name(map, i),
                      territory_name(map, 0));
            return -1;
        }
    }

    if (map->rules.minReinforce < 0 ||
        map->rules.territoriesPerArmy < 1 ||
        map->rules.strongholdReinforce < 0 ||
        map->rules.leadersPerPlayer < 1 ||
        map->rules.leaderCost < 0 ||
        map->rules.dieCap < 0 ||
        map->rules.setBonusAAA < 0 || map->rules.setBonusBBB < 0 ||
        map->rules.setBonusCCC < 0 || map->rules.setBonusABC < 0) {
        set_error(err, err_size, "map rules contain an invalid value");
        return -1;
    }
    for (i = 2; i <= MAP_MAX_PLAYERS; i++) {
        if (!map->setup[i].present || map->setup[i].armies < 1) {
            set_error(err, err_size,
                      "map setup for %d players is missing or invalid", i);
            return -1;
        }
    }

    if (map->territoryCardCount > MAP_MAX_CARDS) {
        set_error(err, err_size, "too many territory cards");
        return -1;
    }
    for (i = 0; i < map->territoryCardCount; i++) {
        const struct MapTerritoryCard *card = &map->territoryCards[i];

        if (card->wild) {
            wilds++;
            continue;
        }
        if (card->territory < 0 || card->territory >= map->territoryCount) {
            set_error(err, err_size, "territory card %d is out of range", i);
            return -1;
        }
        if (covered[card->territory]) {
            set_error(err, err_size,
                      "territory \"%s\" has more than one territory card",
                      territory_name(map, card->territory));
            return -1;
        }
        covered[card->territory] = 1;
        if (card->symbol == 'A') {
            symbol_a++;
        } else if (card->symbol == 'B') {
            symbol_b++;
        } else if (card->symbol == 'C') {
            symbol_c++;
        } else {
            set_error(err, err_size,
                      "territory \"%s\" has an invalid card symbol",
                      territory_name(map, card->territory));
            return -1;
        }
    }
    for (i = 0; i < map->territoryCount; i++) {
        if (!covered[i]) {
            set_error(err, err_size,
                      "territory \"%s\" has no territory card",
                      territory_name(map, i));
            return -1;
        }
    }
    if ((symbol_a > symbol_b ? symbol_a - symbol_b : symbol_b - symbol_a) > 1 ||
        (symbol_a > symbol_c ? symbol_a - symbol_c : symbol_c - symbol_a) > 1 ||
        (symbol_b > symbol_c ? symbol_b - symbol_c : symbol_c - symbol_b) > 1) {
        set_error(err, err_size,
                  "territory-card symbols are not balanced (A=%d B=%d C=%d)",
                  symbol_a, symbol_b, symbol_c);
        return -1;
    }
    (void)wilds;

    for (i = 0; i < map->bridgeCount; i++) {
        int a = map->bridges[i].a;
        int b = map->bridges[i].b;
        if (a >= map->territoryCount || b >= map->territoryCount ||
            !map_are_adjacent(map, a, b)) {
            set_error(err, err_size,
                      "bridge %d does not join two adjacent territories", i);
            return -1;
        }
    }
    for (i = 0; i < map->mountainCount; i++) {
        int k;
        if (map->mountains[i].count < 2) {
            set_error(err, err_size,
                      "mountain range \"%s\" needs at least 2 territories",
                      map->mountains[i].name);
            return -1;
        }
        for (k = 0; k < map->mountains[i].count; k++) {
            if (map->mountains[i].territories[k] >= map->territoryCount) {
                set_error(err, err_size,
                          "mountain range \"%s\" lists a missing territory",
                          map->mountains[i].name);
                return -1;
            }
        }
    }

    if (map->adventureCardCount > MAP_MAX_CARDS) {
        set_error(err, err_size, "too many adventure cards");
        return -1;
    }
    for (i = 0; i < map->adventureCardCount; i++) {
        const struct MapAdventureCard *card = &map->adventureCards[i];

        if (card->id[0] == '\0' || card->title[0] == '\0' ||
            card->text[0] == '\0' || card->effect[0] == '\0') {
            set_error(err, err_size, "adventure card %d is missing data", i);
            return -1;
        }
        if (strcmp(card->kind, "mission") != 0 &&
            strcmp(card->kind, "event") != 0 &&
            strcmp(card->kind, "power") != 0) {
            set_error(err, err_size,
                      "adventure card \"%s\" has unknown kind \"%s\"",
                      card->id, card->kind);
            return -1;
        }
        if (!cards_effect_exists(card->effect)) {
            set_error(err, err_size,
                      "adventure card \"%s\" uses unknown effect \"%s\"",
                      card->id, card->effect);
            return -1;
        }
    }
    return 0;
}
