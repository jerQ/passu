/*
 * Unbiased small-range random numbers using rejection sampling.
 *
 * byte % n is biased whenever 256 is not a multiple of n, so bytes at or
 * above the largest multiple of n are discarded and redrawn:
 *   d6:  accept 0..251 (252 = 6 * 42),  reject rate ~1.6 %
 *   d10: accept 0..249 (250 = 10 * 25), reject rate ~2.3 %
 */
#pragma once

#include "rng.h"

/* Give up after this many rejected draws. With a healthy source the chance
 * of reaching it is below 2^-190, so hitting it means the source is broken. */
#define DICE_MAX_TRIES 64u

/* Uniform value in [0, n), 2 <= n <= 256. Returns false on source failure. */
bool dice_uniform(const rng_t *rng, unsigned n, uint8_t *out);

/* A d6 roll, 1..6. */
bool dice_d6(const rng_t *rng, uint8_t *out);

/* A decimal digit, 0..9. */
bool dice_digit(const rng_t *rng, uint8_t *out);
