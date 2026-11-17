#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void build_test_map(void)
{
    int i;
    static const int adjacency[8][2] = {
        { 1, -1 }, { 0, 2 }, { 1, 3 }, { 2, -1 },
        { 5, -1 }, { 4, 6 }, { 5, 7 }, { 6, -1 }
    };
    static const int adjacency_count[8] = { 1, 2, 2, 1, 1, 2, 2, 1 };
    static const char symbols[8] = { 'A', 'A', 'A', 'B', 'B', 'B', 'C', 'C' };

    if (test_map_ready) {
        return;
    }
    memset(&test_map, 0, sizeof(test_map));
    strcpy(test_map.name, "Test Map");
    test_map.version = 1;
    strcpy(test_map.image.file, "board.svg");
    test_map.image.width = 100;
    test_map.image.height = 100;
    strcpy(test_map.dir, "maps/test");
    test_map.factionCount = 4;
    for (i = 0; i < 4; i++) {
        snprintf(test_map.factions[i].name,
                 sizeof(test_map.factions[i].name), "Faction%d", i);
        strcpy(test_map.factions[i].color, "#808080");
        strcpy(test_map.factions[i].unit1, "pieces/unit1.svg");
        strcpy(test_map.factions[i].unit3, "pieces/unit3.svg");
        strcpy(test_map.factions[i].unit5, "pieces/unit5.svg");
        strcpy(test_map.factions[i].leader, "pieces/leader.svg");
        test_map.factions[i].id = i;
    }
    test_map.regionCount = 2;
    strcpy(test_map.regions[0].name, "North");
    test_map.regions[0].bonus = 2;
    strcpy(test_map.regions[0].color, "#aa0000");
    test_map.regions[1].id = 1;
    strcpy(test_map.regions[1].name, "South");
    test_map.regions[1].bonus = 3;
    strcpy(test_map.regions[1].color, "#00aa00");
    test_map.territoryCount = 8;
    for (i = 0; i < 8; i++) {
        struct MapTerritory *t = &test_map.territories[i];
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
    test_map.linkCount = 1;
    test_map.links[0].a = 3;
    test_map.links[0].b = 4;
    strcpy(test_map.links[0].kind, "bridge");
    strcpy(test_map.links[0].path, "M 0,0 L 1,1");
    test_map.territoryCardCount = 10;
    for (i = 0; i < 8; i++) {
        test_map.territoryCards[i].territory = i;
        test_map.territoryCards[i].symbol = symbols[i];
    }
    test_map.territoryCards[8].wild = 1;
    test_map.territoryCards[8].territory = -1;
    test_map.territoryCards[9].wild = 1;
    test_map.territoryCards[9].territory = -1;
    test_map.adventureCardCount = 0;
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
            fprintf(stderr, "join failed: %s\n", error);
        }
    }
}

static int start_test_game(struct Game *g, int players, int mode,
                           unsigned long long seed)
{
    char error[256];

    build_test_map();
    game_init(g, &test_map, 1, "test game", "p0", seed);
    add_players(g, players);
    return game_start(g, mode, 5, error, sizeof(error));
}

/* Create a game already in the attack phase for focused action tests. */
static void init_playing(struct Game *g, int players, unsigned long long seed)
{
    int i;

    build_test_map();
    game_init(g, &test_map, 1, "test game", "p0", seed);
    add_players(g, players);
    g->status = GAME_RUNNING;
    g->phase = PHASE_ATTACK;
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
        g->seats[i].leaders_in_play = 0;
        g->seats[i].last_seen_version = g->version;
    }
    g->deck_count = test_map.territoryCardCount;
    for (i = 0; i < g->deck_count; i++) {
        g->deck[i] = i;
    }
    g->discard_count = 0;
    g->adventure_deck_count = 0;
    g->adventure_discard_count = 0;
    g->conquered_this_turn = 0;
    g->cards_traded_this_turn = 0;
    g->fortified_this_turn = 0;
    g->must_trade = 0;
    g->pending_from = -1;
    g->pending_to = -1;
    g->pending_min = 0;
    g->pending_max = 0;
    g->reinforcements_pending = 0;
    memset(&g->reinforce_info, 0, sizeof(g->reinforce_info));
    memset(g->deferred_card_bonus, 0, sizeof(g->deferred_card_bonus));
    memset(&g->log, 0, sizeof(g->log));
    g->version++;
    dice_seed(&g->rng, seed);
}

