/*
 * EFF large wordlist (7776 words, five d6 rolls per word).
 *
 * The list is embedded verbatim in flash. At boot it is parsed once into an
 * index of offsets and strictly validated: every line must be
 * "<five dice digits><tab><word>", and the dice codes must appear in exact
 * order 11111, 11112, ... 66666. This proves that dice -> index -> word
 * matches the printed EFF list, and catches a truncated or edited file.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORDLIST_DICE      5u
#define WORDLIST_SIZE      7776u  /* 6^5 */
#define WORDLIST_MAX_WORD  16u    /* EFF words are <= 9; generous limit */

typedef struct {
    const char *data;
    uint32_t    offset[WORDLIST_SIZE];
    uint8_t     length[WORDLIST_SIZE];
    unsigned    max_len;
} wordlist_t;

typedef enum {
    WL_OK = 0,
    WL_ERR_TRUNCATED,     /* fewer than 7776 entries */
    WL_ERR_DICE_CODE,     /* dice code malformed or out of order */
    WL_ERR_SEPARATOR,     /* no tab/space after the dice code */
    WL_ERR_WORD,          /* empty, too long or invalid characters */
    WL_ERR_TRAILING,      /* non-whitespace after the last entry */
} wordlist_err_t;

/* Parse and validate. On error, *bad_line (1-based) says where. */
wordlist_err_t wordlist_init(wordlist_t *wl, const char *data, size_t len,
                             unsigned *bad_line);

const char *wordlist_strerror(wordlist_err_t err);

/* Five d6 values (1..6, first die most significant) -> index 0..7775.
 * Returns false if any value is out of range. */
bool wordlist_index_from_dice(const uint8_t dice[WORDLIST_DICE], uint16_t *index);

/* Word for an index (not NUL-terminated; length via *len). */
const char *wordlist_word(const wordlist_t *wl, uint16_t index, unsigned *len);
