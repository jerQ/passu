/*
 * passu - ESP32 diceware passphrase generator, proof of concept.
 *
 * Entropy: ESP32 hardware RNG with the Wi-Fi radio running (never connected),
 *          health-checked, then hashed with SHA-256 (condition.c).
 * Words:   EFF large wordlist, embedded in flash and validated at boot.
 * Output:  passphrases printed to the serial console and/or shown on an
 *          optional I2C OLED (display.c, oled.c).
 * Trigger: press the button (default GPIO0, the BOOT button on most dev
 *          boards) or press Enter in the serial monitor.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "driver/gpio.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

#include "condition.h"
#include "display.h"
#include "entropy_radio.h"
#include "health.h"
#include "oled.h"
#include "passphrase.h"
#include "wordlist.h"

static const char *TAG = "passu";

extern const char wordlist_start[] asm("_binary_eff_large_wordlist_txt_start");
extern const char wordlist_end[]   asm("_binary_eff_large_wordlist_txt_end");

#define DEBOUNCE_MS 300

/* Print passphrases over serial: always without a display, optional with one */
#if !CONFIG_DW_DISPLAY || CONFIG_DW_SERIAL_PASSPHRASE
#define SERIAL_PASSPHRASE 1
#else
#define SERIAL_PASSPHRASE 0
#endif

typedef enum { TRIGGER_BUTTON, TRIGGER_SERIAL } trigger_type_t;

typedef struct {
    trigger_type_t type;
    uint32_t       cycles;   /* CPU cycle count at the press, mixed into the hash */
} trigger_t;

static wordlist_t    s_wordlist;      /* ~39 KB index, keep off the stack */
static checked_rng_t s_checked;
static conditioned_rng_t s_cond;
static QueueHandle_t s_triggers;

/* ---------- display ---------- */

#if CONFIG_DW_DISPLAY

#if CONFIG_DW_GROUPS > 2 * (CONFIG_DW_DISPLAY_HEIGHT / 8)
#error "Too many groups for this display: at most 2 per 8-pixel text row"
#endif

#define DISPLAY_CLEAR_MS (CONFIG_DW_DISPLAY_CLEAR_S * 1000)

#if CONFIG_DW_DISPLAY_SH1106
#define DISPLAY_CONTROLLER "SH1106"
#else
#define DISPLAY_CONTROLLER "SSD1306"
#endif

static display_fb_t s_fb;           /* 1 KB, keep off the stack */
static bool s_display_ok;
static bool s_crashed;              /* shown on the idle screen until reset */

/* Draw, send, and wipe the framebuffer at once: the display keeps the
 * image, so no copy of a passphrase needs to stay in RAM. */
static bool show_lines(const char *const *lines, unsigned n)
{
    bool ok = s_display_ok && display_message(&s_fb, lines, n) &&
              oled_flush(&s_fb) == ESP_OK;
    display_wipe(&s_fb);
    return ok;
}

static void display_show_idle(void)
{
    char bits[24];
    snprintf(bits, sizeof(bits), "%d groups, ~%d bits", CONFIG_DW_GROUPS,
             (CONFIG_DW_GROUPS * 1625) / 100);
    const char *lines[] = { "Press the button", "for a passphrase", bits,
                            "Restarted after crash" };
    show_lines(lines, s_crashed ? 4 : 3);
}

static void display_show_error(const char *line1, const char *line2)
{
    const char *lines[] = { line1, line2 };
    show_lines(lines, 2);
}

static bool display_show_passphrase(const char *phrase)
{
    bool ok = s_display_ok && display_passphrase(&s_fb, phrase) &&
              oled_flush(&s_fb) == ESP_OK;
    display_wipe(&s_fb);
    return ok;
}

static void display_start(void)
{
    display_init(&s_fb, CONFIG_DW_DISPLAY_HEIGHT / 8);
    esp_err_t err = oled_init();
    s_display_ok = err == ESP_OK;
    if (s_display_ok) {
        printf("Display: %s 128x%d at I2C 0x%02x (8-bit 0x%02x), SDA GPIO%d, "
               "SCL GPIO%d\n",
               DISPLAY_CONTROLLER,
               CONFIG_DW_DISPLAY_HEIGHT, CONFIG_DW_DISPLAY_I2C_ADDR,
               CONFIG_DW_DISPLAY_I2C_ADDR << 1, CONFIG_DW_DISPLAY_SDA,
               CONFIG_DW_DISPLAY_SCL);
        const char *lines[] = { "Starting..." };
        show_lines(lines, 1);
        return;
    }
    ESP_LOGE(TAG, "no display at I2C address 0x%02x (SDA GPIO%d, SCL GPIO%d): %s",
             CONFIG_DW_DISPLAY_I2C_ADDR, CONFIG_DW_DISPLAY_SDA,
             CONFIG_DW_DISPLAY_SCL, esp_err_to_name(err));
#if !SERIAL_PASSPHRASE
    /* no display and serial printing off: passphrases would go nowhere */
    printf("\nDEVICE-ERROR: display not found, halting\n");
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
#endif
}

