/*
 * Host-side tests for the platform-independent modules.
 * Build and run:  make -C test
 *
 * Uses a synthetic wordlist in EFF format, or the real list if
 * ../main/eff_large_wordlist.txt exists.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/sha.h>

#include "condition.h"
#include "dice.h"
#include "display.h"
#include "health.h"
#include "passphrase.h"
#include "wordlist.h"

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

/* ---------- fake sources ---------- */

typedef struct { const uint8_t *bytes; size_t n, pos; } seq_t;
static bool seq_read(void *ctx, uint8_t *out, size_t len)
{
    seq_t *s = ctx;
    for (size_t i = 0; i < len; i++) {
        if (s->pos >= s->n) return false;
        out[i] = s->bytes[s->pos++];
    }
    return true;
}

static FILE *urandom;
static bool urandom_read(void *ctx, uint8_t *out, size_t len)
{
    (void)ctx;
    return fread(out, 1, len, urandom) == len;
}
static const rng_t URANDOM = { urandom_read, NULL };

static bool const_read(void *ctx, uint8_t *out, size_t len)
{
    memset(out, *(uint8_t *)ctx, len);
    return true;
}

static bool sha256_openssl(const uint8_t *data, size_t len, uint8_t out[32])
{
    return SHA256(data, len, out) != NULL;
}

static bool sha256_broken(const uint8_t *data, size_t len, uint8_t out[32])
{
    (void)data; (void)len; (void)out;
    return false;
}

/* ---------- synthetic wordlist ---------- */

static char *make_list(size_t *len, const char *eol)
{
    size_t cap = WORDLIST_SIZE * 24, p = 0;
    char *buf = malloc(cap);
    for (unsigned i = 0; i < WORDLIST_SIZE; i++) {
        char code[6];
        unsigned x = i;
        for (int d = 4; d >= 0; d--) { code[d] = '1' + x % 6; x /= 6; }
        code[5] = 0;
        /* word = "w" + base-26 letters of index, e.g. unique & lowercase */
        char word[8]; int wl = 0; x = i;
        word[wl++] = 'w';
        do { word[wl++] = 'a' + x % 26; x /= 26; } while (x);
        word[wl] = 0;
        p += snprintf(buf + p, cap - p, "%s\t%s%s", code, word, eol);
    }
    *len = p;
    return buf;
}

static char *load_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(n);
    *len = fread(buf, 1, n, f);
    fclose(f);
    return buf;
}

static wordlist_t wl;

/* ---------- tests ---------- */

