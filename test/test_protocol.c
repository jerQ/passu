#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "serial_protocol.h"

static const char *id = "00112233445566778899aabbccddeeff";
static serial_event_t feed(serial_parser_t *p, const char *s, char *out)
{
    serial_event_t last = SERIAL_NONE;
    for (; *s; s++) {
        serial_event_t event = serial_parser_feed(p, *s, out);
        if (event != SERIAL_NONE) last = event;
    }
    return last;
}
int main(void)
{
    serial_parser_t p = {0};
    char out[SERIAL_REQUEST_ID_LEN + 1] = {0};
    char request[64];
    snprintf(request, sizeof(request), "PASSU/1 %s\r\n", id);
    assert(feed(&p, request, out) == SERIAL_REQUEST);
    assert(strcmp(out, id) == 0);
    assert(feed(&p, "\r", out) == SERIAL_ENTER);
    assert(feed(&p, "\n", out) == SERIAL_NONE);
    assert(feed(&p, "\n", out) == SERIAL_ENTER);
    assert(feed(&p, "PASSU/1 short\n", out) == SERIAL_NONE);
    snprintf(request, sizeof(request), "PASSU/1 z%s\n", id + 1);
    assert(feed(&p, request, out) == SERIAL_NONE);
    for (unsigned i = 0; i < 100; i++) assert(serial_parser_feed(&p, 'a', out) == SERIAL_NONE);
    assert(feed(&p, "\n", out) == SERIAL_NONE);
    snprintf(request, sizeof(request), "PASSU/1 %s\n", id);
    assert(feed(&p, request, out) == SERIAL_REQUEST);
    assert(serial_crc32("123456789", 9) == UINT32_C(0xcbf43926));
    assert(serial_crc32("", 0) == 0);
    const char *phrase = "apple3-orbit0-canopy5-mellow2-zigzag9";
    printf("PASSU/1 %s 5 %u %08lx %s\n", id, (unsigned)strlen(phrase),
           (unsigned long)serial_crc32(phrase, strlen(phrase)), phrase);
    return 0;
}
