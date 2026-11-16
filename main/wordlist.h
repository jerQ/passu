/**
 * @file wordlist.h
 * @brief Validate dice-code records and index words without copying them.
 *
 * EFF large wordlist (7776 words, five d6 rolls per word).
 *
 * The list is embedded verbatim in flash. At boot it is parsed once into an
 * index of offsets and strictly validated: every line must be
 * "<five dice digits><tab><word>", and the dice codes must appear in exact
 * order 11111, 11112, ... 66666. This validates the structure and the
 * dice-to-index mapping. The pinned SHA-256 checked by the build and boot
 * code verifies the exact EFF list. Parsing alone does not check that
 * words are unique or match EFF when hash pinning is disabled.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORDLIST_DICE      5u
#define WORDLIST_SIZE      7776u  /* 6^5 */
#define WORDLIST_MAX_WORD  16u    /* EFF words are <= 9; generous limit */

/**
 * Caller-owned index borrowing the original text buffer. The buffer must remain
 * unchanged and readable for all lookups. Offsets/lengths locate word slices,
 * so no per-word allocation is needed (the index uses about 39 KB).
 * Publish for read-only use only after wordlist_init() returns WL_OK.
 */
typedef struct {
    const char *data;        /* Borrowed, length-bounded text, not a C string. */
    uint32_t    offset[WORDLIST_SIZE];
    uint8_t     length[WORDLIST_SIZE];
    unsigned    max_len;     /* Longest accepted word, in bytes. */
} wordlist_t;

typedef enum {
    WL_OK = 0,
    WL_ERR_TRUNCATED,     /* fewer than 7776 entries */
    WL_ERR_DICE_CODE,     /* dice code malformed or out of order */
    WL_ERR_SEPARATOR,     /* no tab/space after the dice code */
    WL_ERR_WORD,          /* empty, too long or invalid characters */
    WL_ERR_TRAILING,      /* non-whitespace after the last entry */
} wordlist_err_t;

/**
 * Build wl from exactly len bytes at data; no terminating NUL is required.
 * wl and data must be valid, with len small enough for 32-bit stored offsets.
 * Accepts ordered five-die codes, one or more spaces/tabs, and lowercase words
 * or hyphens of length 1..WORDLIST_MAX_WORD. CRLF and a missing final newline
 * are allowed. Only whitespace or NUL bytes may follow the final record.
 *
 * bad_line may be null. Otherwise it receives 0 on success or the 1-based
 * failing record; WORDLIST_SIZE + 1 identifies unexpected trailing content.
 * On failure wl may be partly populated and must not be used for lookups.
 * This parser checks neither the file hash nor word uniqueness.
 */
wordlist_err_t wordlist_init(wordlist_t *wl, const char *data, size_t len,
                             unsigned *bad_line);

/** Return a static diagnostic string for a parser result; no allocation. */
const char *wordlist_strerror(wordlist_err_t err);

/**
 * Convert five dice (each 1..6) to a zero-based index 0..7775.
 * dice must address WORDLIST_DICE bytes and index one writable uint16_t.
 * The first die is most significant: 11111 -> 0, 66666 -> 7775.
 * Returns false for out-of-range dice, leaving *index unchanged.
 */
bool wordlist_index_from_dice(const uint8_t dice[WORDLIST_DICE], uint16_t *index);

/**
 * Return a borrowed word slice from a successfully initialized wl.
 * len must be non-null; *len receives the byte count. The slice is not
 * NUL-terminated: use its length for memcpy/formatting rather than strlen().
 * An index >= WORDLIST_SIZE returns null and writes zero to *len.
 */
const char *wordlist_word(const wordlist_t *wl, uint16_t index, unsigned *len);
