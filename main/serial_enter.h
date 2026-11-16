/* Coalesce CRLF while accepting standalone CR and LF as Enter. This needs
 * raw console input: sdkconfig.defaults disables ESP-IDF's CR-to-LF
 * stdin conversion, which would otherwise turn CRLF into two LFs. */
#pragma once
#include <stdbool.h>

typedef struct { bool previous_cr; } serial_enter_t;

/* Feed received characters only; EOF/timeouts must not change state. */
static inline bool serial_enter_feed(serial_enter_t *state, int c)
{
    bool enter = c == '\r' || (c == '\n' && !state->previous_cr);
    state->previous_cr = c == '\r';
    return enter;
}