static int do_action(struct Game *g, int player, struct Action *action,
                     struct Event *events, int *event_count,
                     char *err, size_t err_size)
{
    return game_apply(g, player, action, events, event_count, err,
                      err_size);
}

static int remove_card_from_deck(struct Game *g, int card)
{
    int i;
    for (i = 0; i < g->deck_count; i++) {
        if (g->deck[i] == card) {
            int j;
            for (j = i; j + 1 < g->deck_count; j++) {
                g->deck[j] = g->deck[j + 1];
            }
            g->deck_count--;
            return 1;
        }
    }
    return 0;
}

static int remove_card_from_discard(struct Game *g, int card)
{
    int i;
    for (i = 0; i < g->discard_count; i++) {
        if (g->discard[i] == card) {
            int j;
            for (j = i; j + 1 < g->discard_count; j++) {
                g->discard[j] = g->discard[j + 1];
            }
            g->discard_count--;
            return 1;
        }
    }
    return 0;
}

/* Give a card to a player while keeping deck+discard+hands conserved. */
static void give_card(struct Game *g, int player, int card)
{
    if (!remove_card_from_deck(g, card)) {
        remove_card_from_discard(g, card);
    }
    if (g->seats[player].hand_count < GAME_MAX_CARDS) {
        g->seats[player].hand[g->seats[player].hand_count++] = card;
    }
}

static int same_game(const struct Game *a, const struct Game *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static int check_conservation(const struct Game *g)
{
    int seen[GAME_MAX_CARDS];
    int i, j;
    int total = 0;

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < g->deck_count; i++) {
        int c = g->deck[i];
        if (c < 0 || c >= g->map->territoryCardCount || seen[c]) {
            return 0;
        }
        seen[c] = 1;
        total++;
    }
    for (i = 0; i < g->discard_count; i++) {
        int c = g->discard[i];
        if (c < 0 || c >= g->map->territoryCardCount || seen[c]) {
            return 0;
        }
        seen[c] = 1;
        total++;
    }
    for (i = 0; i < g->player_count; i++) {
        for (j = 0; j < g->seats[i].hand_count; j++) {
            int c = g->seats[i].hand[j];
            if (c < 0 || c >= g->map->territoryCardCount || seen[c]) {
                return 0;
            }
            seen[c] = 1;
            total++;
        }
    }
    for (i = 0; i < g->adventure_deck_count; i++) {
        total++; /* adventure cards are a separate deck */
    }
    return total == g->map->territoryCardCount + g->adventure_deck_count +
                    g->adventure_discard_count;
}

static void test_setup_deal(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int guard;
    int t;
    int sum = 0;

    CHECK(start_test_game(&g, 2, GAME_START_DEAL, 111) == 0);
    CHECK(g.status == GAME_RUNNING);
    CHECK(g.phase == PHASE_SETUP_PLACE);
    CHECK(g.setup_pool[0] == 16);
    CHECK(g.setup_pool[1] == 16);
    CHECK(g.territory_count == 8);

    guard = 0;
    while (g.phase == PHASE_SETUP_PLACE && guard++ < 100) {
        struct LegalActions legal;
        int found = -1;
        game_legal_actions(&g, &legal);
        for (t = 0; t < g.territory_count; t++) {
            if (legal.placeable[t]) {
                found = t;
                break;
            }
        }
        CHECK(found >= 0);
        if (found < 0) {
            break;
        }
        memset(&action, 0, sizeof(action));
        action.type = ACT_PLACE;
        action.territory = found;
        action.count = legal.setup_batch_remaining;
        CHECK(do_action(&g, g.current_player, &action, events, &count,
                        error, sizeof(error)) == 0);
    }
    CHECK(g.phase == PHASE_SETUP_LEADERS);
    guard = 0;
    while (g.phase == PHASE_SETUP_LEADERS && guard++ < 100) {
        struct LegalActions legal;
        int found = -1;
        game_legal_actions(&g, &legal);
        for (t = 0; t < g.territory_count; t++) {
            if (legal.leader_placeable[t]) {
                found = t;
                break;
            }
        }
        CHECK(found >= 0);
        if (found < 0) {
            break;
        }
        memset(&action, 0, sizeof(action));
        action.type = ACT_PLACE_LEADER;
        action.territory = found;
        CHECK(do_action(&g, g.current_player, &action, events, &count,
                        error, sizeof(error)) == 0);
    }
    CHECK(g.phase == PHASE_REINFORCE);
    CHECK(g.current_player == 0);
    CHECK(g.seats[0].leaders_in_play == 2);
    CHECK(g.seats[1].leaders_in_play == 2);
    CHECK(game_count_territories(&g, 0) == 4);
    CHECK(game_count_territories(&g, 1) == 4);
    for (t = 0; t < g.territory_count; t++) {
        sum += g.territories[t].armies;
    }
    CHECK(sum == 40);
    CHECK(check_conservation(&g));
}

