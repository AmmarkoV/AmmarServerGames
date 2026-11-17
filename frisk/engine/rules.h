#ifndef FRISK_RULES_H
#define FRISK_RULES_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "dice.h"
#include "map.h"

#define GAME_MAX_PLAYERS 4
#define GAME_MAX_TERRITORIES MAP_MAX_TERRITORIES
#define GAME_MAX_CARDS MAP_MAX_CARDS
#define GAME_ADVENTURE_HAND_MAX 32
#define GAME_ADVENTURE_HAND_LIMIT 4
#define GAME_SITE_MASK_WORDS 4
#define GAME_LOG_RING 64
#define GAME_MAX_BLITZ_ROUNDS 32
#define GAME_MAX_EVENTS_PER_ACTION 24
#define GAME_NAME_SIZE 64
#define GAME_USERNAME_SIZE 64
#define GAME_PASSWORD_HASH_SIZE 128

enum GameStatus {
    GAME_OPEN = 0,
    GAME_RUNNING,
    GAME_FINISHED
};

enum GamePhase {
    PHASE_LOBBY = 0,
    PHASE_SETUP_CLAIM,
    PHASE_SETUP_PLACE,
    PHASE_SETUP_LEADERS,
    PHASE_REINFORCE,
    PHASE_ATTACK,
    PHASE_CONQUEST_MOVE,
    PHASE_FORTIFY,
    PHASE_DRAW,
    PHASE_FINISHED
};

enum StartMode {
    GAME_START_DEAL = 0,
    GAME_START_DRAFT
};

enum ActionType {
    ACT_NONE = 0,
    ACT_CLAIM,
    ACT_PLACE,
    ACT_PLACE_LEADER,
    ACT_TRADE_CARDS,
    ACT_DONE_REINFORCE,
    ACT_ATTACK,
    ACT_MOVE_IN,
    ACT_DONE_ATTACK,
    ACT_FORTIFY,
    ACT_END_TURN,
    ACT_RESIGN,
    ACT_KICK,
    ACT_RECRUIT_LEADER,
    ACT_PLAY_CARD,
    ACT_DISCARD_ADVENTURE,
    ACT_CLAIM_MISSION,      /* card_a = hand slot of a site mission */
    ACT_OATHBREAKERS        /* territory = the territory invading from */
};

/* Printed-card invasion state (PLAN.md 10b.4).  An invasion is a run of
 * attacks from one territory into one territory. */
#define INV_NO_STRONGHOLD 1     /* Siege Machines / The Enemy Is at Hand */
#define INV_MOUNTAIN      2     /* The Way Is Under the Mountains */
#define INV_STARTED       4     /* declaration effects already applied */
#define INV_STORM_DONE    8     /* Violent Storms already struck */

/* Mission reward placement areas. */
enum BonusArea {
    BONUS_NONE = 0,
    BONUS_ANY,          /* all into one territory you control */
    BONUS_REGION,       /* anywhere in a region (any split) */
    BONUS_TERRITORY,    /* into one named territory */
    BONUS_PORT          /* all into one port you control */
};

enum EventKind {
    EVENT_NONE = 0,
    EVENT_START,
    EVENT_CLAIM,
    EVENT_PLACE,
    EVENT_PLACE_LEADER,
    EVENT_TRADE_CARDS,
    EVENT_ATTACK,
    EVENT_CONQUEST,
    EVENT_MOVE_IN,
    EVENT_FORTIFY,
    EVENT_DRAW_CARD,
    EVENT_END_TURN,
    EVENT_ELIMINATED,
    EVENT_GAME_OVER,
    EVENT_RESIGN,
    EVENT_SETUP_DONE,
    EVENT_TURN_BEGIN,
    EVENT_PHASE_CHANGE,
    EVENT_KICK,
    EVENT_ADVENTURE,
    EVENT_CARD_PLAYED,
    EVENT_MISSION_COMPLETE,
    EVENT_SEA_CLOSED
};

struct PlayerStats {
    int games_played;
    int wins;
    int losses;
    int territories_taken;
    int armies_killed;
    int armies_lost;
};

