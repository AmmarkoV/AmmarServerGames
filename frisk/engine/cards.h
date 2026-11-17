#ifndef FRISK_CARDS_H
#define FRISK_CARDS_H

/* Effect identifiers understood by the rules engine (PLAN.md 6.4).  The map
 * pack may use any of these in an adventure card's "effect" field. */
#define FRISK_EFFECT_COUNT 16

extern const char *const frisk_effect_ids[FRISK_EFFECT_COUNT];

int cards_effect_exists(const char *id);

#endif