static void test_setup_draft(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int guard;
    int t;

    CHECK(start_test_game(&g, 2, GAME_START_DRAFT, 222) == 0);
    CHECK(g.phase == PHASE_SETUP_CLAIM);
    guard = 0;
    while (g.phase == PHASE_SETUP_CLAIM && guard++ < 100) {
        struct LegalActions legal;
        int found = -1;
        game_legal_actions(&g, &legal);
        for (t = 0; t < g.territory_count; t++) {
            if (legal.claimable[t]) {
                found = t;
                break;
            }
        }
        CHECK(found >= 0);
        if (found < 0) {
            break;
        }
        memset(&action, 0, sizeof(action));
        action.type = ACT_CLAIM;
        action.territory = found;
        CHECK(do_action(&g, g.current_player, &action, events, &count,
                        error, sizeof(error)) == 0);
    }
    CHECK(g.phase == PHASE_SETUP_PLACE);
    CHECK(g.setup_pool[0] == 16);
    CHECK(g.setup_pool[1] == 16);
    guard = 0;
    while (g.phase == PHASE_SETUP_PLACE && guard++ < 100) {
        struct LegalActions legal;
        int found = -1;
        game_legal_actions(&g, &legal);
        for (t = 0; t < g.territory_count; t++) {
            if (legal.placeable[t]) {
                found = t;
                break;
            }
        }
        CHECK(found >= 0);
        if (found < 0) {
            break;
        }
        memset(&action, 0, sizeof(action));
        action.type = ACT_PLACE;
        action.territory = found;
        action.count = legal.setup_batch_remaining;
        CHECK(do_action(&g, g.current_player, &action, events, &count,
                        error, sizeof(error)) == 0);
    }
    CHECK(g.phase == PHASE_SETUP_LEADERS);
    CHECK(game_count_territories(&g, 0) == 4);
    CHECK(game_count_territories(&g, 1) == 4);
}

static void test_reinforce_math(void)
{
    struct Game g;
    struct ReinforcementInfo info;
    int i;

    init_playing(&g, 2, 333);
    g.phase = PHASE_REINFORCE;
    for (i = 0; i < 8; i++) {
        g.territories[i].owner = 0;
        g.territories[i].armies = 1;
    }
    CHECK(game_reinforcements(&g, 0, &info) == 0);
    CHECK(info.territories == 8);
    CHECK(info.base == 3);
    CHECK(info.region_bonus == 5);
    CHECK(info.stronghold_bonus == 2);
    CHECK(info.regions_held == 2);
    CHECK(info.strongholds_held == 2);
    CHECK(info.total == 10);

    for (i = 4; i < 8; i++) {
        g.territories[i].owner = 1;
    }
    CHECK(game_reinforcements(&g, 0, &info) == 0);
    CHECK(info.territories == 4);
    CHECK(info.base == 3);
    CHECK(info.region_bonus == 2);
    CHECK(info.stronghold_bonus == 1);
    CHECK(info.total == 6);
}

