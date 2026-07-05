#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_events.h"
#include "ble_transport.h"
#include "config.h"
#include "frame_reassembly.h"
#include "protocol_decoder.h"

static const char *TAG = "opelxid";

typedef struct {
    size_t len;
    uint8_t data[OPX_MAX_BLE_WRITE_SIZE];
} rx_chunk_t;

static QueueHandle_t s_rx_queue;

static void on_protocol_frame(const uint8_t *frame, size_t len, void *ctx)
{
    (void)ctx;
    protocol_decoder_process(frame, len);
}

static void protocol_task(void *arg)
{
    (void)arg;

    frame_reassembly_t reassembly;
    frame_reassembly_init(&reassembly);

    rx_chunk_t chunk;
    while (true) {
        if (xQueueReceive(s_rx_queue, &chunk, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        frame_reassembly_push(&reassembly, chunk.data, chunk.len, on_protocol_frame, NULL);
    }
}

static void ble_rx_cb(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;

    if (data == NULL || len == 0 || len > OPX_MAX_BLE_WRITE_SIZE) {
        return;
    }

    rx_chunk_t chunk = {
        .len = len,
    };
    memcpy(chunk.data, data, len);

    if (xQueueSend(s_rx_queue, &chunk, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX queue full, dropping chunk len=%u", (unsigned)len);
    }
}

static void app_event_logger(const app_event_t *event, void *ctx)
{
    (void)ctx;

    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case APP_EVENT_AUDIO_METADATA:
        ESP_LOGI(TAG,
                 "audio state=%d track='%s' artist='%s' updated_ms=%" PRId64,
                 (int)event->data.audio.playback_state,
                 event->data.audio.track,
                 event->data.audio.artist,
                 event->data.audio.updated_ms);
        break;
    case APP_EVENT_TIME_SYNC:
        ESP_LOGI(TAG,
                 "time sync success=%d epoch=%" PRId64,
                 (int)event->data.time.success,
                 (int64_t)event->data.time.epoch_seconds);
        break;
    default:
        break;
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());

    app_events_init();
    app_events_register_callback(app_event_logger, NULL);

    s_rx_queue = xQueueCreate(CONFIG_OPX_RX_QUEUE_DEPTH, sizeof(rx_chunk_t));
    ESP_ERROR_CHECK(s_rx_queue == NULL ? ESP_FAIL : ESP_OK);

    BaseType_t ok = xTaskCreate(protocol_task,
                                "protocol_task",
                                OPX_PROTOCOL_TASK_STACK,
                                NULL,
                                OPX_PROTOCOL_TASK_PRIORITY,
                                NULL);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_FAIL);

    ESP_ERROR_CHECK(ble_transport_start(CONFIG_OPX_DEVICE_NAME, ble_rx_cb, NULL));

    ESP_LOGI(TAG, "Phase 1 started: RX-only time and audio metadata sync");
}
