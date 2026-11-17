#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/map.h"
#include "../engine/rules.h"

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

static struct Map test_map;
static int test_map_ready;

static void add_adventure(struct Map *map, const char *id, const char *kind,
                          const char *title, const char *effect,
                          const char *args)
{
    struct MapAdventureCard *card = &map->adventureCards[map->adventureCardCount++];
    snprintf(card->id, sizeof(card->id), "%s", id);
    snprintf(card->kind, sizeof(card->kind), "%s", kind);
    snprintf(card->title, sizeof(card->title), "%s", title);
    snprintf(card->text, sizeof(card->text), "%s", title);
    snprintf(card->effect, sizeof(card->effect), "%s", effect);
    snprintf(card->args, sizeof(card->args), "%s", args);
}

static void build_card_map(void)
{
    static const int adjacency[8][2] = {
        { 1, -1 }, { 0, 2 }, { 1, 3 }, { 2, -1 },
        { 5, -1 }, { 4, 6 }, { 5, 7 }, { 6, -1 }
    };
    static const int adjacency_count[8] = { 1, 2, 2, 1, 1, 2, 2, 1 };
    static const char symbols[8] = { 'A', 'A', 'A', 'B', 'B', 'B', 'C', 'C' };
    int i;

    if (test_map_ready) {
        return;
    }
    memset(&test_map, 0, sizeof(test_map));
    snprintf(test_map.name, sizeof(test_map.name), "Card Test Map");
    test_map.version = 1;
    snprintf(test_map.image.file, sizeof(test_map.image.file), "board.svg");
    test_map.image.width = 100;
    test_map.image.height = 100;
    snprintf(test_map.dir, sizeof(test_map.dir), "maps/cards");
    test_map.factionCount = 4;
    for (i = 0; i < 4; i++) {
        snprintf(test_map.factions[i].name, sizeof(test_map.factions[i].name),
                 "Faction%d", i);
        snprintf(test_map.factions[i].color,
                 sizeof(test_map.factions[i].color), "#808080");
        snprintf(test_map.factions[i].unit1,
                 sizeof(test_map.factions[i].unit1), "pieces/unit1.svg");
        snprintf(test_map.factions[i].unit3,
                 sizeof(test_map.factions[i].unit3), "pieces/unit3.svg");
        snprintf(test_map.factions[i].unit5,
                 sizeof(test_map.factions[i].unit5), "pieces/unit5.svg");
        snprintf(test_map.factions[i].leader,
                 sizeof(test_map.factions[i].leader), "pieces/leader.svg");
        test_map.factions[i].id = i;
    }
    test_map.regionCount = 2;
    snprintf(test_map.regions[0].name, sizeof(test_map.regions[0].name),
             "North");
    test_map.regions[0].bonus = 2;
    snprintf(test_map.regions[0].color, sizeof(test_map.regions[0].color),
             "#aa0000");
    test_map.regions[1].id = 1;
    snprintf(test_map.regions[1].name, sizeof(test_map.regions[1].name),
             "South");
    test_map.regions[1].bonus = 3;
    snprintf(test_map.regions[1].color, sizeof(test_map.regions[1].color),
             "#00aa00");
    test_map.territoryCount = 8;
    for (i = 0; i < 8; i++) {
        struct MapTerritory *t = &test_map.territories[i];
        t->id = i;
        snprintf(t->name, sizeof(t->name), "T%d", i);
        t->region = (i < 4) ? 0 : 1;
        snprintf(t->path, sizeof(t->path), "M 0,0 L 1,0 L 1,1 Z");
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
    test_map.linkCount = 1;
    test_map.links[0].a = 3;
    test_map.links[0].b = 4;
    snprintf(test_map.links[0].kind, sizeof(test_map.links[0].kind), "sea");
    snprintf(test_map.links[0].path, sizeof(test_map.links[0].path),
             "M 0,0 L 1,1");
    test_map.territoryCardCount = 10;
    for (i = 0; i < 8; i++) {
        test_map.territoryCards[i].territory = i;
        test_map.territoryCards[i].symbol = symbols[i];
    }
    test_map.territoryCards[8].wild = 1;
    test_map.territoryCards[8].territory = -1;
    test_map.territoryCards[9].wild = 1;
    test_map.territoryCards[9].territory = -1;
    add_adventure(&test_map, "m1", "mission", "Hold North T0/T1",
                  "HOLD_TERRITORIES",
                  "{\"territories\":[\"T0\",\"T1\"],\"reward\":2}");
    add_adventure(&test_map, "m2", "mission", "Hold North",
                  "HOLD_REGION", "{\"region\":\"North\",\"reward\":3}");
    add_adventure(&test_map, "m3", "mission", "Conquer One",
                  "CONQUER_N_THIS_TURN", "{\"n\":1,\"reward\":2}");
    add_adventure(&test_map, "m4", "mission", "Reach T2",
                  "LEADER_REACH", "{\"territory\":\"T2\",\"reward\":2}");
    add_adventure(&test_map, "m5", "mission", "Hold a Stronghold",
                  "HOLD_STRONGHOLDS", "{\"n\":1,\"reward\":2}");
    add_adventure(&test_map, "e1", "event", "North Attrition",
                  "REGION_ATTRITION", "{\"region\":\"North\",\"loss\":1}");
    add_adventure(&test_map, "e2", "event", "Mustering",
                  "REINFORCE_ALL", "{\"n\":2}");
    add_adventure(&test_map, "e3", "event", "Black Breath",
                  "LOSE_LEADER_UNLESS", "{}");
    add_adventure(&test_map, "e4", "event", "Close the Sea",
                  "SEA_CLOSED", "{\"a\":\"T3\",\"b\":\"T4\"}");
    add_adventure(&test_map, "p1", "power", "Attack Bonus",
                  "ATTACK_BONUS", "{\"n\":1}");
    add_adventure(&test_map, "p2", "power", "Defense Bonus",
                  "DEFENSE_BONUS", "{\"n\":2}");
    add_adventure(&test_map, "p3", "power", "Reroll",
                  "REROLL", "{}");
    add_adventure(&test_map, "p4", "power", "Extra Reinforce",
                  "EXTRA_REINFORCE", "{\"n\":3}");
    add_adventure(&test_map, "p5", "power", "Free Fortify",
                  "FREE_FORTIFY", "{}");
    add_adventure(&test_map, "p6", "power", "Free Leader",
                  "RECRUIT_LEADER", "{}");
    add_adventure(&test_map, "p7", "power", "Cancel Event",
                  "CANCEL_EVENT", "{}");
    for (i = 2; i <= 4; i++) {
        test_map.setup[i].present = 1;
        test_map.setup[i].armies = 20;
        test_map.setup[i].neutral = 0;
    }
    test_map.rules.setBonusAAA = 4;
    test_map.rules.setBonusBBB = 6;
    test_map.rules.setBonusCCC = 8;
    test_map.rules.setBonusABC = 10;
    test_map.rules.minReinforce = 3;
    test_map.rules.territoriesPerArmy = 3;
    test_map.rules.strongholdReinforce = 1;
    test_map.rules.leadersPerPlayer = 2;
    test_map.rules.leaderCost = 3;
    test_map.rules.dieCap = 6;
    test_map_ready = 1;
}

static void add_players(struct Game *g, int players)
{
    int i;
    for (i = 0; i < players; i++) {
        char name[16];
        char error[128];
        snprintf(name, sizeof(name), "p%d", i);
        if (game_join(g, name, -1, error, sizeof(error)) != 0) {
            fprintf(stderr, "card join failed: %s\n", error);
        }
    }
}

static void init_card_game(struct Game *g, int players, unsigned long long seed)
{
    int i;

    build_card_map();
    game_init(g, &test_map, 1, "card game", "p0", seed);
    add_players(g, players);
    g->status = GAME_RUNNING;
    g->phase = PHASE_REINFORCE;
    g->current_player = 0;
    g->turn = 1;
    g->winner = -1;
    g->territory_count = test_map.territoryCount;
    for (i = 0; i < g->territory_count; i++) {
        g->territories[i].owner = i % players;
        g->territories[i].armies = 3;
        g->territories[i].leader_of = 0;
        g->territories[i].neutral = 0;
    }
    for (i = 0; i < players; i++) {
        memset(&g->seats[i].stats, 0, sizeof(g->seats[i].stats));
        g->seats[i].alive = 1;
        g->seats[i].hand_count = 0;
        g->seats[i].adventure_hand_count = 0;
        memset(g->seats[i].adventure_auto, 0,
               sizeof(g->seats[i].adventure_auto));
        memset(g->seats[i].adventure_auto_n, 0,
               sizeof(g->seats[i].adventure_auto_n));
        memset(g->seats[i].site_claimed, 0,
               sizeof(g->seats[i].site_claimed));
        g->seats[i].leaders_in_play = 0;
        g->seats[i].missions_done = 0;
        g->seats[i].attack_bonus = 0;
        g->seats[i].last_seen_version = g->version;
    }
    g->deck_count = test_map.territoryCardCount;
    for (i = 0; i < g->deck_count; i++) {
        g->deck[i] = i;
    }
    g->discard_count = 0;
    g->adventure_deck_count = 0;
    g->adventure_discard_count = 0;
    memset(g->deferred_card_bonus, 0, sizeof(g->deferred_card_bonus));
    g->conquered_this_turn = 0;
    g->territories_conquered_this_turn = 0;
    g->cards_traded_this_turn = 0;
    g->fortified_this_turn = 0;
    g->extra_fortifies = 0;
    g->must_trade = 0;
    g->must_discard_adventure = 0;
    g->pending_from = -1;
    g->pending_to = -1;
    g->pending_min = 0;
    g->pending_max = 0;
    g->reinforcements_pending = 0;
    memset(g->link_closed_by, 0, sizeof(g->link_closed_by));
    memset(&g->log, 0, sizeof(g->log));
    dice_seed(&g->rng, seed);
    g->version++;
}

static int find_card(const struct Game *g, const char *effect)
{
    int i;
    for (i = 0; i < g->map->adventureCardCount; i++) {
        if (strcmp(g->map->adventureCards[i].effect, effect) == 0) {
            return i;
        }
    }
    return -1;
}

static int give_card(struct Game *g, int seat, const char *effect)
{
    int card = find_card(g, effect);
    int slot = g->seats[seat].adventure_hand_count++;
    g->seats[seat].adventure_hand[slot] = card;
    g->seats[seat].adventure_auto[slot] = 0;
    g->seats[seat].adventure_auto_n[slot] = 0;
    return slot;
}

static void put_deck_event(struct Game *g, const char *effect)
{
    g->adventure_deck_count = 1;
    g->adventure_deck[0] = find_card(g, effect);
    g->adventure_discard_count = 0;
}

static int do_action(struct Game *g, int player, struct Action *action,
                     struct Event *events, int *count,
                     char *error, size_t error_size)
{
    return game_apply(g, player, action, events, count, error, error_size);
}

static void prepare_site_draw(struct Game *g)
{
    g->phase = PHASE_FORTIFY;
    g->current_player = 0;
    g->fortified_this_turn = 1;
    g->extra_fortifies = 0;
    g->reinforcements_pending = 0;
    g->conquered_this_turn = 0;
    g->must_trade = 0;
    g->territories[2].owner = 0;
    g->territories[2].armies = 3;
    g->territories[2].leader_of = 1;
    g->seats[0].leaders_in_play = 1;
}

static void end_turn_player1(struct Game *g)
{
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;

    g->phase = PHASE_FORTIFY;
    g->current_player = 1;
    g->fortified_this_turn = 1;
    g->reinforcements_pending = 0;
    g->conquered_this_turn = 0;
    memset(&action, 0, sizeof(action));
    action.type = ACT_END_TURN;
    CHECK(do_action(g, 1, &action, events, &count, error,
                    sizeof(error)) == 0);
}

static void test_leader_stronghold_combat(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    struct DiceRng predicted;
    int attacker[3], defender[2];
    int i, j, count;
    int attacker_losses = 0, defender_losses = 0;
    char error[256];

    init_card_game(&g, 2, 9182);
    g.phase = PHASE_ATTACK;
    g.current_player = 0;
    g.territories[0].owner = 0;
    g.territories[0].armies = 10;
    g.territories[0].leader_of = 1;
    g.territories[1].owner = 1;
    g.territories[1].armies = 5;
    g.territories[1].leader_of = 0;
    predicted = g.rng;
    for (i = 0; i < 3; i++) {
        attacker[i] = dice_roll(&predicted);
    }
    for (i = 0; i < 2; i++) {
        defender[i] = dice_roll(&predicted);
    }
    for (i = 0; i < 3; i++) {
        for (j = i + 1; j < 3; j++) {
            if (attacker[j] > attacker[i]) {
                int t = attacker[i]; attacker[i] = attacker[j]; attacker[j] = t;
            }
        }
    }
    for (i = 0; i < 2; i++) {
        for (j = i + 1; j < 2; j++) {
            if (defender[j] > defender[i]) {
                int t = defender[i]; defender[i] = defender[j]; defender[j] = t;
            }
        }
    }
    {
        int a0 = attacker[0] + 1;
        int d0 = defender[0] + 1;
        if (a0 > 6) a0 = 6;
        if (d0 > 6) d0 = 6;
        if (a0 > d0) defender_losses++; else attacker_losses++;
        if (attacker[1] > defender[1]) defender_losses++; else attacker_losses++;
    }
    memset(&action, 0, sizeof(action));
    action.type = ACT_ATTACK;
    action.from = 0;
    action.to = 1;
    action.dice = 3;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(count >= 1 && events[0].kind == EVENT_ATTACK);
    CHECK(events[0].battles[0].attacker_bonus == 1);
    CHECK(events[0].battles[0].defender_bonus == 1);
    for (i = 0; i < 3; i++) {
        CHECK(events[0].battles[0].attacker[i] == attacker[i]);
    }
    for (i = 0; i < 2; i++) {
        CHECK(events[0].battles[0].defender[i] == defender[i]);
    }
    CHECK(events[0].battles[0].attacker_losses == attacker_losses);
    CHECK(events[0].battles[0].defender_losses == defender_losses);
}

static void test_leader_and_site_draw(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;

    init_card_game(&g, 2, 11);
    g.phase = PHASE_CONQUEST_MOVE;
    g.current_player = 0;
    g.territories[0].owner = 0;
    g.territories[0].armies = 5;
    g.territories[0].leader_of = 1;
    g.seats[0].leaders_in_play = 1;
    g.territories[2].owner = 0;
    g.territories[2].armies = 0;
    g.pending_from = 0;
    g.pending_to = 2;
    g.pending_min = 1;
    g.pending_max = 2;
    g.adventure_deck_count = 1;
    g.adventure_deck[0] = find_card(&g, "HOLD_TERRITORIES");
    g.adventure_discard_count = 0;
    memset(&action, 0, sizeof(action));
    action.type = ACT_MOVE_IN;
    action.from = 0;
    action.to = 2;
    action.count = 1;
    action.leader = 1;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.territories[2].leader_of == 1);
    CHECK(g.seats[0].adventure_hand_count == 1);
    CHECK(g.adventure_deck_count == 0);
    CHECK(g.seats[0].site_claimed[0] & (1u << 2));
    CHECK(g.seats[0].attack_bonus == 0);

    /* A claimed site never draws a second card. */
    prepare_site_draw(&g);
    g.adventure_deck_count = 1;
    g.adventure_deck[0] = find_card(&g, "EXTRA_REINFORCE");
    g.territories[2].leader_of = 1;
    end_turn_player1(&g);
    CHECK(g.seats[0].adventure_hand_count == 1);
}