static void test_attack_determinism(void)
{
    struct Game g;
    struct Game before;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    struct DiceRng predicted;
    int attacker_dice[3];
    int defender_dice[2];
    int attacker_losses = 0;
    int defender_losses = 0;
    int i;
    int pairs;

    init_playing(&g, 2, 4242);
    g.phase = PHASE_ATTACK;
    g.current_player = 0;
    g.territories[0].owner = 0;
    g.territories[0].armies = 6;
    g.territories[1].owner = 1;
    g.territories[1].armies = 3;
    g.territories[2].owner = 1;
    g.territories[2].armies = 1;

    predicted = g.rng;
    attacker_dice[0] = dice_roll(&predicted);
    attacker_dice[1] = dice_roll(&predicted);
    defender_dice[0] = dice_roll(&predicted);
    defender_dice[1] = dice_roll(&predicted);
    for (i = 0; i < 2; i++) {
        int j;
        for (j = i + 1; j < 2; j++) {
            if (attacker_dice[j] > attacker_dice[i]) {
                int tmp = attacker_dice[i];
                attacker_dice[i] = attacker_dice[j];
                attacker_dice[j] = tmp;
            }
            if (defender_dice[j] > defender_dice[i]) {
                int tmp = defender_dice[i];
                defender_dice[i] = defender_dice[j];
                defender_dice[j] = tmp;
            }
        }
    }
    pairs = 2;
    {
        int raw_high = defender_dice[0];
        if (defender_dice[0] < 6) {
            defender_dice[0]++;  /* T1 is a stronghold in the test map */
        }
        for (i = 0; i < pairs; i++) {
            if (attacker_dice[i] > defender_dice[i]) {
                defender_losses++;
            } else {
                attacker_losses++;
            }
        }
        defender_dice[0] = raw_high;  /* event dice are the raw rolls */
    }

    memset(&action, 0, sizeof(action));
    action.type = ACT_ATTACK;
    action.from = 0;
    action.to = 1;
    action.dice = 2;
    before = g;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(count >= 1);
    CHECK(events[0].kind == EVENT_ATTACK);
    CHECK(events[0].battle_count == 1);
    CHECK(events[0].battles[0].attacker_count == 2);
    CHECK(events[0].battles[0].defender_count == 2);
    for (i = 0; i < 2; i++) {
        CHECK(events[0].battles[0].attacker[i] == attacker_dice[i]);
        CHECK(events[0].battles[0].defender[i] == defender_dice[i]);
    }
    CHECK(g.territories[0].armies == 6 - attacker_losses);
    CHECK(g.territories[1].armies == 3 - defender_losses);
    CHECK(g.seats[0].stats.armies_lost == attacker_losses);
    CHECK(g.seats[0].stats.armies_killed == defender_losses);
    CHECK(g.seats[1].stats.armies_lost == defender_losses);
    CHECK(g.seats[1].stats.armies_killed == attacker_losses);
    CHECK(g.version == before.version + 1);
    CHECK(g.phase == PHASE_ATTACK);
}

static void test_attack_rejections(void)
{
    struct Game g;
    struct Game before;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;

    init_playing(&g, 2, 555);
    g.phase = PHASE_ATTACK;
    g.current_player = 0;
    g.territories[0].owner = 0;
    g.territories[0].armies = 5;
    g.territories[1].owner = 1;
    g.territories[1].armies = 3;

    memset(&action, 0, sizeof(action));
    action.type = ACT_ATTACK;
    action.from = 0;
    action.to = 1;
    action.dice = 2;

    before = g;
    CHECK(do_action(&g, 1, &action, events, &count, error,
                    sizeof(error)) == -1);
    CHECK(same_game(&before, &g));

    action.dice = 4;
    before = g;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == -1);
    CHECK(same_game(&before, &g));

    action.dice = 2;
    action.to = 2;
    before = g;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == -1);
    CHECK(same_game(&before, &g));

    action.to = 1;
    g.territories[0].armies = 1;
    before = g;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == -1);
    CHECK(same_game(&before, &g));

    g.territories[0].armies = 5;
    g.territories[1].owner = 0;
    before = g;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == -1);
    CHECK(same_game(&before, &g));

    g.territories[1].owner = 1;
    g.phase = PHASE_REINFORCE;
    before = g;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == -1);
    CHECK(same_game(&before, &g));
}

