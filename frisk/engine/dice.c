#include "dice.h"

#include <errno.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

static uint64_t next_u64(struct DiceRng *rng)
{
    uint64_t x = rng->state;

    /* xorshift64* */
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng->state = x;
    return x * UINT64_C(2685821657736338717);
}

void dice_seed(struct DiceRng *rng, uint64_t seed)
{
    if (rng == NULL) {
        return;
    }
    rng->state = (seed == 0) ? UINT64_C(0x9e3779b97f4a7c15) : seed;
}

void dice_seed_system(struct DiceRng *rng)
{
    uint64_t seed = 0;
    int ok = 0;

    if (rng == NULL) {
        return;
    }
#if defined(__linux__)
    if (getrandom(&seed, sizeof(seed), 0) == (ssize_t)sizeof(seed)) {
        ok = 1;
    }
#endif
    if (!ok) {
        seed = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32) ^
               (uint64_t)(uintptr_t)rng;
    }
    dice_seed(rng, seed);
}

int dice_roll(struct DiceRng *rng)
{
    return dice_range(rng, 6) + 1;
}

int dice_range(struct DiceRng *rng, int count)
{
    uint64_t value;
    uint64_t limit;
    uint64_t modulus;

    if (rng == NULL || count <= 0) {
        return 0;
    }
    modulus = (uint64_t)count;
    /* Reject the tail that is not a whole multiple of count. */
    limit = UINT64_MAX - (UINT64_MAX % modulus);
    do {
        value = next_u64(rng);
    } while (value >= limit);
    return (int)(value % modulus);
}
