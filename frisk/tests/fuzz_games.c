#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/rules.h"
#include "../engine/serialize.h"

static struct Map fuzz_map;
static struct Map printed_map;       /* PLAN.md 10b: printed LOTR cards */
static const struct Map *active_map = &fuzz_map;
static int printed_mode;

static void build_fuzz_map(void)
{
    static const int adjacency[8][2] = {
        { 1, -1 }, { 0, 2 }, { 1, 3 }, { 2, -1 },
        { 5, -1 }, { 4, 6 }, { 5, 7 }, { 6, -1 }
    };
    static const int adjacency_count[8] = { 1, 2, 2, 1, 1, 2, 2, 1 };
    static const char symbols[8] = { 'A', 'A', 'A', 'B', 'B', 'B', 'C', 'C' };
    int i;

    memset(&fuzz_map, 0, sizeof(fuzz_map));
    strcpy(fuzz_map.name, "Fuzz Map");
    fuzz_map.version = 1;
    strcpy(fuzz_map.image.file, "board.svg");
    fuzz_map.image.width = 100;
    fuzz_map.image.height = 100;
    strcpy(fuzz_map.dir, "maps/fuzz");
    fuzz_map.factionCount = 4;
    for (i = 0; i < 4; i++) {
        snprintf(fuzz_map.factions[i].name,
                 sizeof(fuzz_map.factions[i].name), "Faction%d", i);
        strcpy(fuzz_map.factions[i].color, "#808080");
        strcpy(fuzz_map.factions[i].unit1, "pieces/unit1.svg");
        strcpy(fuzz_map.factions[i].unit3, "pieces/unit3.svg");
        strcpy(fuzz_map.factions[i].unit5, "pieces/unit5.svg");
        strcpy(fuzz_map.factions[i].leader, "pieces/leader.svg");
        fuzz_map.factions[i].id = i;
    }
    fuzz_map.regionCount = 2;
    strcpy(fuzz_map.regions[0].name, "North");
    fuzz_map.regions[0].bonus = 2;
    strcpy(fuzz_map.regions[0].color, "#aa0000");
    fuzz_map.regions[1].id = 1;
    strcpy(fuzz_map.regions[1].name, "South");
    fuzz_map.regions[1].bonus = 3;
    strcpy(fuzz_map.regions[1].color, "#00aa00");
    fuzz_map.territoryCount = 8;
    for (i = 0; i < 8; i++) {
        struct MapTerritory *t = &fuzz_map.territories[i];
        t->id = i;
        snprintf(t->name, sizeof(t->name), "T%d", i);
        t->region = (i < 4) ? 0 : 1;
        strcpy(t->path, "M 0,0 L 1,0 L 1,1 Z");
        t->labelX = 10 + i * 10;
        t->labelY = 10;
        t->stronghold = (i == 1 || i == 5);
        t->site = (i == 2 || i == 6);
        t->adjacentCount = adjacency_count[i];
        t->adjacent[0] = adjacency[i][0];
        if (adjacency[i][1] >= 0) {
            t->adjacent[1] = adjacency[i][1];
        }
    }
    fuzz_map.linkCount = 1;
    fuzz_map.links[0].a = 3;
    fuzz_map.links[0].b = 4;
    strcpy(fuzz_map.links[0].kind, "bridge");
    strcpy(fuzz_map.links[0].path, "M 0,0 L 1,1");
    fuzz_map.territoryCardCount = 10;
    for (i = 0; i < 8; i++) {
        fuzz_map.territoryCards[i].territory = i;
        fuzz_map.territoryCards[i].symbol = symbols[i];
    }
    fuzz_map.territoryCards[8].wild = 1;
    fuzz_map.territoryCards[8].territory = -1;
    fuzz_map.territoryCards[9].wild = 1;
    fuzz_map.territoryCards[9].territory = -1;
    fuzz_map.adventureCardCount = 0;
    {
        static const struct {
            const char *id;
            const char *kind;
            const char *effect;
            const char *args;
        } adv[] = {
            { "m1", "mission", "HOLD_TERRITORIES",
              "{\"territories\":[\"T0\",\"T1\"],\"reward\":2}" },
            { "m2", "mission", "HOLD_REGION",
              "{\"region\":\"North\",\"reward\":3}" },
            { "m3", "mission", "CONQUER_N_THIS_TURN",
              "{\"n\":1,\"reward\":2}" },
            { "m4", "mission", "LEADER_REACH",
              "{\"territory\":\"T2\",\"reward\":2}" },
            { "m5", "mission", "HOLD_STRONGHOLDS",
              "{\"n\":1,\"reward\":2}" },
            { "e1", "event", "REGION_ATTRITION",
              "{\"region\":\"North\",\"loss\":1}" },
            { "e2", "event", "REINFORCE_ALL", "{\"n\":2}" },
            { "e3", "event", "LOSE_LEADER_UNLESS", "{}" },
            { "e4", "event", "SEA_CLOSED", "{\"a\":\"T3\",\"b\":\"T4\"}" },
            { "p1", "power", "ATTACK_BONUS", "{\"n\":1}" },
            { "p2", "power", "DEFENSE_BONUS", "{\"n\":2}" },
            { "p3", "power", "REROLL", "{}" },
            { "p4", "power", "EXTRA_REINFORCE", "{\"n\":3}" },
            { "p5", "power", "FREE_FORTIFY", "{}" },
            { "p6", "power", "RECRUIT_LEADER", "{}" },
            { "p7", "power", "CANCEL_EVENT", "{}" }
        };
        int c;
        for (c = 0; c < (int)(sizeof(adv) / sizeof(adv[0])); c++) {
            struct MapAdventureCard *card =
                &fuzz_map.adventureCards[fuzz_map.adventureCardCount++];
            snprintf(card->id, sizeof(card->id), "%s", adv[c].id);
            snprintf(card->kind, sizeof(card->kind), "%s", adv[c].kind);
            snprintf(card->title, sizeof(card->title), "Card %s", adv[c].id);
            snprintf(card->text, sizeof(card->text), "Card %s", adv[c].id);
            snprintf(card->effect, sizeof(card->effect), "%s", adv[c].effect);
            snprintf(card->args, sizeof(card->args), "%s", adv[c].args);
        }
    }
    for (i = 2; i <= 4; i++) {
        fuzz_map.setup[i].present = 1;
        fuzz_map.setup[i].armies = 20;
        fuzz_map.setup[i].neutral = 0;
    }
    /* Exercise the passive neutral faction in two-player fuzz games. */
    fuzz_map.setup[2].neutral = 1;
    fuzz_map.setup[2].neutralSharePercent = 33;
    fuzz_map.setup[2].neutralArmies = 2;
    fuzz_map.rules.setBonusAAA = 4;
    fuzz_map.rules.setBonusBBB = 6;
    fuzz_map.rules.setBonusCCC = 8;
    fuzz_map.rules.setBonusABC = 10;
    fuzz_map.rules.minReinforce = 3;
    fuzz_map.rules.territoriesPerArmy = 3;
    fuzz_map.rules.strongholdReinforce = 1;
    fuzz_map.rules.leadersPerPlayer = 2;
    fuzz_map.rules.leaderCost = 3;
    fuzz_map.rules.dieCap = 6;
}