static void test_blitz_conquest(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;

    init_playing(&g, 2, 666);
    g.phase = PHASE_ATTACK;
    g.current_player = 0;
    g.territories[0].owner = 0;
    g.territories[0].armies = 20;
    g.territories[1].owner = 1;
    g.territories[1].armies = 1;
    g.territories[5].owner = 1;
    g.territories[5].armies = 2;

    memset(&action, 0, sizeof(action));
    action.type = ACT_ATTACK;
    action.from = 0;
    action.to = 1;
    action.dice = 1;
    action.repeat = 1;
    action.stop_at = 1;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.territories[1].owner == 0);
    CHECK(g.phase == PHASE_CONQUEST_MOVE);
    CHECK(g.pending_min >= 1);
    CHECK(g.pending_min <= g.pending_max);
    CHECK(g.conquered_this_turn == 1);

    memset(&action, 0, sizeof(action));
    action.type = ACT_MOVE_IN;
    action.from = 0;
    action.to = 1;
    action.count = g.pending_min;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.phase == PHASE_ATTACK);
    CHECK(g.territories[1].armies == action.count);
    CHECK(g.territories[0].armies >= 1);
    CHECK(check_conservation(&g));
}

static int hook_calls;
static int hook_winner;

static void test_end_hook(struct Game *game, int winner, void *user)
{
    (void)game;
    hook_calls++;
    hook_winner = winner;
    if (user != NULL) {
        *(int *)user = 42;
    }
}

static void test_elimination_and_victory(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int i;
    int hook_user = 0;

    init_playing(&g, 2, 777);
    g.phase = PHASE_ATTACK;
    g.current_player = 0;
    hook_calls = 0;
    hook_winner = -1;
    g.end_hook = test_end_hook;
    g.end_hook_user = &hook_user;
    for (i = 0; i < g.territory_count; i++) {
        g.territories[i].owner = 0;
        g.territories[i].armies = 1;
    }
    g.territories[0].armies = 30;
    g.territories[1].owner = 1;
    g.territories[1].armies = 1;
    give_card(&g, 1, 0);

    memset(&action, 0, sizeof(action));
    action.type = ACT_ATTACK;
    action.from = 0;
    action.to = 1;
    action.dice = 1;
    action.repeat = 1;
    action.stop_at = 1;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.status == GAME_FINISHED);
    CHECK(g.phase == PHASE_FINISHED);
    CHECK(g.winner == 0);
    CHECK(g.seats[1].alive == 0);
    CHECK(g.seats[0].hand_count == 1);
    CHECK(g.seats[0].hand[0] == 0);
    CHECK(g.seats[0].stats.wins == 1);
    CHECK(g.seats[1].stats.losses == 1);
    CHECK(g.seats[0].stats.games_played == 1);
    CHECK(hook_calls == 1);
    CHECK(hook_winner == 0);
    CHECK(hook_user == 42);
    CHECK(check_conservation(&g));
}

