#ifndef FRISK_DICE_H
#define FRISK_DICE_H

#include <stdint.h>

struct DiceRng {
    uint64_t state;
};

/* Seed the generator.  A zero seed is replaced with a fixed non-zero value. */
void dice_seed(struct DiceRng *rng, uint64_t seed);

/* Seed from the OS where possible (getrandom), falling back to time/pid. */
void dice_seed_system(struct DiceRng *rng);

/* Return a value in 1..6. */
int dice_roll(struct DiceRng *rng);

/* Return a value in 0..count-1 (count must be positive; returns 0 otherwise). */
int dice_range(struct DiceRng *rng, int count);

#endif
