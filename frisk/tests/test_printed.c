/* Engine tests for the printed LOTR adventure cards (PLAN.md 10b).
 *
 * Board: T0-T1-T2-T3 (North) and T4-T5-T6-T7 (South), T3~T4 a sea-line
 * between two ports, a bridge T1-T2, a mountain range {T0, T6}, a stronghold
 * on T5 and Sites of Power on T1 and T6.  Seat 0 is a good faction, seat 1
 * an evil one.
 */
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

static struct Map pmap;

static void add_card(const char *id, const char *kind, const char *effect,
                     const char *args)
{
    struct MapAdventureCard *c = &pmap.adventureCards[pmap.adventureCardCount++];
    snprintf(c->id, sizeof(c->id), "%s", id);
    snprintf(c->kind, sizeof(c->kind), "%s", kind);
    snprintf(c->title, sizeof(c->title), "%s", id);
    snprintf(c->text, sizeof(c->text), "%s", id);
    snprintf(c->effect, sizeof(c->effect), "%s", effect);
    snprintf(c->args, sizeof(c->args), "%s", args);
}

static void build_map(void)
{
    static const int adj[8][2] = { { 1, -1 }, { 0, 2 }, { 1, 3 }, { 2, -1 },
                                   { 5, -1 }, { 4, 6 }, { 5, 7 }, { 6, -1 } };
    int i;

    memset(&pmap, 0, sizeof(pmap));
    snprintf(pmap.name, sizeof(pmap.name), "Printed Test Map");
    pmap.version = 1;
    snprintf(pmap.dir, sizeof(pmap.dir), "maps/printed");
    pmap.factionCount = 2;
    for (i = 0; i < 2; i++) {
        pmap.factions[i].id = i;
        snprintf(pmap.factions[i].name, sizeof(pmap.factions[i].name), "F%d", i);
        pmap.factions[i].side = i == 0 ? MAP_SIDE_GOOD : MAP_SIDE_EVIL;
    }
    pmap.regionCount = 2;
    snprintf(pmap.regions[0].name, sizeof(pmap.regions[0].name), "North");
    pmap.regions[0].bonus = 2;
    pmap.regions[1].id = 1;
    snprintf(pmap.regions[1].name, sizeof(pmap.regions[1].name), "South");
    pmap.regions[1].bonus = 2;
    pmap.territoryCount = 8;
    for (i = 0; i < 8; i++) {
        struct MapTerritory *t = &pmap.territories[i];
        t->id = i;
        snprintf(t->name, sizeof(t->name), "T%d", i);
        t->region = i < 4 ? 0 : 1;
        t->stronghold = i == 5;
        t->site = i == 1 || i == 6;
        t->port = i == 3 || i == 4;
        t->adjacent[0] = adj[i][0];
        t->adjacentCount = 1;
        if (adj[i][1] >= 0) {
            t->adjacent[1] = adj[i][1];
            t->adjacentCount = 2;
        }
    }
    pmap.linkCount = 1;
    pmap.links[0].a = 3;
    pmap.links[0].b = 4;
    snprintf(pmap.links[0].kind, sizeof(pmap.links[0].kind), "sea");
    pmap.bridgeCount = 1;
    pmap.bridges[0].a = 1;
    pmap.bridges[0].b = 2;
    pmap.mountainCount = 1;
    snprintf(pmap.mountains[0].name, sizeof(pmap.mountains[0].name), "Peaks");
    pmap.mountains[0].territories[0] = 0;
    pmap.mountains[0].territories[1] = 6;
    pmap.mountains[0].count = 2;
    pmap.territoryCardCount = 10;
    for (i = 0; i < 8; i++) {
        pmap.territoryCards[i].territory = i;
        pmap.territoryCards[i].symbol = "ABC"[i % 3];
    }
    pmap.territoryCards[8].wild = pmap.territoryCards[9].wild = 1;
    pmap.territoryCards[8].territory = pmap.territoryCards[9].territory = -1;
    add_card("e_free", "event", "FREE_LEADER", "{\"max\":2}");
    add_card("e_side", "event", "SIDE_CONTROL",
             "{\"territory\":\"T5\",\"side\":\"good\",\"gain\":10,\"loss\":2}");
    add_card("e_toll", "event", "SEABORNE_TOLL", "{\"loss\":3}");
    add_card("e_up", "event", "LOCAL_UPRISING", "{\"n\":4}");
    add_card("e_raid", "event", "RAIDERS", "{\"cards\":3}");
    add_card("e_must", "event", "MUSTERING", "{\"cards\":3,\"n\":3}");
    add_card("e_winter", "event", "WINTER_STORMS", "{\"cards\":3}");
    add_card("p_ambush", "power", "AMBUSH", "{\"n\":3}");
    add_card("p_sails", "power", "SEABORNE_BONUS", "{\"n\":1}");
    add_card("p_borne", "power", "LEADER_MOVE", "{}");
    add_card("p_courage", "power", "INVADED_REINFORCE", "{\"n\":4}");
    add_card("p_grima", "power", "GRIMA", "{\"n\":2}");
    add_card("p_night", "power", "EXTRA_REDEPLOY", "{\"n\":3}");
    add_card("p_siege", "power", "NO_STRONGHOLD", "{}");
    add_card("p_withdraw", "power", "WITHDRAW", "{}");
    add_card("p_way", "power", "MOUNTAIN_PASS", "{}");
    add_card("p_storm", "power", "STORM_REPEL", "{}");
    add_card("p_pass", "power", "BRIDGE_BLOCK", "{}");
    add_card("m_region", "mission", "SITE_MISSION",
             "{\"site\":\"T1\",\"reward\":\"region\",\"region\":\"North\","
             "\"good\":2,\"evil\":6}");
    add_card("m_card", "mission", "SITE_MISSION",
             "{\"site\":\"T1\",\"reward\":\"card\",\"good\":0,\"evil\":0}");
    add_card("m_oath", "mission", "SITE_MISSION",
             "{\"site\":\"T1\",\"reward\":\"oathbreakers\",\"good\":6,"
             "\"evil\":6}");
    add_card("m_port", "mission", "SITE_MISSION",
             "{\"site\":\"T1\",\"reward\":\"port\",\"good\":2,\"evil\":2}");
    pmap.rules.minReinforce = 3;
    pmap.rules.territoriesPerArmy = 3;
    pmap.rules.leadersPerPlayer = 2;
    pmap.rules.leaderCost = 3;
    pmap.rules.dieCap = 6;
    pmap.rules.adventureStartHand = 4;
    pmap.rules.drawOnLeaderConquest = 1;
}