static void test_trade_cards(void)
{
    struct Game g;
    struct Game before;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int a, b, c;

    init_playing(&g, 2, 888);
    g.phase = PHASE_REINFORCE;
    g.current_player = 0;
    g.seats[0].hand_count = 0;
    give_card(&g, 0, 0);
    give_card(&g, 0, 1);
    give_card(&g, 0, 2);
    g.territories[0].owner = 0;
    g.territories[1].owner = 0;
    g.territories[2].owner = 0;
    g.territories[0].armies = 1;
    g.territories[1].armies = 1;
    g.territories[2].armies = 1;
    g.reinforcements_pending = 0;
    g.must_trade = 0;

    memset(&action, 0, sizeof(action));
    action.type = ACT_TRADE_CARDS;
    action.card_a = 0;
    action.card_b = 1;
    action.card_c = 2;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.seats[0].hand_count == 0);
    CHECK(g.discard_count == 3);
    CHECK(g.reinforcements_pending == 4);
    CHECK(g.reinforce_info.card_bonus == 4);
    CHECK(g.territories[0].armies == 3);
    CHECK(g.territories[1].armies == 3);
    CHECK(g.territories[2].armies == 3);
    CHECK(events[0].kind == EVENT_TRADE_CARDS);
    CHECK(events[0].set_bonus == 4);
    CHECK(check_conservation(&g));

    /* Invalid set: A, A, B. */
    init_playing(&g, 2, 889);
    g.phase = PHASE_REINFORCE;
    g.current_player = 0;
    g.seats[0].hand_count = 0;
    give_card(&g, 0, 0);
    give_card(&g, 0, 1);
    give_card(&g, 0, 3);
    memset(&action, 0, sizeof(action));
    action.type = ACT_TRADE_CARDS;
    action.card_a = 0;
    action.card_b = 1;
    action.card_c = 3;
    before = g;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == -1);
    CHECK(same_game(&before, &g));

    /* Wild card: A, C, wild -> one of each. */
    init_playing(&g, 2, 890);
    g.phase = PHASE_REINFORCE;
    g.current_player = 0;
    g.seats[0].hand_count = 0;
    give_card(&g, 0, 0);
    give_card(&g, 0, 7);
    give_card(&g, 0, 8);
    g.territories[0].owner = 0;
    g.territories[7].owner = 0;
    g.territories[0].armies = 1;
    g.territories[7].armies = 1;
    g.reinforcements_pending = 0;
    memset(&action, 0, sizeof(action));
    action.type = ACT_TRADE_CARDS;
    action.card_a = 0;
    action.card_b = 7;
    action.card_c = 8;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.reinforcements_pending == 10);
    CHECK(g.territories[0].armies == 3);
    CHECK(g.territories[7].armies == 3);
    CHECK(check_conservation(&g));

    /* find_card_set handles one of each without wilds. */
    init_playing(&g, 2, 891);
    g.seats[0].hand_count = 0;
    give_card(&g, 0, 0);
    give_card(&g, 0, 3);
    give_card(&g, 0, 6);
    CHECK(game_find_card_set(&g, 0, &a, &b, &c) == 1);
    CHECK(a == 0 && b == 3 && c == 6);
}

static void test_fortify(void)
{
    struct Game g;
    struct Game before;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int i;

    init_playing(&g, 2, 999);
    g.phase = PHASE_FORTIFY;
    g.current_player = 0;
    g.fortified_this_turn = 0;
    for (i = 0; i < 8; i++) {
        g.territories[i].owner = 0;
        g.territories[i].armies = 1;
    }
    g.territories[0].armies = 5;
    g.territories[1].owner = 1;
    g.territories[1].armies = 2;

    memset(&action, 0, sizeof(action));
    action.type = ACT_FORTIFY;
    action.from = 0;
    action.to = 3;
    action.count = 2;
    before = g;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == -1);
    CHECK(same_game(&before, &g));

    g.territories[1].owner = 0;
    g.territories[1].armies = 1;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.territories[0].armies == 3);
    CHECK(g.territories[3].armies == 3);
    CHECK(g.fortified_this_turn == 1);
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == -1);

    g.fortified_this_turn = 0;
    g.territories[0].armies = 1;
    g.territories[3].armies = 1;
    action.count = 1;
    before = g;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == -1);
    CHECK(same_game(&before, &g));
}

static void test_end_turn_and_draw(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int expected;
    int i;

    init_playing(&g, 2, 1234);
    g.phase = PHASE_FORTIFY;
    g.current_player = 0;
    g.conquered_this_turn = 1;
    g.fortified_this_turn = 1;
    g.must_trade = 0;
    g.reinforcements_pending = 0;
    g.deck_count = 10;
    for (i = 0; i < 10; i++) {
        g.deck[i] = i;
    }
    g.discard_count = 0;
    g.seats[0].hand_count = 0;
    g.turn = 1;
    expected = g.deck[9];

    memset(&action, 0, sizeof(action));
    action.type = ACT_END_TURN;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.seats[0].hand_count == 1);
    CHECK(g.seats[0].hand[0] == expected);
    CHECK(g.deck_count == 9);
    CHECK(g.current_player == 1);
    CHECK(g.phase == PHASE_REINFORCE);
    CHECK(g.turn == 2);
    CHECK(g.conquered_this_turn == 0);
    CHECK(check_conservation(&g));

    /* Empty deck: discard is shuffled back before drawing. */
    init_playing(&g, 2, 1235);
    g.phase = PHASE_FORTIFY;
    g.current_player = 0;
    g.conquered_this_turn = 1;
    g.fortified_this_turn = 1;
    g.must_trade = 0;
    g.reinforcements_pending = 0;
    g.deck_count = 0;
    g.discard_count = 10;
    for (i = 0; i < 10; i++) {
        g.discard[i] = i;
    }
    g.seats[0].hand_count = 0;
    memset(&action, 0, sizeof(action));
    action.type = ACT_END_TURN;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.seats[0].hand_count == 1);
    CHECK(g.seats[0].hand[0] >= 0 && g.seats[0].hand[0] < 10);
    CHECK(g.deck_count == 9);
    CHECK(g.discard_count == 0);
    CHECK(check_conservation(&g));
}

