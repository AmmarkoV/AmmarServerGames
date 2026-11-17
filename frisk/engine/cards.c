#include "cards.h"

#include <string.h>

const char *const frisk_effect_ids[FRISK_EFFECT_COUNT] = {
    "HOLD_TERRITORIES",
    "HOLD_REGION",
    "CONQUER_N_THIS_TURN",
    "LEADER_REACH",
    "HOLD_STRONGHOLDS",
    "REGION_ATTRITION",
    "REINFORCE_ALL",
    "LOSE_LEADER_UNLESS",
    "SEA_CLOSED",
    "ATTACK_BONUS",
    "DEFENSE_BONUS",
    "REROLL",
    "EXTRA_REINFORCE",
    "FREE_FORTIFY",
    "RECRUIT_LEADER",
    "CANCEL_EVENT",
    /* printed LOTR cards (PLAN.md 10b) */
    "FREE_LEADER",
    "SIDE_CONTROL",
    "SEABORNE_TOLL",
    "LOCAL_UPRISING",
    "RAIDERS",
    "MUSTERING",
    "WINTER_STORMS",
    "AMBUSH",
    "SEABORNE_BONUS",
    "LEADER_MOVE",
    "INVADED_REINFORCE",
    "GRIMA",
    "EXTRA_REDEPLOY",
    "NO_STRONGHOLD",
    "WITHDRAW",
    "MOUNTAIN_PASS",
    "STORM_REPEL",
    "BRIDGE_BLOCK",
    "SITE_MISSION",
};

int cards_effect_exists(const char *id)
{
    int i;

    if (id == NULL) {
        return 0;
    }
    for (i = 0; i < FRISK_EFFECT_COUNT; i++) {
        if (strcmp(frisk_effect_ids[i], id) == 0) {
            return 1;
        }
    }
    return 0;
}