/* The same board with sides, ports, a bridge, a mountain range, the printed
 * card effects and the rulebook card flow. */
static void build_printed_map(void)
{
    static const struct {
        const char *id, *kind, *effect, *args;
    } adv[] = {
        { "e1", "event", "FREE_LEADER", "{\"max\":2}" },
        { "e2", "event", "FREE_LEADER", "{\"max\":2}" },
        { "e3", "event", "SIDE_CONTROL",
          "{\"territory\":\"T5\",\"side\":\"good\",\"gain\":10,\"loss\":2}" },
        { "e4", "event", "SEABORNE_TOLL", "{\"loss\":3}" },
        { "e5", "event", "LOCAL_UPRISING", "{\"n\":4}" },
        { "e6", "event", "RAIDERS", "{\"cards\":3}" },
        { "e7", "event", "MUSTERING", "{\"cards\":3,\"n\":3}" },
        { "e8", "event", "WINTER_STORMS", "{\"cards\":3}" },
        { "p1", "power", "AMBUSH", "{\"n\":3}" },
        { "p2", "power", "SEABORNE_BONUS", "{\"n\":1}" },
        { "p3", "power", "LEADER_MOVE", "{}" },
        { "p4", "power", "INVADED_REINFORCE", "{\"n\":4}" },
        { "p5", "power", "GRIMA", "{\"n\":2}" },
        { "p6", "power", "EXTRA_REDEPLOY", "{\"n\":3}" },
        { "p7", "power", "NO_STRONGHOLD", "{}" },
        { "p8", "power", "WITHDRAW", "{}" },
        { "p9", "power", "MOUNTAIN_PASS", "{}" },
        { "pa", "power", "STORM_REPEL", "{}" },
        { "pb", "power", "BRIDGE_BLOCK", "{}" },
        { "m1", "mission", "SITE_MISSION",
          "{\"site\":\"T2\",\"reward\":\"region\",\"region\":\"North\","
          "\"good\":2,\"evil\":6}" },
        { "m2", "mission", "SITE_MISSION",
          "{\"site\":\"T6\",\"reward\":\"port\",\"good\":2,\"evil\":2}" },
        { "m3", "mission", "SITE_MISSION",
          "{\"site\":\"T2\",\"reward\":\"card\",\"good\":0,\"evil\":0}" },
        { "m4", "mission", "SITE_MISSION",
          "{\"site\":\"T6\",\"reward\":\"oathbreakers\",\"good\":6,"
          "\"evil\":6}" },
        { "m5", "mission", "SITE_MISSION",
          "{\"site\":\"T2\",\"reward\":\"any\",\"good\":2,\"evil\":5}" },
        { "m6", "mission", "SITE_MISSION",
          "{\"site\":\"T6\",\"reward\":\"territory\",\"territory\":\"T6\","
          "\"good\":4,\"evil\":2}" },
    };
    int c;

    printed_map = fuzz_map;
    strcpy(printed_map.name, "Printed Fuzz Map");
    for (c = 0; c < 4; c++) {
        printed_map.factions[c].side = c < 2 ? MAP_SIDE_GOOD : MAP_SIDE_EVIL;
    }
    strcpy(printed_map.links[0].kind, "sea");
    printed_map.territories[3].port = 1;
    printed_map.territories[4].port = 1;
    printed_map.bridgeCount = 1;
    printed_map.bridges[0].a = 1;
    printed_map.bridges[0].b = 2;
    printed_map.mountainCount = 1;
    strcpy(printed_map.mountains[0].name, "Peaks");
    printed_map.mountains[0].territories[0] = 0;
    printed_map.mountains[0].territories[1] = 6;
    printed_map.mountains[0].territories[2] = 3;
    printed_map.mountains[0].count = 3;
    printed_map.adventureCardCount = 0;
    for (c = 0; c < (int)(sizeof(adv) / sizeof(adv[0])); c++) {
        struct MapAdventureCard *card =
            &printed_map.adventureCards[printed_map.adventureCardCount++];
        snprintf(card->id, sizeof(card->id), "%s", adv[c].id);
        snprintf(card->kind, sizeof(card->kind), "%s", adv[c].kind);
        snprintf(card->title, sizeof(card->title), "Card %s", adv[c].id);
        snprintf(card->text, sizeof(card->text), "Card %s", adv[c].id);
        snprintf(card->effect, sizeof(card->effect), "%s", adv[c].effect);
        snprintf(card->args, sizeof(card->args), "%s", adv[c].args);
    }
    printed_map.rules.adventureStartHand = 4;
    printed_map.rules.drawOnLeaderConquest = 1;
}