static void test_hand_limit_and_end_turn_site(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int i;

    init_card_game(&g, 2, 2027);
    prepare_site_draw(&g);
    for (i = 0; i < 4; i++) {
        (void)give_card(&g, 0, i == 0 ? "HOLD_TERRITORIES" :
                                  i == 1 ? "HOLD_REGION" :
                                  i == 2 ? "HOLD_STRONGHOLDS" :
                                           "LEADER_REACH");
    }
    put_deck_event(&g, "EXTRA_REINFORCE");
    memset(&action, 0, sizeof(action));
    action.type = ACT_END_TURN;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.current_player == 0);
    CHECK(g.phase == PHASE_FORTIFY);
    CHECK(g.seats[0].adventure_hand_count == 5);
    CHECK(g.must_discard_adventure == 1);
    memset(&action, 0, sizeof(action));
    action.type = ACT_DISCARD_ADVENTURE;
    action.card_a = 0;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.seats[0].adventure_hand_count == 4);
    CHECK(g.must_discard_adventure == 0);
    memset(&action, 0, sizeof(action));
    action.type = ACT_END_TURN;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.current_player == 1);
}

static void test_power_cards(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int slot;
    int cost = test_map.rules.leaderCost;

    /* EXTRA_REINFORCE */
    init_card_game(&g, 2, 44);
    g.phase = PHASE_REINFORCE;
    g.reinforcements_pending = 0;
    slot = give_card(&g, 0, "EXTRA_REINFORCE");
    memset(&action, 0, sizeof(action));
    action.type = ACT_PLAY_CARD;
    action.card_a = slot;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.reinforcements_pending == 3);
    CHECK(g.seats[0].adventure_hand_count == 0);

    /* ATTACK_BONUS is consumed by the next battle. */
    init_card_game(&g, 2, 55);
    g.phase = PHASE_ATTACK;
    g.current_player = 0;
    g.territories[0].owner = 0;
    g.territories[0].armies = 8;
    g.territories[0].leader_of = 0;
    g.territories[1].owner = 1;
    g.territories[1].armies = 4;
    slot = give_card(&g, 0, "ATTACK_BONUS");
    memset(&action, 0, sizeof(action));
    action.type = ACT_PLAY_CARD;
    action.card_a = slot;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.seats[0].attack_bonus == 1);
    memset(&action, 0, sizeof(action));
    action.type = ACT_ATTACK;
    action.from = 0;
    action.to = 1;
    action.dice = 2;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.seats[0].attack_bonus == 0);
    CHECK(events[0].battles[0].attacker_bonus >= 1);

    /* FREE_FORTIFY adds one move. */
    init_card_game(&g, 2, 66);
    g.phase = PHASE_ATTACK;
    g.territories[0].owner = 0;
    g.territories[0].armies = 5;
    g.territories[1].owner = 0;
    g.territories[1].armies = 3;
    slot = give_card(&g, 0, "FREE_FORTIFY");
    memset(&action, 0, sizeof(action));
    action.type = ACT_PLAY_CARD;
    action.card_a = slot;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.extra_fortifies == 1);
    g.phase = PHASE_FORTIFY;
    memset(&action, 0, sizeof(action));
    action.type = ACT_FORTIFY;
    action.from = 0;
    action.to = 1;
    action.count = 1;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    memset(&action, 0, sizeof(action));
    action.type = ACT_FORTIFY;
    action.from = 1;
    action.to = 0;
    action.count = 1;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.extra_fortifies == 0);
    memset(&action, 0, sizeof(action));
    action.type = ACT_FORTIFY;
    action.from = 0;
    action.to = 1;
    action.count = 1;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) != 0);

    /* RECRUIT_LEADER power and paid recruitment. */
    init_card_game(&g, 2, 77);
    g.phase = PHASE_REINFORCE;
    g.seats[0].leaders_in_play = 0;
    g.reinforcements_pending = 0;
    g.territories[0].owner = 0;
    g.territories[0].armies = 3;
    slot = give_card(&g, 0, "RECRUIT_LEADER");
    memset(&action, 0, sizeof(action));
    action.type = ACT_PLAY_CARD;
    action.card_a = slot;
    action.territory = 0;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.territories[0].leader_of == 1);
    CHECK(g.seats[0].leaders_in_play == 1);

    init_card_game(&g, 2, 88);
    g.phase = PHASE_REINFORCE;
    g.seats[0].leaders_in_play = 0;
    g.reinforcements_pending = cost + 1;
    g.territories[0].owner = 0;
    g.territories[0].armies = 3;
    memset(&action, 0, sizeof(action));
    action.type = ACT_RECRUIT_LEADER;
    action.territory = 0;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.reinforcements_pending == 1);
    CHECK(g.territories[0].leader_of == 1);
}

