/**
 * @file passphrase.h
 * @brief Build word+digit groups into a caller-supplied C string.
 *
 * Passphrase format: <word><digit>-<word><digit>-...  (e.g. 8 groups)
 *
 * Each group uses five d6 rolls to pick a word from the EFF large list and
 * one extra roll for a decimal digit 0-9 appended to the word.
 * Strength per group: log2(7776) + log2(10) ~ 12.92 + 3.32 = 16.25 bits.
 * Eight groups ~ 130 bits, assuming independent uniform choices.
 */
#pragma once

#include "rng.h"
#include "wordlist.h"

#define PASSPHRASE_MAX_GROUPS 16u

/* Conservative capacity, including separators and the terminating NUL.
 * Uses the parser's allowed word length, not the shorter pinned EFF maximum.
 * The final group has no separator, leaving one spare byte in this estimate. */
#define PASSPHRASE_BUF_SIZE(groups) \
    ((groups) * (WORDLIST_MAX_WORD + 2u) + 1u)

/** Generation result; PP_ERR_ENTROPY also covers exhausted rejection attempts. */
typedef enum {
    PP_OK = 0,
    PP_ERR_ARGS,
    PP_ERR_BUFFER,
    PP_ERR_ENTROPY,
} pp_err_t;

/**
 * Generate groups word+digit groups separated by '-'; NUL-terminate on success.
 * rng must contain a valid callback. wl must have completed wordlist_init()
 * successfully, and its backing data must still exist. groups is 1..16 here;
 * the firmware's Kconfig setting imposes a separate minimum of five.
 *
 * out must address out_size writable bytes and not alias the wordlist or RNG
 * state. PASSPHRASE_BUF_SIZE(groups) is sufficient for any accepted wordlist.
 * No allocation or output I/O is performed. The RNG state advances on draws,
 * including draws made before an error; a failed attempt cannot be rolled back.
 *
 * Returns PP_OK, PP_ERR_ARGS, PP_ERR_BUFFER, or PP_ERR_ENTROPY. On error,
 * supplied writable output bytes are erased. A null out or out_size == 0
 * provides no writable range to clear. Internal dice/index temporaries are
 * cleared after generation; the caller owns the successful phrase and its wipe.
 */
pp_err_t passphrase_generate(const rng_t *rng, const wordlist_t *wl,
                             unsigned groups, char *out, size_t out_size);

/** Return a static diagnostic string; do not free or modify it. */
const char *passphrase_strerror(pp_err_t err);
