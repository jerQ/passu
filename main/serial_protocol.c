#include "serial_protocol.h"
#include <string.h>

serial_event_t serial_parser_feed(serial_parser_t *p, int c, uint32_t now_ms,
                                  char id[SERIAL_REQUEST_ID_LEN + 1])
{
    bool enter = serial_enter_feed(&p->enter, c, now_ms);
    if ((c == '\r' || c == '\n') && !enter) {
        return SERIAL_NONE;
    }
    if (c != '\r' && c != '\n') {
        if (p->len < sizeof(p->line)) {
            p->line[p->len++] = (char)c;
        } else {
            p->overflow = true;
        }
        return SERIAL_NONE;
    }
    serial_event_t event = SERIAL_NONE;
    if (!p->overflow && p->len == 0) {
        event = SERIAL_ENTER;
    } else if (!p->overflow && p->len == 8 + SERIAL_REQUEST_ID_LEN &&
               memcmp(p->line, "PASSU/1 ", 8) == 0) {
        bool valid = true;
        for (size_t i = 8; i < p->len; i++) {
            char ch = p->line[i];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
                valid = false;
            }
        }
        if (valid) {
            memcpy(id, p->line + 8, SERIAL_REQUEST_ID_LEN);
            id[SERIAL_REQUEST_ID_LEN] = '\0';
            event = SERIAL_REQUEST;
        }
    }
    memset(p->line, 0, sizeof(p->line));
    p->len = 0;
    p->overflow = false;
    return event;
}

/* CRC-32/ISO-HDLC, matching Go hash/crc32 and Python zlib.crc32. */
uint32_t serial_crc32(const char *data, size_t len)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint8_t)data[i];
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
        }
    }
    return crc ^ UINT32_MAX;
}