#else  /* no display */

#define DISPLAY_CLEAR_MS 0
static bool s_crashed;
static void display_show_idle(void) {}
static void display_show_error(const char *a, const char *b) { (void)a; (void)b; }
static bool display_show_passphrase(const char *p) { (void)p; return false; }
static void display_start(void) {}

#endif

/* ---------- wordlist ---------- */

static void wordlist_load_or_halt(void)
{
    size_t len = (size_t)(wordlist_end - wordlist_start);
    /* EMBED_TXTFILES appends a NUL terminator that is counted in the size */
    if (len > 0 && wordlist_start[len - 1] == '\0') {
        len--;
    }

    /* PSA crypto API: available in Mbed TLS 3.x (IDF 5) and 4.x (IDF 6) */
    uint8_t hash[32];
    size_t hash_len = 0;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)wordlist_start, len,
                         hash, sizeof(hash), &hash_len) != PSA_SUCCESS ||
        hash_len != sizeof(hash)) {
        ESP_LOGE(TAG, "SHA-256 of wordlist failed");
        goto halt;
    }
    char hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&hex[i * 2], "%02x", hash[i]);
    }
    printf("Wordlist: %u bytes, SHA-256 %s\n", (unsigned)len, hex);

    const char *expected = CONFIG_DW_WORDLIST_SHA256;
    if (expected[0] != '\0' && strcasecmp(expected, hex) != 0) {
        ESP_LOGE(TAG, "wordlist hash mismatch (expected %s)", expected);
        goto halt;
    }
    if (expected[0] == '\0') {
        ESP_LOGW(TAG, "wordlist hash not pinned (set DW_WORDLIST_SHA256 in menuconfig)");
    }

    unsigned bad_line = 0;
    wordlist_err_t err = wordlist_init(&s_wordlist, wordlist_start, len, &bad_line);
    if (err != WL_OK) {
        ESP_LOGE(TAG, "wordlist invalid at line %u: %s", bad_line,
                 wordlist_strerror(err));
        goto halt;
    }
    printf("Wordlist: %u entries validated, longest word %u chars\n",
           WORDLIST_SIZE, s_wordlist.max_len);
    return;

halt:
    display_show_error("Wordlist invalid", "See serial output");
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

/* ---------- triggers ---------- */

static void IRAM_ATTR button_isr(void *arg)
{
    (void)arg;
    trigger_t t = { TRIGGER_BUTTON, esp_cpu_get_cycle_count() };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_triggers, &t, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static void button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_DW_BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,   /* button pulls to GND */
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_DW_BUTTON_GPIO, button_isr, NULL));
}