static void test_wordlist(void)
{
    printf("wordlist parsing/validation\n");
    size_t len; unsigned line;
    char *good = make_list(&len, "\n");
    CHECK(wordlist_init(&wl, good, len, &line) == WL_OK);

    unsigned n; const char *w;
    uint8_t first[5] = {1,1,1,1,1}, last[5] = {6,6,6,6,6}, mid[5] = {2,3,4,5,6};
    uint16_t idx;
    CHECK(wordlist_index_from_dice(first, &idx) && idx == 0);
    CHECK(wordlist_index_from_dice(last, &idx) && idx == 7775);
    CHECK(wordlist_index_from_dice(mid, &idx) && idx == ((((1*6+2)*6+3)*6+4)*6+5));
    w = wordlist_word(&wl, 0, &n);    CHECK(n == 2 && memcmp(w, "wa", 2) == 0);
    uint8_t bad[5] = {1,1,0,1,1};
    CHECK(!wordlist_index_from_dice(bad, &idx));
    uint8_t bad7[5] = {1,1,7,1,1};
    CHECK(!wordlist_index_from_dice(bad7, &idx));

    /* CRLF accepted, trailing blank lines accepted */
    size_t l2; char *crlf = make_list(&l2, "\r\n");
    CHECK(wordlist_init(&wl, crlf, l2, &line) == WL_OK);
    free(crlf);

    /* last line without newline accepted */
    CHECK(wordlist_init(&wl, good, len - 1, &line) == WL_OK);

    /* truncated */
    CHECK(wordlist_init(&wl, good, len / 2, &line) == WL_ERR_TRUNCATED);

    /* out-of-order code */
    char *c = strdup(good);
    char *l100 = c; for (int i = 0; i < 100; i++) l100 = strchr(l100, '\n') + 1;
    l100[4] = l100[4] == '1' ? '2' : '1';
    CHECK(wordlist_init(&wl, c, len, &line) == WL_ERR_DICE_CODE && line == 101);
    free(c);

    /* uppercase word */
    c = strdup(good);
    l100 = c; for (int i = 0; i < 100; i++) l100 = strchr(l100, '\n') + 1;
    l100[6] = 'W';
    CHECK(wordlist_init(&wl, c, len, &line) == WL_ERR_WORD);
    free(c);

    /* garbage after list */
    c = malloc(len + 8); memcpy(c, good, len); memcpy(c + len, "junk\n", 5);
    CHECK(wordlist_init(&wl, c, len + 5, &line) == WL_ERR_TRAILING);
    free(c);
    free(good);

    /* real EFF list, if present */
    char *real = load_file("../main/eff_large_wordlist.txt", &len);
    if (real) {
        printf("  using real EFF list\n");
        CHECK(wordlist_init(&wl, real, len, &line) == WL_OK);
        w = wordlist_word(&wl, 0, &n);    CHECK(n == 6 && !memcmp(w, "abacus", 6));
        w = wordlist_word(&wl, 7775, &n); CHECK(n == 4 && !memcmp(w, "zoom", 4));
        /* intentionally leaked: wl points into it for the rest of the run */
    } else {
        good = make_list(&len, "\n");
        CHECK(wordlist_init(&wl, good, len, &line) == WL_OK);
    }
}

static void test_dice(void)
{
    printf("dice rejection sampling\n");
    uint8_t v;
    const uint8_t d6seq[] = { 252, 253, 254, 255, 11 };
    seq_t s = { d6seq, sizeof d6seq, 0 };
    rng_t r = { seq_read, &s };
    CHECK(dice_d6(&r, &v) && v == 11 % 6 + 1 && s.pos == 5);

    const uint8_t d10seq[] = { 250, 251, 255, 249 };
    s = (seq_t){ d10seq, sizeof d10seq, 0 };
    CHECK(dice_digit(&r, &v) && v == 9 && s.pos == 4);

    /* boundaries accepted */
    const uint8_t b[] = { 0, 251, 249 };
    s = (seq_t){ b, sizeof b, 0 };
    CHECK(dice_d6(&r, &v) && v == 1);
    CHECK(dice_d6(&r, &v) && v == 6);
    CHECK(dice_digit(&r, &v) && v == 9);

    /* always-rejected source gives up */
    uint8_t ff = 0xff;
    rng_t stuck = { const_read, &ff };
    CHECK(!dice_d6(&stuck, &v));
    CHECK(!dice_digit(&stuck, &v));

    /* source failure propagates */
    s = (seq_t){ b, 0, 0 };
    CHECK(!dice_d6(&r, &v));

    /* distribution: chi-squared on 600k d6 and 1M digits */
    long c6[6] = {0}, c10[10] = {0};
    const long N6 = 600000, N10 = 1000000;
    for (long i = 0; i < N6; i++) { dice_d6(&URANDOM, &v); c6[v - 1]++; }
    for (long i = 0; i < N10; i++) { dice_digit(&URANDOM, &v); c10[v]++; }
    double chi6 = 0, chi10 = 0;
    for (int i = 0; i < 6; i++)  { double e = N6 / 6.0;  chi6  += (c6[i]-e)*(c6[i]-e)/e; }
    for (int i = 0; i < 10; i++) { double e = N10 / 10.0; chi10 += (c10[i]-e)*(c10[i]-e)/e; }
    printf("  chi2 d6 = %.2f (df 5, p<0.001 at 20.5)\n", chi6);
    printf("  chi2 d10 = %.2f (df 9, p<0.001 at 27.9)\n", chi10);
    CHECK(chi6 < 20.5);
    CHECK(chi10 < 27.9);

    /* the naive modulo approach would be biased - demonstrate it */
    long naive[6] = {0};
    for (int b2 = 0; b2 < 256; b2++) naive[b2 % 6]++;
    CHECK(naive[0] == 43 && naive[5] == 42);   /* why rejection is needed */
}

