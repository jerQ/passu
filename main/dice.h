/**
 * @file dice.h
 * @brief Convert uniform random bytes into bounded integer choices.
 *
 * byte % n is biased whenever 256 is not a multiple of n, so bytes at or
 * above the largest multiple of n are discarded and redrawn:
 *   d6:  accept 0..251 (252 = 6 * 42),  reject rate ~1.6 %
 *   d10: accept 0..249 (250 = 10 * 25), reject rate ~2.3 %
 */
#pragma once

#include "rng.h"

/* Bound execution time even if a source repeatedly supplies rejected bytes.
 * For independent uniform bytes and the d6/digit ranges used here, reaching
 * this bound has probability below 2^-190. Other n values have different odds. */
#define DICE_MAX_TRIES 64u

/**
 * Write a uniform integer in [0, n), assuming uniform input bytes.
 * rng must reference a valid provider and out must point to one writable byte.
 * n must be 2..256; n == 256 permits every byte without rejection.
 * Returns false for invalid n, source failure, or DICE_MAX_TRIES rejections.
 * out is assigned only on success; callers must not use it after failure.
 */
bool dice_uniform(const rng_t *rng, unsigned n, uint8_t *out);

/** Roll a six-sided die (1..6); pointer and failure rules match dice_uniform(). */
bool dice_d6(const rng_t *rng, uint8_t *out);

/** Select a decimal digit (0..9); failure rules match dice_uniform(). */
bool dice_digit(const rng_t *rng, uint8_t *out);
