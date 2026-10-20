#include "wordlist.h"

#include <string.h>

static void dice_code_for_index(unsigned index, char code[WORDLIST_DICE])
{
    for (int i = WORDLIST_DICE - 1; i >= 0; i--) {
        code[i] = (char)('1' + index % 6);
        index /= 6;
    }
}

static bool is_word_char(char c)
{
    /* The EFF large list is lowercase a-z plus a few hyphenated words. */
    return (c >= 'a' && c <= 'z') || c == '-';
}

wordlist_err_t wordlist_init(wordlist_t *wl, const char *data, size_t len,
                             unsigned *bad_line)
{
    size_t p = 0;
    unsigned dummy;
    if (!bad_line) {
        bad_line = &dummy;
    }

    memset(wl, 0, sizeof(*wl));
    wl->data = data;

    for (unsigned i = 0; i < WORDLIST_SIZE; i++) {
        *bad_line = i + 1;

        /* dice code */
        char expect[WORDLIST_DICE];
        dice_code_for_index(i, expect);
        if (p + WORDLIST_DICE > len) {
            return WL_ERR_TRUNCATED;
        }
        if (memcmp(data + p, expect, WORDLIST_DICE) != 0) {
            return WL_ERR_DICE_CODE;
        }
        p += WORDLIST_DICE;

        /* separator: one or more tabs/spaces */
        if (p >= len || (data[p] != '\t' && data[p] != ' ')) {
            return WL_ERR_SEPARATOR;
        }
        while (p < len && (data[p] == '\t' || data[p] == ' ')) {
            p++;
        }

        /* word */
        size_t start = p;
        while (p < len && is_word_char(data[p])) {
            p++;
        }
        size_t wlen = p - start;
        if (wlen == 0 || wlen > WORDLIST_MAX_WORD) {
            return WL_ERR_WORD;
        }

        /* end of line: optional '\r', then '\n' (or end of data on last line) */
        if (p < len && data[p] == '\r') {
            p++;
        }
        if (p < len) {
            if (data[p] != '\n') {
                return WL_ERR_WORD;
            }
            p++;
        } else if (i != WORDLIST_SIZE - 1) {
            return WL_ERR_TRUNCATED;
        }

        wl->offset[i] = (uint32_t)start;
        wl->length[i] = (uint8_t)wlen;
        if (wlen > wl->max_len) {
            wl->max_len = (unsigned)wlen;
        }
    }

    /* only whitespace may follow the last entry */
    *bad_line = WORDLIST_SIZE + 1;
    for (; p < len; p++) {
        char c = data[p];
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t' && c != '\0') {
            return WL_ERR_TRAILING;
        }
    }
    *bad_line = 0;
    return WL_OK;
}

const char *wordlist_strerror(wordlist_err_t err)
{
    switch (err) {
    case WL_OK:            return "ok";
    case WL_ERR_TRUNCATED: return "list truncated (fewer than 7776 entries)";
    case WL_ERR_DICE_CODE: return "dice code malformed or out of order";
    case WL_ERR_SEPARATOR: return "missing separator after dice code";
    case WL_ERR_WORD:      return "invalid word";
    case WL_ERR_TRAILING:  return "unexpected data after last entry";
    }
    return "unknown error";
}

bool wordlist_index_from_dice(const uint8_t dice[WORDLIST_DICE], uint16_t *index)
{
    unsigned idx = 0;
    for (unsigned i = 0; i < WORDLIST_DICE; i++) {
        if (dice[i] < 1 || dice[i] > 6) {
            return false;
        }
        idx = idx * 6 + (dice[i] - 1);
    }
    *index = (uint16_t)idx;
    return true;
}

const char *wordlist_word(const wordlist_t *wl, uint16_t index, unsigned *len)
{
    if (index >= WORDLIST_SIZE) {
        *len = 0;
        return NULL;
    }
    *len = wl->length[index];
    return wl->data + wl->offset[index];
}
