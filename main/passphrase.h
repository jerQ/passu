/*
 * Passphrase format: <word><digit>-<word><digit>-...  (e.g. 8 groups)
 *
 * Each group uses five d6 rolls to pick a word from the EFF large list and
 * one extra roll for a decimal digit 0-9 appended to the word.
 * Strength per group: log2(7776) + log2(10) ~ 12.92 + 3.32 = 16.25 bits.
 * Eight groups ~ 130 bits.
 */
#pragma once

#include "rng.h"
#include "wordlist.h"

#define PASSPHRASE_MAX_GROUPS 16u

/* Buffer size that always fits `groups` groups for a given wordlist. */
#define PASSPHRASE_BUF_SIZE(groups) \
    ((groups) * (WORDLIST_MAX_WORD + 2u) + 1u)

typedef enum {
    PP_OK = 0,
    PP_ERR_ARGS,
    PP_ERR_BUFFER,
    PP_ERR_ENTROPY,
} pp_err_t;

/* Generate a passphrase into `out` (NUL-terminated). On any error the
 * output buffer is wiped and nothing partial is left behind. */
pp_err_t passphrase_generate(const rng_t *rng, const wordlist_t *wl,
                             unsigned groups, char *out, size_t out_size);

const char *passphrase_strerror(pp_err_t err);
