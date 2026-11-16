/**
 * @file rng.h
 * @brief Common byte-source interface used by the portable generation pipeline.
 *
 * Everything above this interface (health tests, dice, passphrase builder)
 * is platform independent, so it can be unit tested on a host with a fake
 * source and run on the ESP32 with the radio-backed hardware RNG.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Fill exactly len bytes, returning true only when the complete read succeeds.
 * ctx points to provider-specific state; out must have room for len bytes.
 * On false, none of the requested output may be used, even if some bytes were
 * written. Erasing failed output is a provider policy, not an interface promise.
 * Callers must abort generation rather than fall back to a weaker source.
 */
typedef bool (*rng_read_fn)(void *ctx, uint8_t *out, size_t len);

/** Borrowed callback/state pair; copying this struct does not copy its state. */
typedef struct {
    rng_read_fn read;  /* Must be non-null; invoked synchronously by rng_read(). */
    void *ctx;        /* May be null if the provider does not need state. */
} rng_t;

/**
 * Dispatch a read without adding validation, locking, buffering, or ownership.
 * rng and its callback must be valid. The context must outlive every use of
 * this view. Serialize access when the underlying provider mutates state.
 */
static inline bool rng_read(const rng_t *rng, uint8_t *out, size_t len)
{
    return rng->read(rng->ctx, out, len);
}

/**
 * Overwrite len writable bytes at p using volatile stores (passphrase.c).
 * A zero length performs no access. This clears the specified memory only;
 * it cannot erase copies in registers, library buffers, or serial terminals.
 */
void secure_zero(void *p, size_t len);
