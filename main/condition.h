/**
 * @file condition.h
 * @brief SHA-256 conditioner between a checked byte source and dice selection.
 *
 * Each 32-byte output block is SHA-256(raw || extra), where raw is
 * CONDITION_RAW_BYTES fresh bytes from the (health-checked) source and extra
 * is optional caller data, such as a cycle count taken at the button press.
 * No state carries over between blocks: every block comes from fresh raw
 * bytes, so a subtle flaw in the raw output (one the health tests can't
 * see) is not passed straight through to the passphrase.
 *
 * Ratio 4:1. If the source supplies at least 4 bits of conditional
 * min-entropy per byte, 128 bytes provide at least 512 bits of input
 * entropy. This is an assumption about the source, including dependencies
 * between samples; passing the health tests does not establish it.
 * Hashing cannot create missing entropy or guarantee full-entropy output.
 *
 * A failure (raw source or hash) latches: every later read fails.
 */
#pragma once

#include "rng.h"

#define CONDITION_RAW_BYTES 128u
#define CONDITION_OUT_BYTES 32u
#define CONDITION_EXTRA_MAX 16u

/**
 * Synchronously hash len input bytes into exactly CONDITION_OUT_BYTES bytes.
 * Return false on any failure; the conditioner then erases its output block.
 * The callback must not retain data or out after returning.
 */
typedef bool (*sha256_fn)(const uint8_t *data, size_t len,
                          uint8_t out[CONDITION_OUT_BYTES]);

/**
 * Caller-owned mutable conditioner; initialize before use and serialize reads.
 * rng_t views borrow this object. raw.ctx remains owned by the upstream source.
 * Unread bytes occupy the tail of block; consumed bytes are erased in place.
 */
typedef struct {
    rng_t     raw;
    sha256_fn sha256;
    uint8_t   block[CONDITION_OUT_BYTES];
    size_t    avail;      /* unread bytes at the end of block */
    uint8_t   extra[CONDITION_EXTRA_MAX];
    size_t    extra_len;
    bool      failed;     /* Sticky until conditioned_rng_init(), not wipe(). */
} conditioned_rng_t;

/**
 * Initialize c with a borrowed raw provider and synchronous SHA-256 callback.
 * c, raw.read, and sha256 must be valid; dependencies must outlive c's use.
 * Clears buffered state and the failure latch; does not initialize raw itself.
 */
void conditioned_rng_init(conditioned_rng_t *c, rng_t raw, sha256_fn sha256);

/**
 * Copy up to CONDITION_EXTRA_MAX bytes from data into c's extra input.
 * data must address at least the copied length and must not overlap c.
 * No ownership is transferred.
 * New extra input is appended on future refills until replaced or wiped;
 * an already buffered block is not recomputed or discarded by this call.
 * Extra input need not be secret, and no entropy is claimed for it.
 */
void conditioned_rng_set_extra(conditioned_rng_t *c, const void *data, size_t len);

/**
 * Erase buffered output and extra input after a passphrase attempt.
 * Keeps raw, sha256, and the sticky failure flag; this is not failure recovery.
 * The next successful read must obtain a fresh block from the upstream source.
 */
void conditioned_rng_wipe(conditioned_rng_t *c);

/**
 * Return a borrowed rng_t view of initialized c, without allocating or copying
 * state. Reads may span several hash blocks. On any failure the entire caller
 * output range is erased, including bytes copied before that failure.
 */
rng_t conditioned_rng_as_rng(conditioned_rng_t *c);