static void test_health(void)
{
    printf("health tests\n");
    checked_rng_t c; uint8_t buf[64];

    /* stuck source trips RCT */
    uint8_t k = 0x42;
    checked_rng_init(&c, (rng_t){ const_read, &k });
    CHECK(!checked_rng_startup(&c));
    CHECK(c.health.failed && strstr(c.health.fail_reason, "repetition"));
    /* failure latches and output is wiped */
    memset(buf, 0xAA, sizeof buf);
    rng_t r = checked_rng_as_rng(&c);
    CHECK(!rng_read(&r, buf, sizeof buf));
    CHECK(buf[0] == 0 && buf[63] == 0);

    /* biased (not stuck) source trips APT: value 7 in every other byte.
     * APT compares against the first sample of each window, so the biased
     * value sits at even positions (window starts are even). */
    health_t h; health_init(&h);
    bool ok = true;
    for (int i = 0; i < 2000 && ok; i++) ok = health_feed(&h, (i & 1) ? (uint8_t)(i * 37) : 7);
    CHECK(!ok && strstr(h.fail_reason, "proportion"));

    /* RCT cutoff exact: 5 repeats pass, 6th fails */
    health_init(&h);
    for (int i = 0; i < 5; i++) CHECK(health_feed(&h, 9));
    CHECK(!health_feed(&h, 9));

    /* good source: 10 MB without false alarm */
    checked_rng_init(&c, URANDOM);
    CHECK(checked_rng_startup(&c));
    r = checked_rng_as_rng(&c);
    ok = true;
    for (int i = 0; i < (10 << 20) / 64 && ok; i++) ok = rng_read(&r, buf, sizeof buf);
    CHECK(ok && !c.health.failed);

    /* failing inner source latches */
    seq_t empty = { NULL, 0, 0 };
    checked_rng_init(&c, (rng_t){ seq_read, &empty });
    r = checked_rng_as_rng(&c);
    CHECK(!rng_read(&r, buf, 1));
    CHECK(c.health.failed);
}

static bool all_zero(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) if (p[i]) return false;
    return true;
}