static int card_id(const char *id)
{
    int i;
    for (i = 0; i < pmap.adventureCardCount; i++) {
        if (strcmp(pmap.adventureCards[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

/* Running game, seat 0 owns T0..T3, seat 1 owns T4..T7, 3 armies each.
 * Every adventure card starts in the deck; hands are empty. */
static void init_game(struct Game *g, int phase)
{
    char error[128];
    int i;

    game_init(g, &pmap, 1, "printed", "p0", 99);
    game_join(g, "p0", -1, error, sizeof(error));
    game_join(g, "p1", -1, error, sizeof(error));
    g->status = GAME_RUNNING;
    g->phase = phase;
    g->current_player = 0;
    g->turn = 3;
    g->territory_count = 8;
    for (i = 0; i < 8; i++) {
        g->territories[i].owner = i < 4 ? 0 : 1;
        g->territories[i].armies = 3;
    }
    for (i = 0; i < 2; i++) {
        g->seats[i].alive = 1;
    }
    g->deck_count = pmap.territoryCardCount;
    for (i = 0; i < g->deck_count; i++) {
        g->deck[i] = i;
    }
    g->adventure_deck_count = pmap.adventureCardCount;
    for (i = 0; i < g->adventure_deck_count; i++) {
        g->adventure_deck[i] = i;
    }
    g->fortified_this_turn = 1;
}

/* Move card `id` from the deck into a seat's hand; returns its slot. */
static int give(struct Game *g, int seat, const char *id)
{
    int card = card_id(id);
    int i, slot;
    for (i = 0; i < g->adventure_deck_count; i++) {
        if (g->adventure_deck[i] == card) {
            g->adventure_deck[i] = g->adventure_deck[--g->adventure_deck_count];
            break;
        }
    }
    slot = g->seats[seat].adventure_hand_count++;
    g->seats[seat].adventure_hand[slot] = card;
    return slot;
}

/* Put card `id` on top of the adventure deck. */
static void deck_top(struct Game *g, const char *id)
{
    int card = card_id(id);
    int i;
    for (i = 0; i < g->adventure_deck_count; i++) {
        if (g->adventure_deck[i] == card) {
            memmove(&g->adventure_deck[i], &g->adventure_deck[i + 1],
                    sizeof(int) * (size_t)(g->adventure_deck_count - i - 1));
            g->adventure_deck[g->adventure_deck_count - 1] = card;
            return;
        }
    }
}

/* Put territory cards for t1, t2, t3 on top of the territory deck. */
static void territory_top(struct Game *g, int t1, int t2, int t3)
{
    int want[3] = { t3, t2, t1 };   /* drawn from the end */
    int k, i;
    for (k = 0; k < 3; k++) {
        for (i = 0; i < g->deck_count; i++) {
            if (g->deck[i] == want[k]) {
                int pos = g->deck_count - 3 + k;
                int tmp = g->deck[pos];
                g->deck[pos] = g->deck[i];
                g->deck[i] = tmp;
                break;
            }
        }
    }
}

static int act(struct Game *g, int player, struct Action *a)
{
    struct Event events[GAME_MAX_EVENTS_PER_ACTION];
    char error[256];
    int count = 0;
    int rc = game_apply(g, player, a, events, &count, error, sizeof(error));
    if (getenv("PRINTED_DEBUG") != NULL && rc != 0) {
        fprintf(stderr, "action %d refused: %s\n", a->type, error);
    }
    return rc;
}

static struct Action make(int type)
{
    struct Action a;
    memset(&a, 0, sizeof(a));
    a.type = type;
    a.territory = -1;
    a.from = -1;
    a.to = -1;
    return a;
}

static int adventure_total(const struct Game *g)
{
    int i, total = g->adventure_deck_count + g->adventure_discard_count;
    for (i = 0; i < g->player_count; i++) {
        total += g->seats[i].adventure_hand_count;
    }
    return total;
}

static void test_start_deal(void)
{
    struct Game *g = calloc(1, sizeof(*g));
    int i, s;

    init_game(g, PHASE_REINFORCE);
    game_rebuild_adventure_deck(g);
    for (s = 0; s < 2; s++) {
        CHECK(g->seats[s].adventure_hand_count == 4);
        for (i = 0; i < 4; i++) {
            CHECK(strcmp(pmap.adventureCards[g->seats[s].adventure_hand[i]].kind,
                         "event") != 0);
        }
    }
    CHECK(adventure_total(g) == pmap.adventureCardCount);
    free(g);
}

/* Rulebook draw: a Leader conquered a site; Appoint a Second Leader pauses
 * the end of the turn for a choice, then drawing continues to a power. */
static void test_draw_chain_and_free_leader(void)
{
    struct Game *g = calloc(1, sizeof(*g));
    struct Action a;

    init_game(g, PHASE_FORTIFY);
    g->territories[0].leader_of = 1;
    g->seats[0].leaders_in_play = 1;
    g->leader_conquered_site = 1;
    deck_top(g, "p_ambush");
    deck_top(g, "e_free");          /* drawn first */
    a = make(ACT_END_TURN);
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->current_player == 0);   /* paused for the free Leader */
    CHECK(g->pending_free_leader == 1);
    a = make(ACT_DONE_ATTACK);
    CHECK(act(g, 0, &a) != 0);       /* nothing else until it is placed */
    a = make(ACT_PLACE_LEADER);
    a.territory = 0;                 /* already has a Leader */
    CHECK(act(g, 0, &a) != 0);
    a.territory = 2;
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->territories[2].leader_of == 1);
    CHECK(g->seats[0].leaders_in_play == 2);
    a = make(ACT_END_TURN);
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->current_player == 1);
    CHECK(g->seats[0].adventure_hand_count == 1);
    CHECK(g->seats[0].adventure_hand[0] == card_id("p_ambush"));
    CHECK(adventure_total(g) == pmap.adventureCardCount);

    /* With 2 Leaders already in play the card is just discarded. */
    init_game(g, PHASE_FORTIFY);
    g->territories[0].leader_of = 1;
    g->territories[1].leader_of = 1;
    g->seats[0].leaders_in_play = 2;
    g->leader_conquered_site = 1;
    deck_top(g, "p_grima");
    deck_top(g, "e_free");
    a = make(ACT_END_TURN);
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->current_player == 1);
    CHECK(g->pending_free_leader == 0);
    free(g);
}

static void end_turn_with_event(struct Game *g, const char *id)
{
    struct Action a = make(ACT_END_TURN);
    g->leader_conquered_site = 1;
    deck_top(g, "p_ambush");
    deck_top(g, id);
    CHECK(act(g, g->current_player, &a) == 0);
}

static void test_events(void)
{
    struct Game *g = calloc(1, sizeof(*g));
    struct Action a;

    /* SIDE_CONTROL: T5 is held by the evil seat -> loses 2. */
    init_game(g, PHASE_FORTIFY);
    g->territories[5].armies = 6;
    end_turn_with_event(g, "e_side");
    CHECK(g->territories[5].armies == 4);
    /* ...held by a good force -> +10. */
    init_game(g, PHASE_FORTIFY);
    g->territories[5].owner = 0;
    end_turn_with_event(g, "e_side");
    CHECK(g->territories[5].armies == 13);

    /* RAIDERS / MUSTERING on T4, T5, T6. */
    init_game(g, PHASE_FORTIFY);
    g->territories[4].armies = 5;
    g->territories[5].armies = 1;
    g->territories[6].armies = 8;
    territory_top(g, 4, 5, 6);
    end_turn_with_event(g, "e_raid");
    CHECK(g->territories[4].armies == 3);   /* 5 -> lose 2 */
    CHECK(g->territories[5].armies == 1);   /* 1 must remain */
    CHECK(g->territories[6].armies == 4);
    init_game(g, PHASE_FORTIFY);
    territory_top(g, 4, 5, 6);
    end_turn_with_event(g, "e_must");
    CHECK(g->territories[4].armies == 6 && g->territories[6].armies == 6);

    /* WINTER_STORMS: no invasions into or out of the drawn territories until
     * the drawer's next turn. */
    init_game(g, PHASE_FORTIFY);
    territory_top(g, 3, 6, 7);
    end_turn_with_event(g, "e_winter");
    CHECK(g->frozen_by[3] == 1 && g->frozen_by[6] == 1);
    g->phase = PHASE_ATTACK;          /* seat 1 now: T4 -> T3 is frozen */
    g->territories[4].armies = 5;
    a = make(ACT_ATTACK);
    a.from = 4;
    a.to = 3;
    a.dice = 1;
    CHECK(act(g, 1, &a) != 0);
    g->phase = PHASE_FORTIFY;
    g->fortified_this_turn = 1;
    g->reinforcements_pending = 0;
    a = make(ACT_END_TURN);
    CHECK(act(g, 1, &a) == 0);        /* seat 0's turn: storms end */
    CHECK(g->frozen_by[3] == 0 && g->frozen_by[6] == 0);

    /* LOCAL_UPRISING: the card stays secret until its territory is held. */
    init_game(g, PHASE_FORTIFY);
    territory_top(g, 4, 5, 6);        /* T4 drawn first */
    end_turn_with_event(g, "e_up");
    CHECK(g->seats[0].uprising_card == 4);
    CHECK(g->territories[4].armies == 3);
    g->territories[4].owner = 0;      /* seat 0 takes T4 */
    g->current_player = 0;
    g->phase = PHASE_REINFORCE;
    g->reinforcements_pending = 0;
    a = make(ACT_DONE_REINFORCE);
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->territories[4].armies == 7);
    CHECK(g->seats[0].uprising_card == -1);

    /* A player who leaves the game takes their lasting effects along (a
     * Winter Storms freeze would otherwise never end). */
    init_game(g, PHASE_REINFORCE);
    g->current_player = 1;
    g->frozen_by[6] = 2;
    g->bridge_blocked_by[0] = 2;
    g->toll_by = 2;
    a = make(ACT_RESIGN);
    CHECK(act(g, 1, &a) == 0);
    CHECK(g->frozen_by[6] == 0 && g->bridge_blocked_by[0] == 0 &&
          g->toll_by == 0);
    free(g);
}