struct Seat {
    char username[GAME_USERNAME_SIZE];
    int faction;
    int alive;
    int hand[GAME_MAX_CARDS];
    int hand_count;
    int adventure_hand[GAME_ADVENTURE_HAND_MAX];
    int adventure_hand_count;
    unsigned char adventure_auto[GAME_ADVENTURE_HAND_MAX];
    unsigned char adventure_auto_n[GAME_ADVENTURE_HAND_MAX];
    int leaders_in_play;
    int missions_done;
    int attack_bonus;            /* +N to the next attack's highest die */
    unsigned int site_claimed[GAME_SITE_MASK_WORDS];
    struct PlayerStats stats;
    int last_seen_version;
    int uprising_card;           /* Local Uprising territory card or -1 */
    int uprising_bonus;
    int oathbreakers;            /* Erech: battalions waiting on the card */
    int extra_card_draws;        /* missions: extra territory cards at end */
};

struct TerritoryState {
    int owner;      /* seat index, or -1 for neutral/unclaimed */
    int armies;
    int leader_of;  /* 0 = no leader, otherwise seat index + 1 */
    int neutral;    /* 1 for the passive neutral faction */
};

struct Action {
    int type;
    int territory;
    int from;
    int to;
    int count;
    int dice;
    int repeat;
    int stop_at;
    int leader;
    int card_a;
    int card_b;
    int card_c;
    int bridge;                  /* You Shall Not Pass: bridge index */
};

struct DiceBattle {
    int attacker[3];
    int attacker_count;
    int defender[2];
    int defender_count;
    int attacker_bonus;
    int defender_bonus;
    int attacker_losses;
    int defender_losses;
};

struct Event {
    int kind;
    int version;   /* game version this event was appended at, for replay */
    int player;
    int from;
    int to;
    int count;
    int card_a;
    int card_b;
    int card_c;
    int set_bonus;
    int battle_count;
    int truncated;
    struct DiceBattle battles[GAME_MAX_BLITZ_ROUNDS];
    char text[192];
};

struct GameLog {
    struct Event events[GAME_LOG_RING];
    int count;   /* events currently in the ring */
    int next;    /* next write slot */
};

struct ReinforcementInfo {
    int territories;
    int base;
    int region_bonus;
    int stronghold_bonus;
    int card_bonus;
    int total;
    int regions_held;
    int strongholds_held;
};

/* End-of-game hook installed by the server (PLAN.md 5.13) to update profiles.
 * It runs inside the action that finishes the game, before game_apply returns. */
struct Game;
typedef void (*GameEndHook)(struct Game *game, int winner, void *user);

struct Game {
    int id;
    char name[GAME_NAME_SIZE];
    char map_pack[MAP_NAME_SIZE];
    char password_hash[GAME_PASSWORD_HASH_SIZE];
    char host[GAME_USERNAME_SIZE];
    int status;
    int version;
    int max_players;
    int player_count;
    struct Seat seats[GAME_MAX_PLAYERS];
    int territory_count;
    struct TerritoryState territories[GAME_MAX_TERRITORIES];
    int deck[GAME_MAX_CARDS];
    int deck_count;
    int discard[GAME_MAX_CARDS];
    int discard_count;
    int adventure_deck[GAME_MAX_CARDS];
    int adventure_deck_count;
    int adventure_discard[GAME_MAX_CARDS];
    int adventure_discard_count;
    int phase;
    int current_player;
    int turn;
    int winner;                  /* seat index or -1 */
    int conquered_this_turn;
    int cards_traded_this_turn;
    int fortified_this_turn;
    int must_trade;
    int must_discard_adventure;
    int extra_fortifies;
    int territories_conquered_this_turn;
    int link_closed_by[MAP_MAX_LINKS];
    int kick_target;             /* seat with an active inactivity vote, or -1 */
    unsigned int kick_votes;     /* bit mask: seats that voted for the kick */
    time_t kick_started;
    int pending_from;
    int pending_to;
    int pending_min;
    int pending_max;
    int reinforcements_pending;
    int setup_pool[GAME_MAX_PLAYERS];
    int setup_batch_remaining;
    int setup_batch_size;
    int start_mode;
    int neutral_enabled;
    int deferred_card_bonus[GAME_MAX_PLAYERS];
    struct ReinforcementInfo reinforce_info;
    time_t created;
    time_t updated;
    /* printed adventure cards (PLAN.md 10b) */
    int leader_conquered_site;   /* a leader conquered a site this turn */
    int draw_pending;            /* end-of-turn adventure draw in progress */
    int pending_free_leader;     /* Appoint a Second Leader awaits a choice */
    int bonus_pending;           /* mission reward still to place */
    int bonus_area;              /* enum BonusArea */
    int bonus_area_id;           /* region or territory id */
    int inv_from;
    int inv_to;
    int inv_flags;               /* INV_* */
    int inv_attack_bonus;        /* Black Sails */
    int toll_by;                 /* Corsairs of Umbar: drawer seat + 1 */
    int toll_loss;
    int frozen_by[GAME_MAX_TERRITORIES];   /* Winter Storms: seat + 1 */
    int bridge_blocked_by[MAP_MAX_BRIDGES]; /* You Shall Not Pass: seat + 1 */
    struct GameLog log;
    struct DiceRng rng;
    const struct Map *map;       /* not serialised; the server resolves the pack */
    GameEndHook end_hook;        /* optional; not serialised */
    void *end_hook_user;
};