static void test_condition(void)
{
    printf("conditioning\n");
    uint8_t raw[3 * CONDITION_RAW_BYTES];
    for (size_t i = 0; i < sizeof raw; i++) raw[i] = (uint8_t)(i * 7 + 1);
    uint8_t want[2][32], got[64], in[CONDITION_RAW_BYTES + 4];
    SHA256(raw, CONDITION_RAW_BYTES, want[0]);
    SHA256(raw + CONDITION_RAW_BYTES, CONDITION_RAW_BYTES, want[1]);

    /* each block is SHA-256 of the next 128 raw bytes */
    conditioned_rng_t c;
    seq_t s = { raw, sizeof raw, 0 };
    conditioned_rng_init(&c, (rng_t){ seq_read, &s }, sha256_openssl);
    rng_t r = conditioned_rng_as_rng(&c);
    CHECK(rng_read(&r, got, 64));
    CHECK(memcmp(got, want[0], 32) == 0 && memcmp(got + 32, want[1], 32) == 0);
    CHECK(s.pos == 2 * CONDITION_RAW_BYTES);

    /* byte-at-a-time reads give the same stream, and used bytes are wiped */
    s.pos = 0;
    conditioned_rng_init(&c, (rng_t){ seq_read, &s }, sha256_openssl);
    for (int i = 0; i < 40; i++) CHECK(rng_read(&r, &got[i], 1));
    CHECK(memcmp(got, want[0], 32) == 0 && memcmp(got + 32, want[1], 8) == 0);
    CHECK(c.avail == 24 && all_zero(c.block, 8));

    /* wipe discards buffered output: next read starts a fresh block */
    conditioned_rng_wipe(&c);
    CHECK(c.avail == 0 && all_zero(c.block, sizeof c.block));
    CHECK(rng_read(&r, got, 1) && s.pos == 3 * CONDITION_RAW_BYTES);

    /* extra data is appended to the hash input */
    uint32_t cycles = 0xDEADBEEF;
    memcpy(in, raw, CONDITION_RAW_BYTES);
    memcpy(in + CONDITION_RAW_BYTES, &cycles, 4);
    SHA256(in, sizeof in, want[0]);
    s.pos = 0;
    conditioned_rng_init(&c, (rng_t){ seq_read, &s }, sha256_openssl);
    conditioned_rng_set_extra(&c, &cycles, sizeof cycles);
    CHECK(rng_read(&r, got, 32) && memcmp(got, want[0], 32) == 0);
    conditioned_rng_wipe(&c);
    CHECK(c.extra_len == 0 && all_zero(c.extra, sizeof c.extra));

    /* raw source runs dry: output wiped, failure latches */
    seq_t short_src = { raw, CONDITION_RAW_BYTES + 10, 0 };
    conditioned_rng_init(&c, (rng_t){ seq_read, &short_src }, sha256_openssl);
    memset(got, 0xAA, sizeof got);
    CHECK(!rng_read(&r, got, 64));
    CHECK(all_zero(got, 64) && c.failed);
    short_src.pos = 0;
    CHECK(!rng_read(&r, got, 1));

    /* hash failure latches too */
    s.pos = 0;
    conditioned_rng_init(&c, (rng_t){ seq_read, &s }, sha256_broken);
    CHECK(!rng_read(&r, got, 1) && c.failed);

    /* a health failure below the conditioner still stops it */
    uint8_t k = 0x42;
    checked_rng_t chk;
    checked_rng_init(&chk, (rng_t){ const_read, &k });
    conditioned_rng_init(&c, checked_rng_as_rng(&chk), sha256_openssl);
    CHECK(!rng_read(&r, got, 1) && chk.health.failed);

    /* the conditioned stream still gives uniform dice */
    checked_rng_init(&chk, URANDOM);
    conditioned_rng_init(&c, checked_rng_as_rng(&chk), sha256_openssl);
    long counts[6] = {0};
    const long n = 600000;
    for (long i = 0; i < n; i++) {
        uint8_t v;
        if (!dice_d6(&r, &v)) { CHECK(!"d6 failed"); break; }
        counts[v - 1]++;
    }
    double chi2 = 0;
    for (int i = 0; i < 6; i++) {
        double d = counts[i] - n / 6.0;
        chi2 += d * d / (n / 6.0);
    }
    printf("  chi2 conditioned d6 = %.2f (df 5, p<0.001 at 20.5)\n", chi2);
    CHECK(chi2 < 20.5);
}

static bool word_in_list(const char *s, size_t n)
{
    for (unsigned i = 0; i < WORDLIST_SIZE; i++) {
        unsigned wl_n; const char *w = wordlist_word(&wl, i, &wl_n);
        if (wl_n == n && !memcmp(w, s, n)) return true;
    }
    return false;
}