static void attack_once(struct Game *g, int from, int to, int dice)
{
    struct Action a = make(ACT_ATTACK);
    a.from = from;
    a.to = to;
    a.dice = dice;
    CHECK(act(g, g->current_player, &a) == 0);
}

static const struct DiceBattle *last_battle(const struct Game *g)
{
    int i;
    for (i = game_log_count(g) - 1; i >= 0; i--) {
        const struct Event *e = game_log_event(g, i);
        if (e->kind == EVENT_ATTACK && e->battle_count > 0) {
            return &e->battles[e->battle_count - 1];
        }
    }
    return NULL;
}

static void test_powers(void)
{
    struct Game *g = calloc(1, sizeof(*g));
    struct Action a;
    int slot;

    /* AMBUSH: +3 in the invading territory. */
    init_game(g, PHASE_ATTACK);
    slot = give(g, 0, "p_ambush");
    a = make(ACT_PLAY_CARD);
    a.card_a = slot;
    a.from = 3;
    a.to = 2;                          /* own territory: refused */
    CHECK(act(g, 0, &a) != 0);
    a.to = 4;
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->territories[3].armies == 6);

    /* GRIMA: up to 2 defenders change sides, 1 must remain. */
    init_game(g, PHASE_ATTACK);
    g->territories[4].armies = 2;
    slot = give(g, 0, "p_grima");
    a = make(ACT_PLAY_CARD);
    a.card_a = slot;
    a.from = 3;
    a.to = 4;
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->territories[4].armies == 1 && g->territories[3].armies == 4);

    /* BLACK SAILS: +1 on the highest die for the whole seaborne invasion. */
    init_game(g, PHASE_ATTACK);
    g->territories[3].armies = 10;
    g->territories[4].armies = 10;
    slot = give(g, 0, "p_sails");
    a = make(ACT_PLAY_CARD);
    a.card_a = slot;
    a.from = 2;                        /* not a sea-line */
    a.to = 1;
    CHECK(act(g, 0, &a) != 0);
    a.from = 3;
    a.to = 4;
    CHECK(act(g, 0, &a) == 0);
    attack_once(g, 3, 4, 3);
    CHECK(last_battle(g) != NULL && last_battle(g)->attacker_bonus == 1);
    attack_once(g, 3, 4, 2);           /* same invasion: still +1 */
    CHECK(last_battle(g)->attacker_bonus == 1);

    /* SIEGE MACHINES: the stronghold loses its +1 for this invasion. */
    init_game(g, PHASE_ATTACK);
    g->territories[4].owner = 0;
    g->territories[4].armies = 10;
    g->territories[5].armies = 10;
    attack_once(g, 4, 5, 1);
    CHECK(last_battle(g)->defender_bonus == 1);
    slot = give(g, 0, "p_siege");
    a = make(ACT_PLAY_CARD);
    a.card_a = slot;
    a.from = 4;
    a.to = 5;
    CHECK(act(g, 0, &a) == 0);
    attack_once(g, 4, 5, 1);
    CHECK(last_battle(g)->defender_bonus == 0);

    /* THE WAY IS UNDER THE MOUNTAINS: T0 and T6 share a range. */
    init_game(g, PHASE_ATTACK);
    g->territories[0].armies = 6;
    a = make(ACT_ATTACK);
    a.from = 0;
    a.to = 6;
    a.dice = 1;
    CHECK(act(g, 0, &a) != 0);         /* not adjacent */
    slot = give(g, 0, "p_way");
    a = make(ACT_PLAY_CARD);
    a.card_a = slot;
    a.from = 0;
    a.to = 5;                          /* not next to the range */
    CHECK(act(g, 0, &a) != 0);
    a.to = 6;
    CHECK(act(g, 0, &a) == 0);
    attack_once(g, 0, 6, 1);
    CHECK(game_attack_route(g, 0, 0, 6) == 1);

    /* BORNE TO ANOTHER PLACE: move a Leader at the start of the turn. */
    init_game(g, PHASE_REINFORCE);
    g->territories[0].leader_of = 1;
    g->seats[0].leaders_in_play = 1;
    slot = give(g, 0, "p_borne");
    a = make(ACT_PLAY_CARD);
    a.card_a = slot;
    a.from = 0;
    a.to = 4;                          /* not ours */
    CHECK(act(g, 0, &a) != 0);
    a.to = 3;
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->territories[3].leader_of == 1 && g->territories[0].leader_of == 0);

    /* YOU SHALL NOT PASS: battalions cross the bridge only with a Leader. */
    init_game(g, PHASE_REINFORCE);
    g->current_player = 1;
    g->territories[2].owner = 1;
    slot = give(g, 1, "p_pass");
    a = make(ACT_PLAY_CARD);
    a.card_a = slot;
    a.bridge = 0;
    CHECK(act(g, 1, &a) == 0);
    CHECK(g->bridge_blocked_by[0] == 2);
    g->current_player = 0;
    g->phase = PHASE_ATTACK;
    g->territories[1].armies = 5;
    a = make(ACT_ATTACK);
    a.from = 1;
    a.to = 2;
    a.dice = 1;
    CHECK(act(g, 0, &a) != 0);
    g->territories[1].leader_of = 1;   /* accompanied by a Leader */
    CHECK(act(g, 0, &a) == 0);

    /* MOVE BY NIGHT: up to 3 extra redeployments. */
    init_game(g, PHASE_FORTIFY);
    g->fortified_this_turn = 1;
    g->territories[0].armies = 9;
    slot = give(g, 0, "p_night");
    a = make(ACT_PLAY_CARD);
    a.card_a = slot;
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->extra_fortifies == 3);
    a = make(ACT_FORTIFY);
    a.from = 0;
    a.to = 1;
    a.count = 1;
    CHECK(act(g, 0, &a) == 0);
    CHECK(act(g, 0, &a) == 0);
    CHECK(act(g, 0, &a) == 0);
    CHECK(act(g, 0, &a) != 0);         /* three extra, no more */
    free(g);
}

