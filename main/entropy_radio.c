#include "entropy_radio.h"

#include <stdatomic.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "entropy";

#define STA_STARTED_BIT BIT0
#define STA_STOPPED_BIT BIT1

static EventGroupHandle_t s_events;
static atomic_bool s_radio_running = false;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base != WIFI_EVENT) {
        return;
    }
    if (id == WIFI_EVENT_STA_START) {
        atomic_store(&s_radio_running, true);
        xEventGroupSetBits(s_events, STA_STARTED_BIT);
    } else if (id == WIFI_EVENT_STA_STOP) {
        /* radio gone: the RNG would silently degrade to pseudo-random */
        atomic_store(&s_radio_running, false);
        xEventGroupSetBits(s_events, STA_STOPPED_BIT);
    }
}

esp_err_t entropy_radio_start(void)
{
    s_events = xEventGroupCreate();
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err;

    if ((err = esp_netif_init()) != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if ((err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          wifi_event_handler, NULL)) != ESP_OK) {
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;                       /* never persist Wi-Fi config */
    if ((err = esp_wifi_init(&cfg)) != ESP_OK) {
        return err;
    }
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) {
        return err;
    }
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) {
        return err;
    }
    /* No esp_wifi_connect() and no scan: the radio receives but the device
     * does not associate with anything. */
    if ((err = esp_wifi_start()) != ESP_OK) {
        return err;
    }
    if ((err = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK) {
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(s_events, STA_STARTED_BIT, pdFALSE,
                                           pdTRUE, pdMS_TO_TICKS(5000));
    if (!(bits & STA_STARTED_BIT)) {
        ESP_LOGE(TAG, "radio did not report start");
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "radio running (station mode, not connected)");
    return ESP_OK;
}

static bool radio_read(void *ctx, uint8_t *out, size_t len)
{
    (void)ctx;
    if (!atomic_load(&s_radio_running)) {
        return false;
    }
    esp_fill_random(out, len);
    /* re-check: if the radio stopped during the read, distrust the output */
    return atomic_load(&s_radio_running);
}

rng_t entropy_radio_rng(void)
{
    return (rng_t){ .read = radio_read, .ctx = NULL };
}