static void test_passphrase(void)
{
    printf("passphrase generation\n");
    char out[PASSPHRASE_BUF_SIZE(PASSPHRASE_MAX_GROUPS)];

    for (int t = 0; t < 200; t++) {
        CHECK(passphrase_generate(&URANDOM, &wl, 8, out, sizeof out) == PP_OK);
        /* 8 groups, 7 hyphens between groups; each group = word + one digit.
         * Groups are split on '-' followed by... words may contain '-', so
         * split at hyphens that follow a digit. */
        int groups = 0; const char *g = out;
        for (const char *p = out; ; p++) {
            if (*p == '\0' || (*p == '-' && p > out && p[-1] >= '0' && p[-1] <= '9')) {
                size_t n = (size_t)(p - g);
                CHECK(n >= 2 && g[n-1] >= '0' && g[n-1] <= '9');
                CHECK(word_in_list(g, n - 1));
                groups++;
                if (*p == '\0') break;
                g = p + 1;
            }
        }
        CHECK(groups == 8);
        if (t < 3) printf("  sample: %s\n", out);
    }

    /* buffer too small: error and buffer wiped */
    char small[20]; memset(small, 'x', sizeof small);
    CHECK(passphrase_generate(&URANDOM, &wl, 8, small, sizeof small) == PP_ERR_BUFFER);
    for (size_t i = 0; i < sizeof small; i++) CHECK(small[i] == 0);

    /* bad args */
    CHECK(passphrase_generate(&URANDOM, &wl, 0, out, sizeof out) == PP_ERR_ARGS);
    CHECK(passphrase_generate(&URANDOM, &wl, 17, out, sizeof out) == PP_ERR_ARGS);

    /* entropy failure mid-way: nothing partial left */
    uint8_t some[20]; memset(some, 3, sizeof some);
    seq_t s = { some, sizeof some, 0 };
    rng_t r = { seq_read, &s };
    memset(out, 'x', sizeof out);
    CHECK(passphrase_generate(&r, &wl, 8, out, sizeof out) == PP_ERR_ENTROPY);
    CHECK(out[0] == 0 && out[sizeof out - 1] == 0);

    /* one group exactly */
    CHECK(passphrase_generate(&URANDOM, &wl, 1, out, sizeof out) == PP_OK);
    CHECK(strchr(out, '\0') - out >= 2);
}

/* ---------- display ---------- */

/* Column x of text row `row` as a 7-char string of '#'/'.', top row first */
static void column_art(const display_fb_t *fb, unsigned row, unsigned x, char out[8])
{
    for (int y = 0; y < 7; y++) out[y] = (fb->page[row][x] >> y) & 1 ? '#' : '.';
    out[7] = 0;
}

static bool fb_row_empty(const display_fb_t *fb, unsigned row)
{
    return all_zero(fb->page[row], DISPLAY_WIDTH);
}

/* Render `s` alone on a fresh framebuffer row and compare with `fb`. */
static bool row_shows(const display_fb_t *fb, unsigned row, const char *s)
{
    static display_fb_t ref;
    display_init(&ref, fb->rows);
    if (!display_text(&ref, row, 0, s, strlen(s))) return false;
    return memcmp(ref.page[row], fb->page[row], DISPLAY_WIDTH) == 0;
}

