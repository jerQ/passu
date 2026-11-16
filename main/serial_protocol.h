/* Versioned machine requests; manual Enter remains supported. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "serial_enter.h"

#define SERIAL_REQUEST_ID_LEN 32u
#define SERIAL_REQUEST_MAX 64u

typedef enum { SERIAL_NONE, SERIAL_ENTER, SERIAL_REQUEST } serial_event_t;
typedef struct {
    char line[SERIAL_REQUEST_MAX];
    size_t len;
    bool overflow;
    serial_enter_t enter;
} serial_parser_t;

/* Invalid/overlong lines are discarded through their next terminator.
 * Feed bytes only, not EOF/timeouts. id is filled only for SERIAL_REQUEST. */
serial_event_t serial_parser_feed(serial_parser_t *p, int c,
                                  char id[SERIAL_REQUEST_ID_LEN + 1]);
uint32_t serial_crc32(const char *data, size_t len);