static void test_reactive_cards(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int slot;

    init_card_game(&g, 2, 99);
    g.phase = PHASE_ATTACK;
    g.current_player = 0;
    g.territories[0].owner = 0;
    g.territories[0].armies = 10;
    g.territories[1].owner = 1;
    g.territories[1].armies = 5;
    slot = give_card(&g, 1, "DEFENSE_BONUS");
    g.seats[1].adventure_auto[slot] = 1; /* any stronghold */
    (void)give_card(&g, 1, "REROLL");
    g.seats[1].adventure_auto[slot + 1] = 1;
    memset(&action, 0, sizeof(action));
    action.type = ACT_ATTACK;
    action.from = 0;
    action.to = 1;
    action.dice = 2;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.seats[1].adventure_hand_count == 0);
    {
        int e;
        int found = 0;
        for (e = 0; e < count; e++) {
            if (events[e].kind == EVENT_ATTACK) {
                CHECK(events[e].battles[0].defender_bonus >= 3);
                found = 1;
            }
        }
        CHECK(found);
    }
}

static void test_event_effects(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;

    /* REGION_ATTRITION */
    init_card_game(&g, 2, 101);
    prepare_site_draw(&g);
    g.territories[0].owner = 0;
    g.territories[0].armies = 5;
    put_deck_event(&g, "REGION_ATTRITION");
    memset(&action, 0, sizeof(action));
    action.type = ACT_END_TURN;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.territories[0].armies == 4);

    /* REINFORCE_ALL */
    init_card_game(&g, 2, 102);
    prepare_site_draw(&g);
    g.territories[1].owner = 1;
    g.territories[1].armies = 3;
    g.territories[5].owner = 0;
    g.territories[5].armies = 3;
    put_deck_event(&g, "REINFORCE_ALL");
    memset(&action, 0, sizeof(action));
    action.type = ACT_END_TURN;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.territories[1].armies == 5);
    CHECK(g.territories[5].armies == 5);

    /* LOSE_LEADER_UNLESS: a territory card is discarded first. */
    init_card_game(&g, 2, 103);
    prepare_site_draw(&g);
    g.seats[0].hand_count = 1;
    g.seats[0].hand[0] = 0;
    put_deck_event(&g, "LOSE_LEADER_UNLESS");
    memset(&action, 0, sizeof(action));
    action.type = ACT_END_TURN;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.seats[0].hand_count == 0);
    CHECK(g.territories[2].leader_of == 1);

    /* LOSE_LEADER_UNLESS: with no card, the leader is lost. */
    init_card_game(&g, 2, 104);
    prepare_site_draw(&g);
    g.seats[0].hand_count = 0;
    put_deck_event(&g, "LOSE_LEADER_UNLESS");
    memset(&action, 0, sizeof(action));
    action.type = ACT_END_TURN;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.territories[2].leader_of == 0);
    CHECK(g.seats[0].leaders_in_play == 0);

    /* SEA_CLOSED blocks the link until the next turn. */
    init_card_game(&g, 2, 105);
    prepare_site_draw(&g);
    g.territories[3].owner = 0;
    g.territories[3].armies = 3;
    g.territories[4].owner = 0;
    g.territories[4].armies = 3;
    CHECK(game_can_fortify(&g, 0, 3, 4) == 1);
    put_deck_event(&g, "SEA_CLOSED");
    memset(&action, 0, sizeof(action));
    action.type = ACT_END_TURN;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(game_can_fortify(&g, 0, 3, 4) == 0);
    {
        char view[65536];
        CHECK(game_view(&g, 0, view, sizeof(view)) == 0);
        CHECK(strstr(view, "\"closedLinks\":[{\"a\":3,\"b\":4}]")
              != NULL);
    }
}