static int is_reactive(const char *effect)
{
    return strcmp(effect, "DEFENSE_BONUS") == 0 ||
           strcmp(effect, "REROLL") == 0 ||
           strcmp(effect, "CANCEL_EVENT") == 0 ||
           strcmp(effect, "INVADED_REINFORCE") == 0 ||
           strcmp(effect, "WITHDRAW") == 0 ||
           strcmp(effect, "STORM_REPEL") == 0;
}

static int start_fuzz_game(struct Game *g, int players, int mode, int batch,
                           uint64_t seed, char *err, size_t err_size)
{
    int i;

    game_init(g, active_map, 1, "fuzz", "p0", seed);
    for (i = 0; i < players; i++) {
        char name[16];
        snprintf(name, sizeof(name), "p%d", i);
        if (game_join(g, name, -1, err, err_size) != 0) {
            return -1;
        }
    }
    if (game_start(g, mode, batch, err, err_size) != 0) {
        return -1;
    }
    {
        struct DiceRng rng;
        int seat;
        dice_seed(&rng, seed ^ UINT64_C(0xa5a5a5a55a5a5a5a));
        if (printed_mode) {
            /* The rulebook deal gave 4 cards; arm some standing orders. */
            for (seat = 0; seat < g->player_count; seat++) {
                int slot;
                for (slot = 0; slot < g->seats[seat].adventure_hand_count;
                     slot++) {
                    const struct MapAdventureCard *mc = &g->map->adventureCards[
                        g->seats[seat].adventure_hand[slot]];
                    if (is_reactive(mc->effect)) {
                        g->seats[seat].adventure_auto[slot] =
                            (unsigned char)dice_range(&rng, 3);
                        g->seats[seat].adventure_auto_n[slot] =
                            (unsigned char)(1 + dice_range(&rng, 3));
                    }
                }
            }
            return 0;
        }
        for (seat = 0; seat < g->player_count; seat++) {
            int n;
            for (n = 0; n < 2 && g->adventure_deck_count > 0; n++) {
                int attempt;
                for (attempt = 0; attempt < 8 &&
                     g->adventure_deck_count > 0; attempt++) {
                    int pos = dice_range(&rng, g->adventure_deck_count);
                    int card = g->adventure_deck[pos];
                    const struct MapAdventureCard *mc =
                        &g->map->adventureCards[card];
                    int slot;
                    if (strcmp(mc->kind, "event") == 0) {
                        continue;
                    }
                    g->adventure_deck[pos] =
                        g->adventure_deck[g->adventure_deck_count - 1];
                    g->adventure_deck_count--;
                    slot = g->seats[seat].adventure_hand_count++;
                    g->seats[seat].adventure_hand[slot] = card;
                    g->seats[seat].adventure_auto[slot] = 0;
                    g->seats[seat].adventure_auto_n[slot] = 0;
                    if (strcmp(mc->effect, "DEFENSE_BONUS") == 0 ||
                        strcmp(mc->effect, "REROLL") == 0 ||
                        strcmp(mc->effect, "CANCEL_EVENT") == 0) {
                        if (dice_range(&rng, 2) == 0) {
                            g->seats[seat].adventure_auto[slot] = 1;
                        }
                    }
                    break;
                }
            }
        }
    }
    return 0;
}

