/*
 * Conditioning: hash health-checked raw bytes before they reach the dice.
 *
 * Each 32-byte output block is SHA-256(raw || extra), where raw is
 * CONDITION_RAW_BYTES fresh bytes from the (health-checked) source and extra
 * is optional caller data, such as a cycle count taken at the button press.
 * No state carries over between blocks: every block comes from fresh raw
 * bytes, so a subtle flaw in the raw output (one the health tests can't
 * see) is not passed straight through to the passphrase.
 *
 * Ratio 4:1. With the 4 bits/byte min-entropy floor claimed in health.h,
 * 128 raw bytes carry at least 512 bits, above the 256 + 64 bits that
 * NIST SP 800-90B asks of a vetted conditioner for full-entropy output.
 *
 * A failure (raw source or hash) latches: every later read fails.
 */
#pragma once

#include "rng.h"

#define CONDITION_RAW_BYTES 128u
#define CONDITION_OUT_BYTES 32u
#define CONDITION_EXTRA_MAX 16u

/* One-shot SHA-256. Return false on failure. */
typedef bool (*sha256_fn)(const uint8_t *data, size_t len,
                          uint8_t out[CONDITION_OUT_BYTES]);

typedef struct {
    rng_t     raw;
    sha256_fn sha256;
    uint8_t   block[CONDITION_OUT_BYTES];
    size_t    avail;      /* unread bytes at the end of block */
    uint8_t   extra[CONDITION_EXTRA_MAX];
    size_t    extra_len;
    bool      failed;
} conditioned_rng_t;

void conditioned_rng_init(conditioned_rng_t *c, rng_t raw, sha256_fn sha256);

/* Mix `data` into every block hashed from now until the next wipe.
 * Anything past CONDITION_EXTRA_MAX bytes is ignored. */
void conditioned_rng_set_extra(conditioned_rng_t *c, const void *data, size_t len);

/* Discard buffered output and extra data, so nothing left over from one
 * passphrase is used for (or reveals) the next. */
void conditioned_rng_wipe(conditioned_rng_t *c);

/* rng_t view of the conditioned source. */
rng_t conditioned_rng_as_rng(conditioned_rng_t *c);
