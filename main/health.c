#include "health.h"

#include <string.h>

void health_init(health_t *h)
{
    memset(h, 0, sizeof(*h));
}

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

    /* Adaptive Proportion Test */
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

void checked_rng_init(checked_rng_t *c, rng_t inner)
{
    c->inner = inner;
    health_init(&c->health);
}

rng_t checked_rng_as_rng(checked_rng_t *c)
{
    return (rng_t){ .read = checked_read, .ctx = c };
}

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
