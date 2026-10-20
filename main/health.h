/*
 * Continuous health tests modelled on NIST SP 800-90B section 4.4:
 *   - Repetition Count Test (RCT): catches a stuck source
 *   - Adaptive Proportion Test (APT): catches a heavily biased source
 *
 * Samples are bytes. Cut-offs assume a claimed min-entropy floor of
 * H = 4 bits per byte and a false-alarm probability of about 2^-20:
 *   RCT cutoff = 1 + ceil(20 / H)                       = 6
 *   APT cutoff = critical binomial value, W = 512, p = 2^-H = 63
 *
 * Note: esp_random() output is already post-processed by the hardware, so
 * these tests only catch gross failures (stuck, constant, badly biased).
 * They are a tripwire, not proof of entropy.
 *
 * A failure latches: once tripped, every later read fails until reset.
 */
#pragma once

#include "rng.h"

#define HEALTH_RCT_CUTOFF      6u
#define HEALTH_APT_WINDOW      512u
#define HEALTH_APT_CUTOFF      63u
#define HEALTH_STARTUP_SAMPLES 1024u

typedef struct {
    /* RCT state */
    uint8_t  rct_last;
    uint32_t rct_count;
    /* APT state */
    uint8_t  apt_ref;
    uint32_t apt_count;
    uint32_t apt_pos;
    /* general */
    bool     started;
    bool     failed;
    const char *fail_reason;
} health_t;

void health_init(health_t *h);

/* Feed one sample. Returns false once a test has failed (latched). */
bool health_feed(health_t *h, uint8_t sample);

/* A health-checked wrapper around another rng_t. */
typedef struct {
    rng_t    inner;
    health_t health;
} checked_rng_t;

void checked_rng_init(checked_rng_t *c, rng_t inner);

/* rng_t view of the checked source. */
rng_t checked_rng_as_rng(checked_rng_t *c);

/* Draw and discard HEALTH_STARTUP_SAMPLES bytes through the tests.
 * Returns false if the source fails at startup. */
bool checked_rng_startup(checked_rng_t *c);
