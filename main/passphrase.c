#include "passphrase.h"

#include <string.h>

#include "dice.h"

void secure_zero(void *p, size_t len)
{
    volatile uint8_t *v = p;
    while (len--) {
        *v++ = 0;
    }
}

pp_err_t passphrase_generate(const rng_t *rng, const wordlist_t *wl,
                             unsigned groups, char *out, size_t out_size)
{
    if (!rng || !wl || !out || groups == 0 || groups > PASSPHRASE_MAX_GROUPS) {
        return PP_ERR_ARGS;
    }
    if (out_size == 0) {
        return PP_ERR_BUFFER;
    }

    pp_err_t err = PP_OK;
    uint8_t dice[WORDLIST_DICE];
    uint8_t digit = 0;
    uint16_t index = 0;
    size_t pos = 0;

    for (unsigned g = 0; g < groups; g++) {
        /* five d6 rolls -> word */
        for (unsigned d = 0; d < WORDLIST_DICE; d++) {
            if (!dice_d6(rng, &dice[d])) {
                err = PP_ERR_ENTROPY;
                goto out;
            }
        }
        if (!wordlist_index_from_dice(dice, &index)) {
            err = PP_ERR_ENTROPY;  /* cannot happen with a correct d6 */
            goto out;
        }
        /* one extra roll -> digit */
        if (!dice_digit(rng, &digit)) {
            err = PP_ERR_ENTROPY;
            goto out;
        }

        unsigned wlen;
        const char *word = wordlist_word(wl, index, &wlen);
        size_t need = wlen + 1 + (g + 1 < groups ? 1 : 0); /* word+digit[+'-'] */
        if (!word || pos + need + 1 > out_size) {             /* +1 for NUL */
            err = PP_ERR_BUFFER;
            goto out;
        }
        memcpy(out + pos, word, wlen);
        pos += wlen;
        out[pos++] = (char)('0' + digit);
        if (g + 1 < groups) {
            out[pos++] = '-';
        }
    }
    out[pos] = '\0';

out:
    secure_zero(dice, sizeof(dice));
    secure_zero(&digit, sizeof(digit));
    secure_zero(&index, sizeof(index));
    if (err != PP_OK) {
        secure_zero(out, out_size);
    }
    return err;
}

const char *passphrase_strerror(pp_err_t err)
{
    switch (err) {
    case PP_OK:          return "ok";
    case PP_ERR_ARGS:    return "invalid arguments";
    case PP_ERR_BUFFER:  return "output buffer too small";
    case PP_ERR_ENTROPY: return "entropy source failure";
    }
    return "unknown error";
}
