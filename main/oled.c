#include "oled.h"

#include "sdkconfig.h"

#if CONFIG_DW_DISPLAY   /* the whole file */

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "rng.h"

#define I2C_HZ      400000
#define I2C_TIMEOUT 100           /* ms */

#define CTRL_CMD    0x00          /* control byte: commands follow */
#define CTRL_DATA   0x40          /* control byte: display data follows */

/* SH1106 has 132 columns of RAM; the visible 128 start at column 2 */
#if CONFIG_DW_DISPLAY_SH1106
#define COL_OFFSET 2
#else
#define COL_OFFSET 0
#endif

#if CONFIG_DW_DISPLAY_ROTATE
#define SEG_REMAP 0xA0
#define COM_SCAN  0xC0
#else
#define SEG_REMAP 0xA1
#define COM_SCAN  0xC8
#endif

#define HEIGHT CONFIG_DW_DISPLAY_HEIGHT

#define SCAN_TIMEOUT 20           /* ms per address */

static const char *TAG = "oled";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

/* Diagnostic for when the display doesn't answer: log every address on
 * the bus that does, in the 7-bit form menuconfig takes and the 8-bit form
 * modules often print. */
static void scan_bus(void)
{
    unsigned found = 0;
    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_bus, addr, SCAN_TIMEOUT) == ESP_OK) {
            ESP_LOGW(TAG, "I2C scan: device at 0x%02x (8-bit form 0x%02x)",
                     addr, addr << 1);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGW(TAG, "I2C scan: no devices on SDA GPIO%d / SCL GPIO%d; "
                 "check wiring and power, and that the module is in I2C mode",
                 CONFIG_DW_DISPLAY_SDA, CONFIG_DW_DISPLAY_SCL);
    }
}

static esp_err_t send_cmds(const uint8_t *cmds, size_t n)
{
    uint8_t buf[1 + 32];
    if (n > sizeof(buf) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = CTRL_CMD;
    memcpy(&buf[1], cmds, n);
    return i2c_master_transmit(s_dev, buf, n + 1, I2C_TIMEOUT);
}

esp_err_t oled_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,                          /* any free port */
        .sda_io_num = CONFIG_DW_DISPLAY_SDA,
        .scl_io_num = CONFIG_DW_DISPLAY_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,    /* most modules have their own */
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        return err;
    }
    if ((err = i2c_master_probe(s_bus, CONFIG_DW_DISPLAY_I2C_ADDR, I2C_TIMEOUT)) != ESP_OK) {
        scan_bus();
        goto fail;
    }
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_DW_DISPLAY_I2C_ADDR,
        .scl_speed_hz = I2C_HZ,
    };
    if ((err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev)) != ESP_OK) {
        goto fail;
    }

    const uint8_t init[] = {
        0xAE,                         /* display off */
        0xD5, 0x80,                   /* clock divide / oscillator */
        0xA8, HEIGHT - 1,             /* multiplex ratio */
        0xD3, 0x00,                   /* display offset */
        0x40,                         /* start line 0 */
#if CONFIG_DW_DISPLAY_SH1106
        0xAD, 0x8B,                   /* DC-DC converter on */
#else
        0x8D, 0x14,                   /* charge pump on */
        0x20, 0x02,                   /* page addressing mode */
#endif
        SEG_REMAP, COM_SCAN,          /* orientation */
        0xDA, HEIGHT == 64 ? 0x12 : 0x02,  /* COM pins layout */
        0x81, 0x80,                   /* contrast */
        0xD9, 0xF1,                   /* pre-charge period */
        0xDB, 0x40,                   /* VCOMH level */
        0xA4,                         /* show RAM contents */
        0xA6,                         /* normal, not inverted */
    };
    if ((err = send_cmds(init, sizeof(init))) != ESP_OK) {
        goto fail;
    }

    /* blank the controller's RAM before switching the panel on */
    static display_fb_t blank;
    display_init(&blank, HEIGHT / 8);
    if ((err = oled_flush(&blank)) != ESP_OK) {
        goto fail;
    }
    const uint8_t on[] = { 0xAF };
    if ((err = send_cmds(on, sizeof(on))) != ESP_OK) {
        goto fail;
    }
    return ESP_OK;

fail:
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    i2c_del_master_bus(s_bus);
    s_bus = NULL;
    return err;
}

esp_err_t oled_flush(const display_fb_t *fb)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    uint8_t buf[1 + DISPLAY_WIDTH];

    for (unsigned p = 0; p < fb->rows && err == ESP_OK; p++) {
        const uint8_t addr[] = {
            0xB0 | p,                          /* page */
            0x00 | (COL_OFFSET & 0x0F),        /* column, low nibble */
            0x10 | (COL_OFFSET >> 4),          /* column, high nibble */
        };
        err = send_cmds(addr, sizeof(addr));
        if (err == ESP_OK) {
            buf[0] = CTRL_DATA;
            memcpy(&buf[1], fb->page[p], DISPLAY_WIDTH);
            err = i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT);
        }
    }
    secure_zero(buf, sizeof(buf));   /* held passphrase pixels */
    return err;
}

#endif /* CONFIG_DW_DISPLAY */
