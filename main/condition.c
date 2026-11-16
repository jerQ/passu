/**
 * @file condition.c
 * @brief Buffer SHA-256 output produced from fresh upstream bytes.
 *
 * The generator calls this through rng_t one byte at a time, while the
 * conditioner reads raw input in larger blocks. Used bytes are erased as they
 * are copied out; conditioned_rng_wipe() discards the unread tail at the end
 * of an attempt. No output block is reused as input to a later hash.
 *
 * This module owns no heap memory and performs no locking or platform I/O.
 * See condition.h for callback lifetimes and source-entropy assumptions.
 */
#include "condition.h"

#include <string.h>

/* Resetting the whole object also clears a previous sticky failure. */
void conditioned_rng_init(conditioned_rng_t *c, rng_t raw, sha256_fn sha256)
{
    memset(c, 0, sizeof(*c));
    c->raw = raw;
    c->sha256 = sha256;
}

/* Replace copied supplemental input; buffered output still belongs to the
 * previous refill, so callers wipe between passphrases before setting new data. */
void conditioned_rng_set_extra(conditioned_rng_t *c, const void *data, size_t len)
{
    if (len > CONDITION_EXTRA_MAX) {
        len = CONDITION_EXTRA_MAX;
    }
    secure_zero(c->extra, sizeof(c->extra));
    memcpy(c->extra, data, len);
    c->extra_len = len;
}

/* Preserve configuration and the failure latch while discarding secrets. */
void conditioned_rng_wipe(conditioned_rng_t *c)
{
    secure_zero(c->block, sizeof(c->block));
    secure_zero(c->extra, sizeof(c->extra));
    c->avail = 0;
    c->extra_len = 0;
}

/* Acquire a complete raw block before hashing. Always erase the stack input,
 * including partially written input on source failure. A failed hash may have
 * touched c->block, so failure also wipes the entire cached output. */
static bool refill(conditioned_rng_t *c)
{
    uint8_t in[CONDITION_RAW_BYTES + CONDITION_EXTRA_MAX];

    bool ok = rng_read(&c->raw, in, CONDITION_RAW_BYTES);
    if (ok) {
        memcpy(in + CONDITION_RAW_BYTES, c->extra, c->extra_len);
        ok = c->sha256(in, CONDITION_RAW_BYTES + c->extra_len, c->block);
    }
    secure_zero(in, sizeof(in));
    if (!ok) {
        c->failed = true;
        conditioned_rng_wipe(c);
        return false;
    }
    c->avail = CONDITION_OUT_BYTES;
    return true;
}

/* rng_t adapter: satisfy arbitrary byte counts from 32-byte blocks. If a later
 * refill fails, erase the entire requested output, not just its remaining tail. */
static bool conditioned_read(void *ctx, uint8_t *out, size_t len)
{
    conditioned_rng_t *c = ctx;

    if (c->failed) {
        secure_zero(out, len);
        return false;
    }
    for (size_t done = 0; done < len;) {
        if (c->avail == 0 && !refill(c)) {
            secure_zero(out, len);
            return false;
        }
        uint8_t *src = c->block + (CONDITION_OUT_BYTES - c->avail);
        size_t n = len - done < c->avail ? len - done : c->avail;
        memcpy(out + done, src, n);
        secure_zero(src, n);    /* used bytes don't linger in the block */
        c->avail -= n;
        done += n;
    }
    return true;
}

/* Return the adapter by value; ctx still points to the caller's original c. */
rng_t conditioned_rng_as_rng(conditioned_rng_t *c)
{
    return (rng_t){ .read = conditioned_read, .ctx = c };
}
