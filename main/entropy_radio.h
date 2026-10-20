/*
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

/* Start the radio and wait until it is running. */
esp_err_t entropy_radio_start(void);

/* rng_t backed by esp_fill_random(); reads fail if the radio is not running. */
rng_t entropy_radio_rng(void);
