/**
 * @file health.c
 * @brief Run continuous repetition/proportion checks on source bytes.
 *
 * health_feed() implements the streaming state machine. checked_read() adapts
 * it to rng_t and enforces all-or-fail output plus a sticky failure latch.
 * Counters survive read boundaries, so changing read chunk sizes does not
 * reset a run or an adaptive-proportion window. These tests detect gross
 * failures; they do not measure or certify the source's entropy.
 */
#include "health.h"

#include <string.h>

void health_init(health_t *h)
{
    memset(h, 0, sizeof(*h));
}

/* All test failures use static reason strings; callers may retain the pointer. */
static bool fail(health_t *h, const char *why)
{
    h->failed = true;
    h->fail_reason = why;
    return false;
}

bool health_feed(health_t *h, uint8_t s)
{
    if (h->failed) {
        return false;
    }

    /* Count the reference byte itself in both tests. Starting at one makes
     * the cutoff mean total occurrences, not repeats after the first byte. */
    if (!h->started) {
        h->started   = true;
        h->rct_last  = s;
        h->rct_count = 1;
        h->apt_ref   = s;
        h->apt_count = 1;
        h->apt_pos   = 1;
        return true;
    }

    /* Repetition Count Test */
    if (s == h->rct_last) {
        if (++h->rct_count >= HEALTH_RCT_CUTOFF) {
            return fail(h, "repetition count test");
        }
    } else {
        h->rct_last  = s;
        h->rct_count = 1;
    }

    /* Adaptive Proportion Test: compare with one reference byte per window.
     * Start a fresh window on the sample AFTER the previous window fills. */
    if (h->apt_pos >= HEALTH_APT_WINDOW) {
        /* start a new window with this sample as reference */
        h->apt_ref   = s;
        h->apt_count = 1;
        h->apt_pos   = 1;
    } else {
        if (s == h->apt_ref && ++h->apt_count >= HEALTH_APT_CUTOFF) {
            return fail(h, "adaptive proportion test");
        }
        h->apt_pos++;
    }
    return true;
}

/* Read first, then validate the whole returned range before declaring success.
 * This prevents callers from accepting bytes preceding a bad sample in the
 * same read. A failed underlying read may have partially filled out. */
static bool checked_read(void *ctx, uint8_t *out, size_t len)
{
    checked_rng_t *c = ctx;

    if (c->health.failed) {
        secure_zero(out, len);
        return false;
    }
    if (!rng_read(&c->inner, out, len)) {
        secure_zero(out, len);
        c->health.failed = true;
        c->health.fail_reason = "entropy source unavailable";
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!health_feed(&c->health, out[i])) {
            secure_zero(out, len);
            return false;
        }
    }
    return true;
}

/* Wrapping a provider does not initialize or take ownership of its context. */
void checked_rng_init(checked_rng_t *c, rng_t inner)
{
    c->inner = inner;
    health_init(&c->health);
}

/* The returned handle borrows c; do not let it outlive that state object. */
rng_t checked_rng_as_rng(checked_rng_t *c)
{
    return (rng_t){ .read = checked_read, .ctx = c };
}

/* Exercise the same checked-read path used at runtime, rather than a separate
 * test implementation. Keep health history when discarding startup samples. */
bool checked_rng_startup(checked_rng_t *c)
{
    uint8_t buf[64];
    for (unsigned done = 0; done < HEALTH_STARTUP_SAMPLES; done += sizeof(buf)) {
        if (!checked_read(c, buf, sizeof(buf))) {
            return false;
        }
    }
    secure_zero(buf, sizeof(buf));
    return true;
}