#if CONFIG_DW_SERIAL_TRIGGER
static void serial_task(void *arg)
{
    (void)arg;
    for (;;) {
        int c = fgetc(stdin);   /* non-blocking with the default console */
        if (c == '\r' || c == '\n') {
            trigger_t t = { TRIGGER_SERIAL, esp_cpu_get_cycle_count() };
            xQueueSend(s_triggers, &t, 0);
        } else if (c == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}
#endif

/* ---------- crash reporting ---------- */

/* Crashes reboot silently (CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT) so no
 * register dump reaches the serial port; say after the reboot that one
 * happened, without any memory contents. */
static void report_previous_crash(void)
{
    const char *why;
    switch (esp_reset_reason()) {
    case ESP_RST_PANIC:    why = "panic"; break;
    case ESP_RST_INT_WDT:  why = "interrupt watchdog"; break;
    case ESP_RST_TASK_WDT: why = "task watchdog"; break;
    case ESP_RST_WDT:      why = "watchdog"; break;
    case ESP_RST_BROWNOUT: why = "brownout"; break;
    default:               return;
    }
    s_crashed = true;
    printf("\nDEVICE-ERROR: previous run crashed (%s), device restarted\n", why);
}

/* ---------- generation ---------- */

static bool sha256_psa(const uint8_t *data, size_t len,
                       uint8_t out[CONDITION_OUT_BYTES])
{
    size_t out_len = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, data, len, out,
                            CONDITION_OUT_BYTES, &out_len) == PSA_SUCCESS &&
           out_len == CONDITION_OUT_BYTES;
}

static void generator_task(void *arg)
{
    (void)arg;
    const rng_t rng = conditioned_rng_as_rng(&s_cond);
    char phrase[PASSPHRASE_BUF_SIZE(PASSPHRASE_MAX_GROUPS)];
    TickType_t last_button = 0;
    bool shown = false;     /* a passphrase is on the display */

    /* Unbuffered, so no copy of the passphrase lingers in stdout's buffer
     * after `phrase` is wiped. */
    setvbuf(stdout, NULL, _IONBF, 0);

    for (;;) {
        trigger_t t;
        /* while a passphrase is shown, clear it after a timeout */
        TickType_t wait = shown ? pdMS_TO_TICKS(DISPLAY_CLEAR_MS) : portMAX_DELAY;
        if (xQueueReceive(s_triggers, &t, wait) != pdTRUE) {
            display_show_idle();
            shown = false;
            continue;
        }

        if (t.type == TRIGGER_BUTTON) {
            TickType_t now = xTaskGetTickCount();
            if (last_button != 0 && now - last_button < pdMS_TO_TICKS(DEBOUNCE_MS)) {
                continue;   /* contact bounce */
            }
            last_button = now;
            if (shown) {
                /* a press while a passphrase is shown only clears it */
                display_show_idle();
                shown = false;
                secure_zero(&t, sizeof(t));
                continue;
            }
        }

        bool tagged = t.type == TRIGGER_SERIAL;
        conditioned_rng_set_extra(&s_cond, &t.cycles, sizeof(t.cycles));
        pp_err_t err = passphrase_generate(&rng, &s_wordlist, CONFIG_DW_GROUPS,
                                           phrase, sizeof(phrase));
        conditioned_rng_wipe(&s_cond);
        secure_zero(&t, sizeof(t));
        /* A button press prints the bare passphrase, one per line, for a
         * person reading the serial monitor. A serial request (the host
         * command) gets a tagged line so it can pick the result out of the
         * stream: "PASSPHRASE: ...". Status and errors are always tagged
         * ("PASSPHRASE-DISPLAYED: ...", "PASSPHRASE-ERROR: ...") so they can
         * never pass for a passphrase. */
        shown = false;
        if (err == PP_OK) {
            shown = display_show_passphrase(phrase);
            if (!shown) {
                display_show_idle();    /* don't leave a partial image */
            }
            if (SERIAL_PASSPHRASE) {
                printf(tagged ? "\nPASSPHRASE: %s\n" : "%s\n", phrase);
            } else if (shown) {
                printf("\nPASSPHRASE-DISPLAYED: shown on the device display\n");
            } else {
                printf("\nPASSPHRASE-ERROR: display failure\n");
            }
        } else if (s_checked.health.failed) {
            printf("\nPASSPHRASE-ERROR: entropy health failure (%s), reset "
                   "the device\n", s_checked.health.fail_reason);
            display_show_error("Entropy failure", "Reset the device");
        } else {
            printf("\nPASSPHRASE-ERROR: %s\n", passphrase_strerror(err));
            display_show_error("Generation failed", "Press to try again");
        }
        fflush(stdout);
        secure_zero(phrase, sizeof(phrase));
    }
}

void app_main(void)
{
    report_previous_crash();
    printf("\npassu - ESP32 diceware passphrase generator (proof of concept)\n");
    display_start();

    wordlist_load_or_halt();

    ESP_ERROR_CHECK(entropy_radio_start());
    checked_rng_init(&s_checked, entropy_radio_rng());
    if (!checked_rng_startup(&s_checked)) {
        ESP_LOGE(TAG, "entropy startup self-test failed: %s",
                 s_checked.health.fail_reason);
        display_show_error("Entropy failure", "Reset the device");
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    /* psa_crypto_init() already ran in wordlist_load_or_halt() */
    conditioned_rng_init(&s_cond, checked_rng_as_rng(&s_checked), sha256_psa);
    printf("Entropy: radio running, startup health test passed\n");

    s_triggers = xQueueCreate(8, sizeof(trigger_t));
    configASSERT(s_triggers);

    display_show_idle();     /* before the generator task owns the display */
    button_init();
    xTaskCreate(generator_task, "generator", 4096, NULL, 5, NULL);
#if CONFIG_DW_SERIAL_TRIGGER
    xTaskCreate(serial_task, "serial", 2560, NULL, 4, NULL);
    printf("Press the button on GPIO%d or Enter for a passphrase "
           "(%d groups, ~%d bits)\n", CONFIG_DW_BUTTON_GPIO, CONFIG_DW_GROUPS,
           (CONFIG_DW_GROUPS * 1625) / 100);
#else
    printf("Press the button on GPIO%d for a passphrase\n", CONFIG_DW_BUTTON_GPIO);
#endif
}