static void test_resign(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count;
    int i;
    int neutral_armies;

    init_playing(&g, 3, 4321);
    g.phase = PHASE_FORTIFY;
    g.current_player = 0;
    g.fortified_this_turn = 1;
    neutral_armies = g.territories[2].armies;

    memset(&action, 0, sizeof(action));
    action.type = ACT_RESIGN;
    CHECK(do_action(&g, 2, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.status == GAME_RUNNING);
    CHECK(g.seats[2].alive == 0);
    CHECK(g.current_player == 0);
    for (i = 0; i < g.territory_count; i++) {
        if (i % 3 == 2) {
            CHECK(g.territories[i].owner == -1);
            CHECK(g.territories[i].neutral == 1);
            CHECK(g.territories[i].armies == neutral_armies);
        }
    }

    memset(&action, 0, sizeof(action));
    action.type = ACT_RESIGN;
    CHECK(do_action(&g, 1, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.status == GAME_FINISHED);
    CHECK(g.winner == 0);
    CHECK(g.seats[0].stats.wins == 1);
}


static void test_kick_and_event_versions(void)
{
    struct Game g;
    struct Action action;
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    char view[65536];
    int count;

    init_playing(&g, 3, 98765);
    g.phase = PHASE_FORTIFY;
    g.current_player = 0;
    g.fortified_this_turn = 1;
    g.kick_target = 2;
    g.kick_votes = (1u << 0) | (1u << 1);
    g.kick_started = 12345;

    CHECK(game_view(&g, 0, view, sizeof(view)) == 0);
    CHECK(strstr(view, "\"kick\":{\"target\":2,\"needed\":2") != NULL);
    CHECK(strstr(view, "\"votes\":[0,1]") != NULL);
    CHECK(strstr(view, "\"started\":12345") != NULL);

    memset(&action, 0, sizeof(action));
    action.type = ACT_KICK;
    CHECK(do_action(&g, 2, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.status == GAME_RUNNING);
    CHECK(g.seats[2].alive == 0);
    CHECK(g.kick_target == -1);
    CHECK(g.kick_votes == 0);
    CHECK(g.current_player == 0);
    CHECK(count >= 1);
    CHECK(events[0].kind == EVENT_KICK);
    CHECK(events[0].version == g.version);
    CHECK(game_log_count(&g) >= 1);
    CHECK(game_log_event(&g, game_log_count(&g) - 1)->version == g.version);
    CHECK(game_view(&g, 0, view, sizeof(view)) == 0);
    CHECK(strstr(view, "\"kind\":\"kick\",\"version\":") != NULL);

    /* Removing the current player when one opponent remains ends the game. */
    memset(&action, 0, sizeof(action));
    action.type = ACT_KICK;
    CHECK(do_action(&g, 0, &action, events, &count, error,
                    sizeof(error)) == 0);
    CHECK(g.status == GAME_FINISHED);
    CHECK(g.winner == 1);

    init_playing(&g, 2, 111);
    g.phase = PHASE_SETUP_PLACE;
    memset(&action, 0, sizeof(action));
    action.type = ACT_KICK;
    CHECK(do_action(&g, 1, &action, events, &count, error,
                    sizeof(error)) != 0);
}

static void test_legal_actions(void)
{
    struct Game g;
    struct LegalActions legal;
    int i;

    init_playing(&g, 2, 2468);
    g.phase = PHASE_ATTACK;
    g.current_player = 0;
    g.territories[0].owner = 0;
    g.territories[0].armies = 5;
    g.territories[1].owner = 1;
    g.territories[1].armies = 3;
    game_legal_actions(&g, &legal);
    CHECK(legal.attack[0][1] == 1);
    CHECK(legal.max_attack_dice[0] == 3);
    CHECK(legal.can_done_attack == 1);

    g.phase = PHASE_REINFORCE;
    g.reinforcements_pending = 3;
    for (i = 0; i < g.territory_count; i++) {
        g.territories[i].owner = 0;
    }
    game_legal_actions(&g, &legal);
    CHECK(legal.placeable[0] == 1);
    CHECK(legal.reinforcements_pending == 3);
    CHECK(legal.can_done_reinforce == 0);

    g.phase = PHASE_FORTIFY;
    g.fortified_this_turn = 0;
    for (i = 0; i < g.territory_count; i++) {
        g.territories[i].owner = 0;
    }
    g.territories[1].owner = 1;
    g.territories[0].armies = 4;
    game_legal_actions(&g, &legal);
    CHECK(legal.fortify[0][2] == 0);
    g.territories[1].owner = 0;
    game_legal_actions(&g, &legal);
    CHECK(legal.fortify[0][3] == 1);
}

static void test_view(void)
{
    struct Game g;
    char view[65536];
    char spectator[65536];

    init_playing(&g, 2, 13579);
    g.phase = PHASE_REINFORCE;
    g.current_player = 0;
    g.reinforcements_pending = 0;
    g.seats[0].hand_count = 0;
    give_card(&g, 0, 0);
    give_card(&g, 0, 1);

    CHECK(game_view(&g, 0, view, sizeof(view)) == 0);
    CHECK(strstr(view, "\"format\":1") != NULL);
    CHECK(strstr(view, "\"players\"") != NULL);
    CHECK(strstr(view, "\"territories\"") != NULL);
    CHECK(strstr(view, "\"hand\":[") != NULL);
    CHECK(strstr(view, "\"legal\":{") != NULL);
    CHECK(strstr(view, "\"deck\":") != NULL);
    CHECK(strstr(view, "\"deck\":[") == NULL);

    CHECK(game_view(&g, -1, spectator, sizeof(spectator)) == 0);
    CHECK(strstr(spectator, "\"hand\":null") != NULL);
    CHECK(strstr(spectator, "\"legal\":null") != NULL);
    CHECK(strstr(spectator, "\"hand\":[") == NULL);

    CHECK(game_view(&g, 1, view, sizeof(view)) == 0);
    CHECK(strstr(view, "\"legal\":null") != NULL);
    CHECK(strstr(view, "\"hand\":[]") != NULL);
}

static void test_start_failure_keeps_open(void)
{
    struct Game g;
    char error[256];

    build_test_map();
    test_map.setup[2].armies = 1;
    game_init(&g, &test_map, 1, "test", "p0", 1);
    add_players(&g, 2);
    CHECK(game_start(&g, GAME_START_DEAL, 5, error, sizeof(error)) == -1);
    CHECK(g.status == GAME_OPEN);
    CHECK(g.phase == PHASE_LOBBY);
    test_map.setup[2].armies = 20;
}

static void test_start_errors(void)
{
    struct Game g;
    char error[256];

    build_test_map();
    game_init(&g, &test_map, 1, "test", "p0", 1);
    CHECK(game_start(&g, GAME_START_DEAL, 5, error, sizeof(error)) == -1);
    add_players(&g, 2);
    CHECK(game_join(&g, "p0", -1, error, sizeof(error)) == -1);
    CHECK(game_start(&g, GAME_START_DEAL, 5, error, sizeof(error)) == 0);
    CHECK(game_join(&g, "p2", -1, error, sizeof(error)) == -1);
}

int test_rules_suite(void)
{
    failures = 0;
    checks = 0;
    test_setup_deal();
    test_setup_draft();
    test_reinforce_math();
    test_attack_determinism();
    test_attack_rejections();
    test_blitz_conquest();
    test_elimination_and_victory();
    test_trade_cards();
    test_fortify();
    test_end_turn_and_draw();
    test_resign();
    test_kick_and_event_versions();
    test_legal_actions();
    test_view();
    test_start_failure_keeps_open();
    test_start_errors();
    printf("%s: %d rules checks, %d failures\n",
           failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures;
}