static void test_defender_cards(void)
{
    struct Game *g = calloc(1, sizeof(*g));
    int slot;

    /* COURAGE ALONE: +4 in the invaded territory (standing order "always"). */
    init_game(g, PHASE_ATTACK);
    g->territories[3].armies = 10;
    slot = give(g, 1, "p_courage");
    g->seats[1].adventure_auto[slot] = 2;
    g->seats[1].adventure_auto_n[slot] = 1;
    attack_once(g, 3, 4, 1);
    CHECK(g->seats[1].adventure_hand_count == 0);
    CHECK(g->territories[4].armies >= 5);   /* 3 + 4, at most 2 lost */

    /* STRATEGIC WITHDRAWAL: all but 1 move to an adjacent own territory. */
    init_game(g, PHASE_ATTACK);
    g->territories[3].armies = 10;
    g->territories[4].armies = 6;
    slot = give(g, 1, "p_withdraw");
    g->seats[1].adventure_auto[slot] = 2;
    g->seats[1].adventure_auto_n[slot] = 1;
    attack_once(g, 3, 4, 1);
    CHECK(g->territories[5].armies == 8);   /* 3 + 5 withdrawn */
    CHECK(g->territories[4].armies <= 1 || g->territories[4].owner == 0);

    /* VIOLENT STORMS: a seaborne battle on a port loses every attacker. */
    init_game(g, PHASE_ATTACK);
    g->territories[3].armies = 10;
    slot = give(g, 1, "p_storm");
    g->seats[1].adventure_auto[slot] = 2;
    g->seats[1].adventure_auto_n[slot] = 1;
    attack_once(g, 3, 4, 3);
    CHECK(g->territories[3].armies == 7);
    CHECK(g->territories[4].armies == 3);
    CHECK(last_battle(g)->attacker_losses == 3);

    /* CORSAIRS OF UMBAR: seaborne invasions lose 3 first. */
    init_game(g, PHASE_ATTACK);
    g->toll_by = 2;
    g->toll_loss = 3;
    g->territories[3].armies = 8;
    g->territories[4].armies = 20;
    attack_once(g, 3, 4, 1);
    CHECK(g->territories[3].armies <= 5);
    init_game(g, PHASE_ATTACK);
    g->toll_by = 2;
    g->toll_loss = 3;
    g->territories[3].armies = 4;     /* toll leaves 1: the invasion stops */
    attack_once(g, 3, 4, 1);
    CHECK(g->territories[3].armies == 1 && g->territories[4].armies == 3);
    free(g);
}

