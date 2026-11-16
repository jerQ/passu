/* Coalesce CRLF while accepting standalone CR and LF as Enter. This needs
 * raw console input: sdkconfig.defaults disables ESP-IDF's CR-to-LF
 * stdin conversion, which would otherwise turn CRLF into two LFs. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* An LF completes a CRLF only if it follows the CR within this window. A
 * terminal sends CRLF back to back; a later bare LF is a separate Enter. */
#define SERIAL_ENTER_CRLF_MS 50u

typedef struct {
    bool     previous_cr;   /* last byte fed was CR */
    uint32_t cr_ms;         /* when that CR arrived */
} serial_enter_t;

/* Feed received characters only; EOF/timeouts must not change state.
 * now_ms is a free-running millisecond clock; wraparound is handled. */
static inline bool serial_enter_feed(serial_enter_t *state, int c, uint32_t now_ms)
{
    bool completes_crlf = c == '\n' && state->previous_cr &&
                          (uint32_t)(now_ms - state->cr_ms) <= SERIAL_ENTER_CRLF_MS;
    bool enter = c == '\r' || (c == '\n' && !completes_crlf);
    state->previous_cr = c == '\r';
    if (c == '\r') {
        state->cr_ms = now_ms;
    }
    return enter;
}
