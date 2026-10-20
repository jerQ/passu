#include "display.h"

#include <string.h>

#include "font6x8.h"
#include "rng.h"

#define CHAR_ADVANCE   6u
#define HYPHEN_ADVANCE 4u   /* the '-' glyph only uses columns 1-3 */
#define MAX_GROUPS     16u

void display_init(display_fb_t *fb, unsigned rows)
{
    if (rows == 0 || rows > DISPLAY_MAX_ROWS) {
        rows = DISPLAY_MAX_ROWS;
    }
    fb->rows = rows;
    display_wipe(fb);
}

void display_wipe(display_fb_t *fb)
{
    secure_zero(fb->page, sizeof(fb->page));
}

static const uint8_t *glyph(char c)
{
    unsigned char u = (unsigned char)c;
    if (u < FONT_FIRST || u > FONT_LAST) {
        u = '?';
    }
    return font6x8[u - FONT_FIRST];
}

unsigned display_text_width(const char *s, size_t len)
{
    unsigned w = 0;
    for (size_t i = 0; i < len; i++) {
        w += s[i] == '-' ? HYPHEN_ADVANCE : CHAR_ADVANCE;
    }
    return w;
}

bool display_text(display_fb_t *fb, unsigned row, unsigned x,
                  const char *s, size_t len)
{
    if (row >= fb->rows || x > DISPLAY_WIDTH ||
        display_text_width(s, len) > DISPLAY_WIDTH - x) {
        return false;
    }
    uint8_t *page = fb->page[row];
    for (size_t i = 0; i < len; i++) {
        const uint8_t *g = glyph(s[i]);
        if (s[i] == '-') {
            /* narrow: glyph columns 1-3, then the 1 px gap */
            memcpy(&page[x], &g[1], 3);
            x += HYPHEN_ADVANCE;
        } else {
            memcpy(&page[x], g, 5);
            x += CHAR_ADVANCE;
        }
    }
    return true;
}

bool display_message(display_fb_t *fb, const char *const *lines, unsigned n)
{
    display_wipe(fb);
    if (n > fb->rows) {
        return false;
    }
    unsigned top = (fb->rows - n) / 2;
    for (unsigned i = 0; i < n; i++) {
        size_t len = strlen(lines[i]);
        unsigned w = display_text_width(lines[i], len);
        unsigned x = w < DISPLAY_WIDTH ? (DISPLAY_WIDTH - w) / 2 : 0;
        if (!display_text(fb, top + i, x, lines[i], len)) {
            display_wipe(fb);
            return false;
        }
    }
    return true;
}

/* Split "word1-word2-..." into groups, each including its trailing '-'.
 * A group ends at a digit followed by '-' or the end of the string. */
static unsigned split_groups(const char *p, const char *start[MAX_GROUPS],
                             size_t len[MAX_GROUPS])
{
    unsigned n = 0;
    const char *g = p;
    for (; *p; p++) {
        if (*p >= '0' && *p <= '9' && (p[1] == '-' || p[1] == '\0')) {
            if (n == MAX_GROUPS) {
                return MAX_GROUPS + 1;          /* too many */
            }
            const char *end = p[1] == '-' ? p + 2 : p + 1;
            start[n] = g;
            len[n] = (size_t)(end - g);
            n++;
            g = end;
            if (p[1] == '-') {
                p++;
            }
        }
    }
    return *g == '\0' ? n : MAX_GROUPS + 1;     /* trailing text: malformed */
}

bool display_passphrase(display_fb_t *fb, const char *phrase)
{
    const char *start[MAX_GROUPS];
    size_t len[MAX_GROUPS];
    bool ok = true;

    display_wipe(fb);
    unsigned n = split_groups(phrase, start, len);
    if (n == 0 || n > MAX_GROUPS) {
        ok = false;
    } else if (n <= fb->rows) {
        unsigned top = (fb->rows - n) / 2;
        for (unsigned i = 0; i < n && ok; i++) {
            ok = display_text(fb, top + i, 0, start[i], len[i]);
        }
    } else if ((n + 1) / 2 <= fb->rows) {
        unsigned used = (n + 1) / 2;
        unsigned top = (fb->rows - used) / 2;
        for (unsigned i = 0; i < n && ok; i += 2) {
            /* both groups are contiguous in the phrase */
            size_t l = len[i] + (i + 1 < n ? len[i + 1] : 0);
            ok = display_text(fb, top + i / 2, 0, start[i], l);
        }
    } else {
        ok = false;
    }

    secure_zero(start, sizeof(start));
    secure_zero(len, sizeof(len));
    if (!ok) {
        display_wipe(fb);
    }
    return ok;
}