static void test_missions(void)
{
    struct Game *g = calloc(1, sizeof(*g));
    struct Action a;
    int slot;

    /* Region reward: 2 battalions (good side) anywhere in North. */
    init_game(g, PHASE_ATTACK);
    slot = give(g, 0, "m_region");
    a = make(ACT_CLAIM_MISSION);
    a.card_a = slot;
    CHECK(act(g, 0, &a) != 0);         /* no Leader on T1 yet */
    g->territories[1].leader_of = 1;
    g->seats[0].leaders_in_play = 1;
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->bonus_pending == 2 && g->seats[0].missions_done == 1);
    a = make(ACT_ATTACK);
    a.from = 3;
    a.to = 4;
    a.dice = 1;
    CHECK(act(g, 0, &a) != 0);         /* place the battalions first */
    a = make(ACT_PLACE);
    a.territory = 5;                   /* South: refused */
    a.count = 2;
    CHECK(act(g, 0, &a) != 0);
    a.territory = 0;
    a.count = 1;
    CHECK(act(g, 0, &a) == 0);
    a.territory = 2;
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->bonus_pending == 0);
    CHECK(g->territories[0].armies == 4 && g->territories[2].armies == 4);

    /* Port reward: all in one port. */
    init_game(g, PHASE_ATTACK);
    g->territories[1].leader_of = 1;
    slot = give(g, 0, "m_port");
    a = make(ACT_CLAIM_MISSION);
    a.card_a = slot;
    CHECK(act(g, 0, &a) == 0);
    a = make(ACT_PLACE);
    a.territory = 3;
    a.count = 1;
    CHECK(act(g, 0, &a) != 0);         /* must be all 2 at once */
    a.count = 2;
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->territories[3].armies == 5);

    /* Card reward: an extra territory card at the end of the turn. */
    init_game(g, PHASE_FORTIFY);
    g->territories[1].leader_of = 1;
    slot = give(g, 0, "m_card");
    a = make(ACT_CLAIM_MISSION);
    a.card_a = slot;
    CHECK(act(g, 0, &a) == 0);
    a = make(ACT_END_TURN);
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->seats[0].hand_count == 1);

    /* Erech: 6 Oathbreakers join an invasion later. */
    init_game(g, PHASE_ATTACK);
    g->territories[1].leader_of = 1;
    slot = give(g, 0, "m_oath");
    a = make(ACT_CLAIM_MISSION);
    a.card_a = slot;
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->seats[0].oathbreakers == 6);
    a = make(ACT_OATHBREAKERS);
    a.territory = 0;                   /* cannot invade from T0 */
    CHECK(act(g, 0, &a) != 0);
    a.territory = 3;
    CHECK(act(g, 0, &a) == 0);
    CHECK(g->territories[3].armies == 9 && g->seats[0].oathbreakers == 0);
    free(g);
}

