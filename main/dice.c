#include "dice.h"

bool dice_uniform(const rng_t *rng, unsigned n, uint8_t *out)
{
    if (n < 2 || n > 256) {
        return false;
    }
    const unsigned limit = 256u - (256u % n); /* largest multiple of n <= 256 */

    for (unsigned tries = 0; tries < DICE_MAX_TRIES; tries++) {
        uint8_t b;
        if (!rng_read(rng, &b, 1)) {
            return false;
        }
        if (b < limit) {
            *out = (uint8_t)(b % n);
            secure_zero(&b, sizeof(b));
            return true;
        }
    }
    return false;
}

bool dice_d6(const rng_t *rng, uint8_t *out)
{
    uint8_t v;
    if (!dice_uniform(rng, 6, &v)) {
        return false;
    }
    *out = (uint8_t)(v + 1);
    return true;
}

bool dice_digit(const rng_t *rng, uint8_t *out)
{
    return dice_uniform(rng, 10, out);
}
