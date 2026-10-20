/*
 * Minimal random-byte source abstraction.
 *
 * Everything above this interface (health tests, dice, passphrase builder)
 * is platform independent, so it can be unit tested on a host with a fake
 * source and run on the ESP32 with the radio-backed hardware RNG.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fill `out` with `len` random bytes. Return false if the source cannot
 * deliver trustworthy output; callers must then abort, never retry with a
 * weaker source. */
typedef bool (*rng_read_fn)(void *ctx, uint8_t *out, size_t len);

typedef struct {
    rng_read_fn read;
    void *ctx;
} rng_t;

static inline bool rng_read(const rng_t *rng, uint8_t *out, size_t len)
{
    return rng->read(rng->ctx, out, len);
}

/* Overwrite memory in a way the compiler cannot optimise away. */
void secure_zero(void *p, size_t len);
