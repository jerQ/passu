/*
 * Text rendering for 128-pixel-wide monochrome OLEDs (SSD1306/SH1106 family).
 *
 * The framebuffer uses the controllers' own layout: the screen is split into
 * 8-pixel-high pages, and each byte is one column of a page, bit 0 at the
 * top. One text row is exactly one page. Characters are 6 px wide (5x7
 * glyph + 1 px gap); '-' is only 4 px wide, so two groups like
 * "abcdefghi1-abcdefghi1-" (the longest possible) fit in 128 px.
 *
 * Portable: no ESP-IDF code, so it is tested on the host. oled.c sends the
 * framebuffer to the display.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DISPLAY_WIDTH    128u
#define DISPLAY_MAX_ROWS 8u    /* 64 px / 8 */

typedef struct {
    unsigned rows;             /* text rows (pages): 4 for 128x32, 8 for 128x64 */
    uint8_t  page[DISPLAY_MAX_ROWS][DISPLAY_WIDTH];
} display_fb_t;

/* Set the size and clear. rows is 1..DISPLAY_MAX_ROWS. */
void display_init(display_fb_t *fb, unsigned rows);

/* Clear every pixel with secure_zero, so no passphrase pixels remain. */
void display_wipe(display_fb_t *fb);

/* Width in pixels of the first `len` chars of `s`. */
unsigned display_text_width(const char *s, size_t len);

/* Draw `len` chars of `s` on text row `row`, starting at pixel column `x`.
 * Returns false, drawing nothing, if the text doesn't fit. */
bool display_text(display_fb_t *fb, unsigned row, unsigned x,
                  const char *s, size_t len);

/* Clear, then draw up to DISPLAY_MAX_ROWS lines, each centred, the block
 * centred vertically. Returns false (and leaves the screen clear) if a line
 * doesn't fit. */
bool display_message(display_fb_t *fb, const char *const *lines, unsigned n);

/* Clear, then lay out a passphrase "word1-word2-..." with its hyphens, so
 * the screen shows exactly what to type: one group per row if the groups
 * fit, otherwise two per row. A group ends at a digit; hyphens inside a
 * word (e.g. "t-shirt") stay with it. Returns false (and leaves the screen
 * clear) if the passphrase doesn't fit. */
bool display_passphrase(display_fb_t *fb, const char *phrase);
