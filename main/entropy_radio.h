/**
 * @file entropy_radio.h
 * @brief Platform adapter exposing the radio-backed ESP32 RNG as rng_t.
 *
 * ESP32 hardware RNG, kept in true-random mode by the Wi-Fi radio.
 *
 * ESP-IDF documents that esp_random()/esp_fill_random() return true random
 * numbers only while the RF subsystem (Wi-Fi or Bluetooth) is enabled;
 * otherwise the output is pseudo-random. This module starts Wi-Fi in station
 * mode without connecting or scanning, which keeps RF noise feeding the RNG
 * without the device associating with any network.
 */
#pragma once

#include "esp_err.h"
#include "rng.h"

/**
 * Initialize networking/event handling and Wi-Fi, then await station start.
 * Call once during boot from task context before using the returned RNG view.
 * Configures RAM-only Wi-Fi state, station mode, and power saving off; never
 * calls connect or scan. The start-event wait has a five-second timeout.
 * Returns ESP_OK or the first setup/timeout error. Partial setup is not rolled
 * back; this module has no stop/retry lifecycle, and app_main aborts on error.
 */
esp_err_t entropy_radio_start(void);

/**
 * Return a view of module-global state, without allocating or starting Wi-Fi.
 * Its callback checks the event-maintained running flag before and after
 * esp_fill_random(). False makes the whole requested output unusable; the
 * callback itself does not erase the buffer (the health wrapper does).
 * The flag is atomic because event callbacks and generation run in different
 * tasks. It reflects delivered Wi-Fi events, not a direct hardware status probe.
 */
rng_t entropy_radio_rng(void);