static uint64_t game_hash(const struct Game *g)
{
    const unsigned char *bytes = (const unsigned char *)g;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;

    for (i = 0; i < sizeof(*g); i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int check_invariants(const struct Game *g, char *why, size_t why_size)
{
    int seen[GAME_MAX_CARDS];
    int i, j;
    int total = 0;
    int pending_allowance = 0;
    int required;
    const struct Map *map = g->map;

    if (g->player_count < 2 || g->player_count > GAME_MAX_PLAYERS) {
        snprintf(why, why_size, "bad player count %d", g->player_count);
        return -1;
    }
    if (g->territory_count != map->territoryCount) {
        snprintf(why, why_size, "territory count mismatch");
        return -1;
    }
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < g->territory_count; i++) {
        int owner = g->territories[i].owner;
        if (owner < -1 || owner >= g->player_count) {
            snprintf(why, why_size, "territory %d has bad owner %d", i,
                     owner);
            return -1;
        }
        if (g->territories[i].leader_of < 0 ||
            g->territories[i].leader_of > g->player_count) {
            snprintf(why, why_size, "territory %d has bad leader %d", i,
                     g->territories[i].leader_of);
            return -1;
        }
        if (g->territories[i].leader_of > 0 &&
            owner != g->territories[i].leader_of - 1) {
            snprintf(why, why_size,
                     "territory %d has a leader but a foreign owner", i);
            return -1;
        }
        if (g->territories[i].neutral) {
            if (owner != -1 || g->territories[i].armies < 1) {
                snprintf(why, why_size, "territory %d neutral state bad", i);
                return -1;
            }
        } else if (owner == -1 && g->territories[i].armies != 0) {
            snprintf(why, why_size, "unclaimed territory %d has armies", i);
            return -1;
        } else if (owner >= 0) {
            if (g->territories[i].armies < 1 &&
                !(g->phase == PHASE_CONQUEST_MOVE &&
                  i == g->pending_to) &&
                g->phase != PHASE_FINISHED) {
                snprintf(why, why_size,
                         "territory %d is owned but has %d armies", i,
                         g->territories[i].armies);
                return -1;
            }
        }
        total += g->territories[i].armies;
    }
    if (g->phase >= PHASE_SETUP_PLACE && g->phase != PHASE_FINISHED) {
        for (i = 0; i < g->territory_count; i++) {
            if (g->territories[i].owner == -1 &&
                !g->territories[i].neutral) {
                snprintf(why, why_size, "territory %d is still unclaimed",
                         i);
                return -1;
            }
        }
    }
    if (g->phase == PHASE_CONQUEST_MOVE) {
        pending_allowance = 1;
    }
    required = g->territory_count - pending_allowance;
    if ((g->phase == PHASE_REINFORCE || g->phase == PHASE_ATTACK ||
         g->phase == PHASE_CONQUEST_MOVE || g->phase == PHASE_FORTIFY ||
         g->phase == PHASE_DRAW) && total < required) {
        snprintf(why, why_size, "only %d armies for %d territories", total,
                 required);
        return -1;
    }

    /* Territory cards must be a partition of the deck. */
    memset(seen, 0, sizeof(seen));
    total = 0;
    for (i = 0; i < g->deck_count; i++) {
        int c = g->deck[i];
        if (c < 0 || c >= map->territoryCardCount || seen[c]) {
            snprintf(why, why_size, "bad deck card %d", c);
            return -1;
        }
        seen[c] = 1;
        total++;
    }
    for (i = 0; i < g->discard_count; i++) {
        int c = g->discard[i];
        if (c < 0 || c >= map->territoryCardCount || seen[c]) {
            snprintf(why, why_size, "bad discard card %d", c);
            return -1;
        }
        seen[c] = 1;
        total++;
    }
    for (i = 0; i < g->player_count; i++) {
        for (j = 0; j < g->seats[i].hand_count; j++) {
            int c = g->seats[i].hand[j];
            if (c < 0 || c >= map->territoryCardCount || seen[c]) {
                snprintf(why, why_size, "bad hand card %d", c);
                return -1;
            }
            seen[c] = 1;
            total++;
        }
    }
    for (i = 0; i < g->player_count; i++) {
        int c = g->seats[i].uprising_card;
        if (c < 0) {
            continue;
        }
        if (c >= map->territoryCardCount || seen[c]) {
            snprintf(why, why_size, "bad uprising card %d", c);
            return -1;
        }
        seen[c] = 1;
        total++;
    }
    if (total != map->territoryCardCount) {
        snprintf(why, why_size, "territory cards %d of %d", total,
                 map->territoryCardCount);
        return -1;
    }
    if (g->bonus_pending < 0 || g->seats[0].oathbreakers < 0) {
        snprintf(why, why_size, "negative printed-card state");
        return -1;
    }
    total = g->adventure_deck_count + g->adventure_discard_count;
    for (i = 0; i < g->player_count; i++) {
        total += g->seats[i].adventure_hand_count;
    }
    if (total != map->adventureCardCount) {
        snprintf(why, why_size, "adventure cards %d of %d", total,
                 map->adventureCardCount);
        return -1;
    }

    if (g->status == GAME_RUNNING) {
        if (g->phase < PHASE_SETUP_CLAIM || g->phase > PHASE_FORTIFY) {
            snprintf(why, why_size, "running game in phase %d", g->phase);
            return -1;
        }
        if (g->current_player < 0 ||
            g->current_player >= g->player_count ||
            !g->seats[g->current_player].alive) {
            snprintf(why, why_size, "current player is not alive");
            return -1;
        }
    }
    if (g->phase == PHASE_CONQUEST_MOVE) {
        if (g->pending_from < 0 || g->pending_to < 0 ||
            g->pending_min < 1 || g->pending_min > g->pending_max ||
            g->pending_to >= g->territory_count ||
            g->territories[g->pending_to].owner != g->current_player ||
            g->territories[g->pending_to].armies < 0) {
            int to_index = (g->pending_to >= 0 &&
                            g->pending_to < g->territory_count)
                               ? g->pending_to : 0;
            snprintf(why, why_size,
                     "bad pending conquest: from=%d to=%d min=%d max=%d "
                     "owner=%d armies=%d current=%d", g->pending_from,
                     g->pending_to, g->pending_min, g->pending_max,
                     g->territories[to_index].owner,
                     g->territories[to_index].armies,
                     g->current_player);
            return -1;
        }
    } else if (g->phase != PHASE_FINISHED && g->pending_from != -1) {
        snprintf(why, why_size, "stale pending conquest");
        return -1;
    }
    if (g->must_trade) {
        int a, b, c;
        if (g->current_player < 0 ||
            g->seats[g->current_player].hand_count < 5 ||
            !game_find_card_set(g, g->current_player, &a, &b, &c)) {
            snprintf(why, why_size, "must_trade without a valid set");
            return -1;
        }
    }
    for (i = 0; i < g->player_count; i++) {
        int leaders = 0;
        for (j = 0; j < g->territory_count; j++) {
            if (g->territories[j].leader_of == i + 1) {
                leaders++;
            }
        }
        if (leaders != g->seats[i].leaders_in_play) {
            snprintf(why, why_size,
                     "seat %d leaders_in_play=%d but %d on the board", i,
                     g->seats[i].leaders_in_play, leaders);
            return -1;
        }
    }
    if (g->status == GAME_FINISHED) {
        if (g->phase != PHASE_FINISHED) {
            snprintf(why, why_size, "finished status in phase %d", g->phase);
            return -1;
        }
        if (g->winner < -1 || g->winner >= g->player_count) {
            snprintf(why, why_size, "bad winner %d", g->winner);
            return -1;
        }
    }
    return 0;
}

static int pick_index(struct DiceRng *rng, int count)
{
    if (count <= 0) {
        return -1;
    }
    return dice_range(rng, count);
}

static char serialize_first[2u * 1024u * 1024u];
static char serialize_second[2u * 1024u * 1024u];
static struct Game serialize_loaded;
static int serialization_checks;

static int serialization_roundtrip(const struct Game *g, char *why,
                                   size_t why_size)
{
    char error[256];

    if (game_to_json(g, serialize_first, sizeof(serialize_first)) != 0) {
        snprintf(why, why_size, "game_to_json failed");
        return -1;
    }
    memset(&serialize_loaded, 0, sizeof(serialize_loaded));
    error[0] = '\0';
    if (game_from_json(serialize_first, &serialize_loaded, g->map, error,
                       sizeof(error)) != 0) {
        snprintf(why, why_size, "game_from_json failed: %.160s", error);
        return -1;
    }
    if (game_to_json(&serialize_loaded, serialize_second,
                     sizeof(serialize_second)) != 0) {
        snprintf(why, why_size, "second game_to_json failed");
        return -1;
    }
    if (strcmp(serialize_first, serialize_second) != 0) {
        size_t i = 0;
        while (serialize_first[i] != '\0' &&
               serialize_first[i] == serialize_second[i]) {
            i++;
        }
        snprintf(why, why_size,
                 "save/load/save differs at offset %zu: \"%.20s\" vs \"%.20s\"",
                 i, serialize_first + i, serialize_second + i);
        return -1;
    }
    return 0;
}

static void choose_place(struct Game *g, struct DiceRng *rng,
                         struct LegalActions *legal, struct Action *action)
{
    int candidates[GAME_MAX_TERRITORIES];
    int count = 0;
    int i;

    for (i = 0; i < g->territory_count; i++) {
        if (legal->placeable[i]) {
            candidates[count++] = i;
        }
    }
    action->type = ACT_PLACE;
    action->territory = candidates[pick_index(rng, count)];
    if (g->phase == PHASE_SETUP_PLACE) {
        action->count = legal->setup_batch_remaining;
    } else {
        action->count = legal->reinforcements_pending;
    }
}

static int pick_playable_card(struct Game *g, struct DiceRng *rng,
                              const struct LegalActions *legal,
                              struct Action *action)
{
    int slots[GAME_ADVENTURE_HAND_MAX];
    int count = 0;
    int i;
    int slot;

    for (i = 0; i < g->seats[g->current_player].adventure_hand_count; i++) {
        if (legal->playable_adventure[i]) {
            slots[count++] = i;
        }
    }
    if (count == 0) {
        return 0;
    }
    slot = slots[pick_index(rng, count)];
    action->type = ACT_PLAY_CARD;
    action->card_a = slot;
    {
        const char *effect = g->map->adventureCards[
            g->seats[g->current_player].adventure_hand[slot]].effect;
        int me = g->current_player;
        int from[GAME_MAX_TERRITORIES * GAME_MAX_TERRITORIES];
        int to[GAME_MAX_TERRITORIES * GAME_MAX_TERRITORIES];
        int n = 0, a, b;
        if (strcmp(effect, "AMBUSH") == 0 || strcmp(effect, "GRIMA") == 0 ||
            strcmp(effect, "NO_STRONGHOLD") == 0 ||
            strcmp(effect, "SEABORNE_BONUS") == 0) {
            for (a = 0; a < g->territory_count; a++) {
                for (b = 0; b < g->territory_count; b++) {
                    int land = 0, k;
                    if (!legal->attack[a][b]) {
                        continue;
                    }
                    for (k = 0; k < g->map->territories[a].adjacentCount; k++) {
                        land |= g->map->territories[a].adjacent[k] == b;
                    }
                    if ((strcmp(effect, "GRIMA") == 0 &&
                         g->territories[b].armies < 2) ||
                        (strcmp(effect, "NO_STRONGHOLD") == 0 &&
                         !g->map->territories[b].stronghold) ||
                        (strcmp(effect, "SEABORNE_BONUS") == 0 &&
                         (land || !map_has_link(g->map, a, b)))) {
                        continue;
                    }
                    from[n] = a;
                    to[n++] = b;
                }
            }
        } else if (strcmp(effect, "MOUNTAIN_PASS") == 0) {
            for (a = 0; a < g->territory_count; a++) {
                for (b = 0; b < g->territory_count; b++) {
                    if (g->territories[a].owner == me &&
                        g->territories[a].armies >= 2 &&
                        g->territories[b].owner != me &&
                        g->territories[b].armies >= 1 &&
                        !g->frozen_by[a] && !g->frozen_by[b] &&
                        map_same_range(g->map, a, b)) {
                        from[n] = a;
                        to[n++] = b;
                    }
                }
            }
        } else if (strcmp(effect, "LEADER_MOVE") == 0) {
            for (a = 0; a < g->territory_count; a++) {
                for (b = 0; b < g->territory_count; b++) {
                    if (a != b && g->territories[a].leader_of == me + 1 &&
                        g->territories[b].owner == me &&
                        g->territories[b].leader_of == 0) {
                        from[n] = a;
                        to[n++] = b;
                    }
                }
            }
        } else if (strcmp(effect, "BRIDGE_BLOCK") == 0) {
            action->bridge = dice_range(rng, g->map->bridgeCount);
            return 1;
        } else {
            n = -1;
        }
        if (n == 0) {
            return 0;
        }
        if (n > 0) {
            int pick = pick_index(rng, n);
            action->from = from[pick];
            action->to = to[pick];
            return 1;
        }
    }
    if (strcmp(g->map->adventureCards[
                   g->seats[g->current_player].adventure_hand[slot]].effect,
               "RECRUIT_LEADER") == 0) {
        int spots[GAME_MAX_TERRITORIES];
        int spot_count = 0;
        for (i = 0; i < g->territory_count; i++) {
            if (legal->recruit_placeable[i]) {
                spots[spot_count++] = i;
            }
        }
        if (spot_count == 0) {
            return 0;
        }
        action->territory = spots[pick_index(rng, spot_count)];
    }
    return 1;
}

static int choose_legal_action(struct Game *g, struct DiceRng *rng,
                               struct Action *action)
{
    struct LegalActions legal;
    int i, j;

    memset(action, 0, sizeof(*action));
    if (g->status != GAME_RUNNING) {
        return 0;
    }
    if (g->must_discard_adventure &&
        g->seats[g->current_player].adventure_hand_count > 0) {
        action->type = ACT_DISCARD_ADVENTURE;
        action->card_a = dice_range(rng,
            g->seats[g->current_player].adventure_hand_count);
        return 1;
    }
    if (g->must_trade) {
        if (!game_find_card_set(g, g->current_player, &action->card_a,
                                &action->card_b, &action->card_c)) {
            return 0;
        }
        action->type = ACT_TRADE_CARDS;
        return 1;
    }
    game_legal_actions(g, &legal);
    if (legal.free_leader_pending || legal.bonus_pending > 0) {
        int spots[GAME_MAX_TERRITORIES];
        int spot_count = 0;
        for (i = 0; i < g->territory_count; i++) {
            if (legal.free_leader_pending ? legal.leader_placeable[i]
                                          : legal.bonus_placeable[i]) {
                spots[spot_count++] = i;
            }
        }
        if (spot_count == 0) {
            return 0;
        }
        action->territory = spots[pick_index(rng, spot_count)];
        if (legal.free_leader_pending) {
            action->type = ACT_PLACE_LEADER;
        } else {
            action->type = ACT_PLACE;
            action->count = (g->bonus_area == BONUS_ANY ||
                             g->bonus_area == BONUS_PORT)
                ? g->bonus_pending : 1 + dice_range(rng, g->bonus_pending);
        }
        return 1;
    }
    for (i = 0; i < g->seats[g->current_player].adventure_hand_count; i++) {
        if (legal.claimable_mission[i] && dice_range(rng, 2) == 0) {
            action->type = ACT_CLAIM_MISSION;
            action->card_a = i;
            return 1;
        }
    }
    if (legal.can_oathbreakers && dice_range(rng, 3) == 0) {
        for (i = 0; i < g->territory_count; i++) {
            for (j = 0; j < g->territory_count; j++) {
                if (legal.attack[i][j]) {
                    action->type = ACT_OATHBREAKERS;
                    action->territory = i;
                    return 1;
                }
            }
        }
    }
    switch (g->phase) {
    case PHASE_SETUP_CLAIM: {
        int candidates[GAME_MAX_TERRITORIES];
        int count = 0;
        for (i = 0; i < g->territory_count; i++) {
            if (legal.claimable[i]) {
                candidates[count++] = i;
            }
        }
        if (count == 0) {
            return 0;
        }
        action->type = ACT_CLAIM;
        action->territory = candidates[pick_index(rng, count)];
        return 1;
    }
    case PHASE_SETUP_PLACE:
        choose_place(g, rng, &legal, action);
        return 1;
    case PHASE_SETUP_LEADERS: {
        int candidates[GAME_MAX_TERRITORIES];
        int count = 0;
        for (i = 0; i < g->territory_count; i++) {
            if (legal.leader_placeable[i]) {
                candidates[count++] = i;
            }
        }
        if (count == 0) {
            return 0;
        }
        action->type = ACT_PLACE_LEADER;
        action->territory = candidates[pick_index(rng, count)];
        return 1;
    }
    case PHASE_REINFORCE:
        if (dice_range(rng, 3) == 0 &&
            pick_playable_card(g, rng, &legal, action)) {
            return 1;
        }
        if (legal.can_recruit_leader && dice_range(rng, 8) == 0) {
            int spots[GAME_MAX_TERRITORIES];
            int spot_count = 0;
            for (i = 0; i < g->territory_count; i++) {
                if (legal.recruit_placeable[i]) {
                    spots[spot_count++] = i;
                }
            }
            if (spot_count > 0) {
                action->type = ACT_RECRUIT_LEADER;
                action->territory = spots[pick_index(rng, spot_count)];
                return 1;
            }
        }
        if (legal.can_trade && dice_range(rng, 2) == 0) {
            if (game_find_card_set(g, g->current_player, &action->card_a,
                                   &action->card_b, &action->card_c)) {
                action->type = ACT_TRADE_CARDS;
                return 1;
            }
        }
        if (legal.reinforcements_pending > 0) {
            choose_place(g, rng, &legal, action);
            return 1;
        }
        if (legal.can_done_reinforce) {
            action->type = ACT_DONE_REINFORCE;
            return 1;
        }
        return 0;
    case PHASE_ATTACK: {
        int pairs_from[GAME_MAX_TERRITORIES * GAME_MAX_TERRITORIES];
        if (dice_range(rng, 6) == 0 &&
            pick_playable_card(g, rng, &legal, action)) {
            return 1;
        }
        int pairs_to[GAME_MAX_TERRITORIES * GAME_MAX_TERRITORIES];
        int count = 0;
        for (i = 0; i < g->territory_count; i++) {
            for (j = 0; j < g->territory_count; j++) {
                if (legal.attack[i][j]) {
                    pairs_from[count] = i;
                    pairs_to[count] = j;
                    count++;
                }
            }
        }
        if (count > 0 && dice_range(rng, 10) != 0) {
            int pick = pick_index(rng, count);
            action->type = ACT_ATTACK;
            action->from = pairs_from[pick];
            action->to = pairs_to[pick];
            action->dice = 1 + dice_range(rng,
                legal.max_attack_dice[action->from]);
            if (dice_range(rng, 3) == 0) {
                action->repeat = 1;
                action->stop_at =
                    1 + dice_range(rng,
                                   g->territories[action->from].armies - 1);
            }
            return 1;
        }
        action->type = ACT_DONE_ATTACK;
        return 1;
    }
    case PHASE_CONQUEST_MOVE:
        action->type = ACT_MOVE_IN;
        action->from = g->pending_from;
        action->to = g->pending_to;
        action->count = g->pending_min +
            dice_range(rng, g->pending_max - g->pending_min + 1);
        if (g->territories[action->from].leader_of ==
                g->current_player + 1 &&
            g->territories[action->to].leader_of == 0 &&
            dice_range(rng, 2) == 1) {
            action->leader = 1;
        }
        return 1;
    case PHASE_FORTIFY: {
        int froms[GAME_MAX_TERRITORIES * GAME_MAX_TERRITORIES];
        if (dice_range(rng, 8) == 0 &&
            pick_playable_card(g, rng, &legal, action)) {
            return 1;
        }
        int tos[GAME_MAX_TERRITORIES * GAME_MAX_TERRITORIES];
        int count = 0;
        for (i = 0; i < g->territory_count; i++) {
            for (j = 0; j < g->territory_count; j++) {
                if (legal.fortify[i][j]) {
                    froms[count] = i;
                    tos[count] = j;
                    count++;
                }
            }
        }
        if (count > 0 && dice_range(rng, 2) == 0) {
            int pick = pick_index(rng, count);
            action->type = ACT_FORTIFY;
            action->from = froms[pick];
            action->to = tos[pick];
            action->count =
                1 + dice_range(rng,
                               g->territories[action->from].armies - 1);
            if (g->territories[action->from].leader_of ==
                    g->current_player + 1 &&
                g->territories[action->to].leader_of == 0 &&
                (dice_range(rng, 2) == 1 ||
                 legal.fortify[action->from][action->to] == 2)) {
                action->leader = 1;
            }
            return 1;
        }
        action->type = ACT_END_TURN;
        return 1;
    }
    default:
        return 0;
    }
}

static int run_legal_game(uint64_t seed, int player_count, int mode,
                          int batch, int max_actions, int *action_count,
                          char *why, size_t why_size)
{
    struct Game g;
    struct DiceRng chooser;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int actions = 0;

    if (start_fuzz_game(&g, player_count, mode, batch, seed, error,
                        sizeof(error)) != 0) {
        snprintf(why, why_size, "start failed: %.160s", error);
        return -1;
    }
    if (check_invariants(&g, why, why_size) != 0) {
        return -1;
    }
    dice_seed(&chooser, seed ^ UINT64_C(0x9e3779b97f4a7c15));
    while (g.status == GAME_RUNNING) {
        struct Action action;
        if (actions >= max_actions) {
            snprintf(why, why_size, "game did not terminate");
            return -2;
        }
        if (!choose_legal_action(&g, &chooser, &action)) {
            snprintf(why, why_size, "no legal action in phase %d",
                     g.phase);
            return -1;
        }
        if (getenv("FUZZ_TRACE") != NULL && (actions >= max_actions - 40 || getenv("FUZZ_TRACE_ALL") != NULL)) {
            int t, own[4] = {0, 0, 0, 0}, arm[4] = {0, 0, 0, 0};
            for (t = 0; t < g.territory_count; t++) {
                if (g.territories[t].owner >= 0) {
                    own[g.territories[t].owner]++;
                    arm[g.territories[t].owner] += g.territories[t].armies;
                }
            }
            fprintf(stderr, "turn %d p%d phase %d act %d from %d to %d t %d n %d | "
                    "lands %d/%d/%d/%d armies %d/%d/%d/%d pend %d bonus %d\n",
                    g.turn, g.current_player, g.phase, action.type,
                    action.from, action.to, action.territory, action.count,
                    own[0], own[1], own[2], own[3], arm[0], arm[1], arm[2],
                    arm[3], g.reinforcements_pending, g.bonus_pending);
        }
        if (game_apply(&g, g.current_player, &action, events, &count,
                       error, sizeof(error)) != 0) {
            snprintf(why, why_size, "action %d rejected in phase %d: %.160s",
                     action.type, g.phase, error);
            return -1;
        }
        if (check_invariants(&g, why, why_size) != 0) {
            return -1;
        }
        if (serialization_checks < 1000) {
            if (serialization_roundtrip(&g, why, why_size) != 0) {
                return -1;
            }
            serialization_checks++;
        }
        actions++;
    }
    if (g.phase != PHASE_FINISHED) {
        snprintf(why, why_size, "game ended in phase %d", g.phase);
        return -1;
    }
    *action_count = actions;
    return 0;
}

static void random_action(struct DiceRng *rng, int players,
                          int card_count, struct Action *action, int *player)
{
    memset(action, 0, sizeof(*action));
    action->type = dice_range(rng, ACT_DISCARD_ADVENTURE + 1);
    action->territory = dice_range(rng, GAME_MAX_TERRITORIES);
    action->from = dice_range(rng, GAME_MAX_TERRITORIES);
    action->to = dice_range(rng, GAME_MAX_TERRITORIES);
    action->count = dice_range(rng, 40);
    action->dice = dice_range(rng, 8);
    action->repeat = dice_range(rng, 2);
    action->stop_at = dice_range(rng, 40);
    action->leader = dice_range(rng, 2);
    action->card_a = dice_range(rng, card_count + 4) - 1;
    action->card_b = dice_range(rng, card_count + 4) - 1;
    action->card_c = dice_range(rng, card_count + 4) - 1;
    *player = dice_range(rng, players);
}

static int run_arbitrary(int iterations, uint64_t seed,
                         unsigned long long *accepted,
                         char *why, size_t why_size)
{
    struct Game g;
    struct DiceRng rng;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int i;
    uint64_t game_seed = seed;

    if (start_fuzz_game(&g, 2, GAME_START_DEAL, 5, game_seed, error,
                        sizeof(error)) != 0) {
        snprintf(why, why_size, "arbitrary setup failed: %.160s", error);
        return -1;
    }
    dice_seed(&rng, seed ^ UINT64_C(0xd1b54a32d192ed03));
    for (i = 0; i < iterations; i++) {
        struct Action action;
        int player;
        int rc;
        uint64_t before;

        if (g.status != GAME_RUNNING) {
            game_seed += UINT64_C(0x9e3779b97f4a7c15);
            if (start_fuzz_game(&g, 2, GAME_START_DEAL, 5, game_seed,
                                error, sizeof(error)) != 0) {
                snprintf(why, why_size, "arbitrary restart failed: %.160s",
                         error);
                return -1;
            }
        }
        random_action(&rng, g.player_count, g.map->territoryCardCount,
                      &action, &player);
        before = game_hash(&g);
        rc = game_apply(&g, player, &action, events, &count, error,
                        sizeof(error));
        if (rc != 0) {
            if (game_hash(&g) != before) {
                snprintf(why, why_size,
                         "rejected arbitrary action mutated the game");
                return -1;
            }
        } else {
            (*accepted)++;
            if (check_invariants(&g, why, why_size) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int games = (argc > 1) ? atoi(argv[1]) : 1000;
    int first = (argc > 3) ? atoi(argv[3]) : 0;
    int arbitrary = (argc > 2) ? atoi(argv[2]) : 10000;
    int failures = 0;
    int total_actions = 0;
    unsigned long long accepted = 0;
    int i;
    char why[256];

    build_fuzz_map();
    build_printed_map();
    if (games < 1) {
        games = 1;
    }
    if (arbitrary < 0) {
        arbitrary = 0;
    }
    for (i = first; i < first + games; i++) {
        uint64_t seed = UINT64_C(0x123456789abcdef) +
                        (uint64_t)i * UINT64_C(0x100000001b3);
        int players = 2 + (int)(seed % 3);
        int mode = (seed & 1) ? GAME_START_DRAFT : GAME_START_DEAL;
        int batch = 1 + (int)((seed >> 8) % 8);
        int actions = 0;
        int rc;
        printed_mode = i % 2;
        active_map = printed_mode ? &printed_map : &fuzz_map;
        /* Printed cards add many battalions, so random games run longer. */
        rc = run_legal_game(seed, players, mode, batch,
                            printed_mode ? 100000 : 20000,
                            &actions, why, sizeof(why));
        if (rc != 0) {
            fprintf(stderr, "FAIL game %d (seed %llu, %d players, mode %d%s): "
                    "%s\n", i, (unsigned long long)seed, players, mode,
                    printed_mode ? ", printed cards" : "", why);
            failures++;
            if (failures >= 5) {
                break;
            }
        } else {
            total_actions += actions;
        }
    }
    if (arbitrary > 0) {
        if (run_arbitrary(arbitrary, UINT64_C(0xabcdef0123456789),
                          &accepted, why, sizeof(why)) != 0) {
            fprintf(stderr, "FAIL arbitrary fuzz: %s\n", why);
            failures++;
        }
    }
    printf("%s: %d/%d games, %d actions, %d serialize round-trips, "
           "%d arbitrary actions (%llu accepted), %d failures\n",
           failures == 0 ? "PASS" : "FAIL", games - failures, games,
           total_actions, serialization_checks, arbitrary, accepted,
           failures);
    return failures == 0 ? 0 : 1;
}
