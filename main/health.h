/**
 * @file health.h
 * @brief Streaming failure detectors and an RNG adapter that enforces them.
 *
 * Continuous health tests modelled on NIST SP 800-90B section 4.4:
 *   - Repetition Count Test (RCT): catches a stuck source
 *   - Adaptive Proportion Test (APT): catches a heavily biased source
 *
 * Samples are bytes. Cut-offs use an assumed min-entropy floor of
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

/**
 * Caller-owned streaming state. Counters continue across reads and requests.
 * APT compares against the first byte of each non-overlapping window, rather
 * than building a histogram of every value. This state is not thread-safe.
 */
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
    const char *fail_reason; /* Borrowed static string when failed; otherwise null. */
} health_t;

/** Reset all counters and the failure latch in valid caller-owned h. */
void health_init(health_t *h);

/**
 * Feed one byte, updating both tests. The first byte initializes their state.
 * Returns false on the byte reaching a cutoff, and on every call after failure.
 * Initialize h first; inspect fail_reason only after a false result.
 */
bool health_feed(health_t *h, uint8_t sample);

/** Borrowed upstream source plus owned health state; initialize before use. */
typedef struct {
    rng_t    inner;
    health_t health;
} checked_rng_t;

/**
 * Initialize c around inner without reading or initializing the upstream source.
 * Referenced source state and callbacks must remain valid while c is used.
 * Reinitializing c resets only these health tests, not the underlying source.
 */
void checked_rng_init(checked_rng_t *c, rng_t inner);

/**
 * Return a borrowed view of c. Each read obtains a complete upstream buffer,
 * then tests every byte. Any source/test failure erases the entire requested
 * output range and latches failure, so no partial result can be accepted.
 */
rng_t checked_rng_as_rng(checked_rng_t *c);

/**
 * Read and discard HEALTH_STARTUP_SAMPLES bytes in small stack-buffer chunks.
 * Call after checked_rng_init() and before accepting generation requests.
 * Returns false on source/test failure. Successful startup retains the test
 * counters so monitoring continues across the startup/runtime boundary.
 * This checks gross failures; it does not establish an entropy lower bound.
 */
bool checked_rng_startup(checked_rng_t *c);