static void test_cancel_event(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int slot;

    init_card_game(&g, 2, 106);
    prepare_site_draw(&g);
    g.territories[0].owner = 0;
    g.territories[0].armies = 5;
    slot = give_card(&g, 1, "CANCEL_EVENT");
    g.seats[1].adventure_auto[slot] = 1;
    put_deck_event(&g, "REGION_ATTRITION");
    memset(&action, 0, sizeof(action));
    action.type = ACT_END_TURN;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.territories[0].armies == 5);
    CHECK(g.seats[1].adventure_hand_count == 0);
}

static void finish_mission_start(struct Game *g, const char *effect)
{
    (void)give_card(g, 0, effect);
    end_turn_player1(g);
}

static void test_missions(void)
{
    struct Game g;
    int i;

    /* HOLD_TERRITORIES */
    init_card_game(&g, 2, 201);
    g.territories[0].owner = 0;
    g.territories[1].owner = 0;
    finish_mission_start(&g, "HOLD_TERRITORIES");
    CHECK(g.seats[0].missions_done == 1);
    CHECK(g.seats[0].adventure_hand_count == 0);
    CHECK(g.reinforcements_pending >= 2);

    /* HOLD_REGION */
    init_card_game(&g, 2, 202);
    for (i = 0; i < 4; i++) {
        g.territories[i].owner = 0;
    }
    finish_mission_start(&g, "HOLD_REGION");
    CHECK(g.seats[0].missions_done == 1);
    CHECK(g.reinforcements_pending >= 3);

    /* HOLD_STRONGHOLDS */
    init_card_game(&g, 2, 203);
    g.territories[1].owner = 0;
    finish_mission_start(&g, "HOLD_STRONGHOLDS");
    CHECK(g.seats[0].missions_done == 1);

    /* LEADER_REACH */
    init_card_game(&g, 2, 204);
    g.territories[2].owner = 0;
    g.territories[2].leader_of = 1;
    g.seats[0].leaders_in_play = 1;
    finish_mission_start(&g, "LEADER_REACH");
    CHECK(g.seats[0].missions_done == 1);

    /* CONQUER_N_THIS_TURN completes during the attack phase. */
    init_card_game(&g, 2, 205);
    g.phase = PHASE_ATTACK;
    g.current_player = 0;
    g.territories[0].owner = 0;
    g.territories[0].armies = 20;
    g.territories[1].owner = 1;
    g.territories[1].armies = 1;
    (void)give_card(&g, 0, "CONQUER_N_THIS_TURN");
    for (i = 0; i < 12 && g.territories[1].owner != 0; i++) {
        struct Action action;
        struct Event events[GAME_MAX_EVENTS_PER_ACTION];
        char error[256];
        int count;
        memset(&action, 0, sizeof(action));
        action.type = ACT_ATTACK;
        action.from = 0;
        action.to = 1;
        action.dice = 3;
        action.repeat = 1;
        action.stop_at = 1;
        CHECK(do_action(&g, 0, &action, events, &count, error,
                        sizeof(error)) == 0);
    }
    CHECK(g.territories[1].owner == 0);
    CHECK(g.seats[0].missions_done == 1);
    CHECK(g.deferred_card_bonus[0] >= 2);
}

int test_cards_suite(void)
{
    failures = 0;
    checks = 0;
    test_leader_stronghold_combat();
    test_leader_and_site_draw();
    test_hand_limit_and_end_turn_site();
    test_power_cards();
    test_reactive_cards();
    test_event_effects();
    test_cancel_event();
    test_missions();
    printf("%s: %d card checks, %d failures\n",
           failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures;
}