struct LegalActions {
    int phase;
    int current_player;
    int setup_batch_remaining;
    int setup_pool;
    int leaders_remaining;
    int reinforcements_pending;
    int move_in_min;
    int move_in_max;
    int must_discard_adventure;
    int can_recruit_leader;
    int recruit_cost;
    int has_recruit_card;
    int can_trade;
    int can_done_reinforce;
    int can_done_attack;
    int can_end_turn;
    int can_resign;
    int claimable[GAME_MAX_TERRITORIES];
    int placeable[GAME_MAX_TERRITORIES];
    int leader_placeable[GAME_MAX_TERRITORIES];
    int recruit_placeable[GAME_MAX_TERRITORIES];
    int playable_adventure[GAME_ADVENTURE_HAND_MAX];
    int claimable_mission[GAME_ADVENTURE_HAND_MAX];
    int free_leader_pending;
    int bonus_pending;
    int bonus_placeable[GAME_MAX_TERRITORIES];
    int can_oathbreakers;
    int max_attack_dice[GAME_MAX_TERRITORIES];
    unsigned char attack[GAME_MAX_TERRITORIES][GAME_MAX_TERRITORIES];
    unsigned char fortify[GAME_MAX_TERRITORIES][GAME_MAX_TERRITORIES];
};

/* Lobby / setup.  game_init creates an OPEN game in PHASE_LOBBY; game_join
 * adds a seat; game_start shuffles seats and deals the board.  A zero seed
 * asks game_init to seed from the operating system. */
void game_init(struct Game *g, const struct Map *map, int id,
               const char *name, const char *host, uint64_t seed);
int game_join(struct Game *g, const char *username, int faction,
              char *err, size_t err_size);
int game_start(struct Game *g, int start_mode, int placement_batch,
               char *err, size_t err_size);

/* The single mutation entry point for accepted gameplay.  `out` must have
 * room for GAME_MAX_EVENTS_PER_ACTION events (it may be NULL).  Rejected
 * actions leave the Game untouched. */
int game_apply(struct Game *g, int player, const struct Action *action,
               struct Event *out, int *out_count,
               char *err, size_t err_size);

/* Enumerate the legal action surface for the current player. */
void game_legal_actions(const struct Game *g, struct LegalActions *out);

/* Itemised reinforcement count (PLAN.md 5.4). */
int game_reinforcements(const struct Game *g, int player,
                        struct ReinforcementInfo *out);

int game_find_seat(const struct Game *g, const char *username);
int game_count_territories(const struct Game *g, int player);
int game_can_fortify(const struct Game *g, int player, int from, int to);
int game_find_card_set(const struct Game *g, int player,
                       int *card_a, int *card_b, int *card_c);
/* Rebuild the adventure deck from the map (and deal starting hands when the
 * pack asks for them) for a game saved with an older deck; only valid while
 * no adventure card is held or played. */
void game_rebuild_adventure_deck(struct Game *g);
int game_attack_route(const struct Game *g, int player, int from, int to);

const char *game_phase_name(int phase);
const char *game_status_name(int status);
const char *game_event_name(int kind);

/* Chronological access to the in-memory log ring (0 = oldest stored). */
int game_log_count(const struct Game *g);
const struct Event *game_log_event(const struct Game *g, int index);

/* Bounded JSON view; the only function the HTTP layer uses to serialise game
 * state.  Returns 0 on success, -1 if the buffer is too small. */
int game_view(const struct Game *g, int viewer_seat, char *out, size_t out_size);

#endif
