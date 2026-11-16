/**
 * @file dice.c
 * @brief Produce die rolls and digits without modulo bias.
 *
 * Rejection sampling removes the excess values before applying modulo. For
 * example, d6 rejects 252..255 so each face has 42 possible input bytes.
 * The retry cap bounds runtime for an unhealthy source. This layer reports
 * failure but does not latch it; the health/conditioning layers do that.
 */
#include "dice.h"

bool dice_uniform(const rng_t *rng, unsigned n, uint8_t *out)
{
    if (n < 2 || n > 256) {
        return false;
    }
    /* Keep the arithmetic unsigned and wider than a byte: n == 256 needs a
     * limit of 256, so every possible byte is accepted. The accepted interval
     * contains exactly the same number of representatives for each residue. */
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

/* Reuse the zero-based sampler and shift only the accepted result to 1..6. */
bool dice_d6(const rng_t *rng, uint8_t *out)
{
    uint8_t v;
    if (!dice_uniform(rng, 6, &v)) {
        return false;
    }
    *out = (uint8_t)(v + 1);
    return true;
}

/* Decimal digits use the same rejection rule with a different bound. */
bool dice_digit(const rng_t *rng, uint8_t *out)
{
    return dice_uniform(rng, 10, out);
}