static void test_round_trip(void)
{
    struct Game *g = calloc(1, sizeof(*g));
    struct Game *loaded = calloc(1, sizeof(*loaded));
    char *json = malloc(1 << 20);
    char *again = malloc(1 << 20);
    char error[256];

    init_game(g, PHASE_ATTACK);
    g->territories[1].leader_of = 1;
    g->seats[0].leaders_in_play = 1;
    give(g, 0, "p_ambush");
    g->seats[1].uprising_card = 6;
    g->seats[1].uprising_bonus = 4;
    {
        int i;                         /* the uprising card leaves the deck */
        for (i = 0; i < g->deck_count; i++) {
            if (g->deck[i] == 6) {
                g->deck[i] = g->deck[--g->deck_count];
                break;
            }
        }
    }
    g->seats[0].oathbreakers = 6;
    g->frozen_by[2] = 2;
    g->bridge_blocked_by[0] = 2;
    g->toll_by = 2;
    g->toll_loss = 3;
    g->inv_from = 3;
    g->inv_to = 4;
    g->inv_flags = INV_NO_STRONGHOLD;
    CHECK(game_to_json(g, json, 1 << 20) == 0);
    CHECK(game_from_json(json, loaded, &pmap, error, sizeof(error)) == 0);
    if (error[0] != '\0') {
        fprintf(stderr, "printed round trip: %s\n", error);
    }
    CHECK(loaded->seats[1].uprising_card == 6);
    CHECK(loaded->seats[0].oathbreakers == 6);
    CHECK(loaded->frozen_by[2] == 2 && loaded->bridge_blocked_by[0] == 2);
    CHECK(loaded->inv_from == 3 && loaded->inv_flags == INV_NO_STRONGHOLD);
    CHECK(game_to_json(loaded, again, 1 << 20) == 0);
    CHECK(strcmp(json, again) == 0);
    free(json);
    free(again);
    free(loaded);
    free(g);
}

int test_printed_suite(void)
{
    failures = 0;
    checks = 0;
    build_map();
    test_start_deal();
    test_draw_chain_and_free_leader();
    test_events();
    test_powers();
    test_defender_cards();
    test_missions();
    test_round_trip();
    printf("%s: %d printed-card checks, %d failures\n",
           failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures;
}
