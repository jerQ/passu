/*
 * I2C driver for 128-pixel-wide monochrome OLEDs: SSD1306 (and SSD1309,
 * which accepts the same commands) and SH1106. Chosen in menuconfig:
 * controller, 128x64 or 128x32, I2C address and pins, 180-degree rotation.
 */
#pragma once

#include "display.h"
#include "esp_err.h"

/* Set up the I2C bus, check the display answers, and initialise it with a
 * blank screen. Returns ESP_ERR_NOT_FOUND if nothing answers. */
esp_err_t oled_init(void);

/* Send the whole framebuffer to the display. */
esp_err_t oled_flush(const display_fb_t *fb);