static void test_display(void)
{
    printf("display\n");
    static display_fb_t fb;
    char art[8];

    /* glyph pixels: 'A' column 0 is ".######" (rows 1-6), column 2 has
     * rows 0 and 3 lit */
    display_init(&fb, 8);
    CHECK(display_text(&fb, 0, 0, "A", 1));
    column_art(&fb, 0, 0, art); CHECK(strcmp(art, ".######") == 0);
    column_art(&fb, 0, 2, art); CHECK(strcmp(art, "#..#...") == 0);
    column_art(&fb, 0, 5, art); CHECK(strcmp(art, ".......") == 0);   /* gap */
    /* unknown characters render as '?' */
    display_init(&fb, 8);
    CHECK(display_text(&fb, 1, 0, "\x01", 1));
    CHECK(row_shows(&fb, 1, "?"));

    /* widths: 6 px per char, 4 px per hyphen; clipping refuses */
    CHECK(display_text_width("ab-c", 4) == 22);
    CHECK(display_text_width("abcdefghi1-abcdefghi1-", 22) == 128);
    display_init(&fb, 8);
    CHECK(display_text(&fb, 0, 2, "abcdefghijklmnopqrstu", 21));        /* 126 px */
    CHECK(!display_text(&fb, 1, 3, "abcdefghijklmnopqrstu", 21));
    CHECK(!display_text(&fb, 8, 0, "a", 1));                            /* bad row */
    CHECK(fb_row_empty(&fb, 1));

    /* 8 groups on 128x64: one per row, with hyphens, hyphenated word intact */
    const char *p8 = "ramrod7-t-shirt8-sworn3-outtakes9-brunt2-finless8-clump5-zoom2";
    CHECK(display_passphrase(&fb, p8));
    CHECK(row_shows(&fb, 0, "ramrod7-"));
    CHECK(row_shows(&fb, 1, "t-shirt8-"));
    CHECK(row_shows(&fb, 7, "zoom2"));

    /* 3 groups: centred vertically (rows 2-4) */
    CHECK(display_passphrase(&fb, "abc1-def2-ghi3"));
    CHECK(fb_row_empty(&fb, 1) && row_shows(&fb, 2, "abc1-") &&
          row_shows(&fb, 4, "ghi3") && fb_row_empty(&fb, 5));

    /* 8 longest groups on 128x32: two per row, filling exactly 128 px */
    display_init(&fb, 4);
    const char *w = "abcdefghi1-";
    char p8l[8 * 11 + 1] = "";
    for (int i = 0; i < 8; i++) strcat(p8l, w);
    p8l[strlen(p8l) - 1] = 0;                                    /* drop last '-' */
    CHECK(display_passphrase(&fb, p8l));
    CHECK(row_shows(&fb, 0, "abcdefghi1-abcdefghi1-"));
    CHECK(row_shows(&fb, 3, "abcdefghi1-abcdefghi1"));
    /* 9 groups don't fit in 4 rows: refused, screen left clear */
    char p9[9 * 11 + 1] = "";
    for (int i = 0; i < 9; i++) strcat(p9, "ab1-");
    p9[strlen(p9) - 1] = 0;
    CHECK(!display_passphrase(&fb, p9));
    CHECK(all_zero(&fb.page[0][0], sizeof fb.page));

    /* 16 groups on 128x64: two per row */
    display_init(&fb, 8);
    char p16[16 * 5 + 1] = "";
    for (int i = 0; i < 16; i++) strcat(p16, "abc1-");
    p16[strlen(p16) - 1] = 0;
    CHECK(display_passphrase(&fb, p16));
    CHECK(row_shows(&fb, 7, "abc1-abc1"));

    /* malformed input: refused and cleared */
    CHECK(!display_passphrase(&fb, ""));
    CHECK(!display_passphrase(&fb, "abc1-def"));                  /* no final digit */
    CHECK(!display_passphrase(&fb, "abcdefghijklmnopqrstuvwxyz1")); /* too wide */
    CHECK(all_zero(&fb.page[0][0], sizeof fb.page));

    /* real passphrases always fit 128x64 and 128x32 */
    bool ok = true;
    char out[PASSPHRASE_BUF_SIZE(8)];
    for (int i = 0; i < 2000 && ok; i++) {
        ok = passphrase_generate(&URANDOM, &wl, 8, out, sizeof out) == PP_OK;
        display_init(&fb, 8); ok = ok && display_passphrase(&fb, out);
        display_init(&fb, 4); ok = ok && display_passphrase(&fb, out);
    }
    CHECK(ok);

    /* messages: centred both ways; too many lines refused */
    display_init(&fb, 8);
    const char *msg[] = { "Press", "button" };
    CHECK(display_message(&fb, msg, 2));
    CHECK(fb_row_empty(&fb, 2) && !fb_row_empty(&fb, 3) && !fb_row_empty(&fb, 4));
    CHECK(fb.page[3][(128 - 30) / 2 - 1] == 0 && fb.page[3][(128 - 30) / 2] != 0);
    const char *nine[9] = { "a", "a", "a", "a", "a", "a", "a", "a", "a" };
    CHECK(!display_message(&fb, nine, 9));

    /* wipe clears everything */
    CHECK(display_passphrase(&fb, p8));
    display_wipe(&fb);
    CHECK(all_zero(&fb.page[0][0], sizeof fb.page));
}

int main(void)
{
    urandom = fopen("/dev/urandom", "rb");
    if (!urandom) { perror("urandom"); return 2; }
    test_wordlist();
    test_dice();
    test_health();
    test_condition();
    test_passphrase();
    test_display();
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
