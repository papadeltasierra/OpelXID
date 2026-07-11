#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "opel_mid.h"

static const char *TAG = "basic_text";

/*
 * Adjust these pin assignments to match your wiring.
 * All three lines must be open-drain capable GPIOs.
 */
#define PIN_SDA  GPIO_NUM_21
#define PIN_SCL  GPIO_NUM_22
#define PIN_MRQ  GPIO_NUM_23

void app_main(void)
{
    opel_mid_config_t config = {
        .pin_sda = PIN_SDA,
        .pin_scl = PIN_SCL,
        .pin_mrq = PIN_MRQ,
        .type    = OPEL_MID_TYPE_TID_10,
    };

    opel_mid_handle_t display = NULL;
    ESP_ERROR_CHECK(opel_mid_init(&config, &display));

    /* Send power-on test sequence (must be called after AA line goes high). */
    ESP_ERROR_CHECK(opel_mid_power_on(display));

    /* Symbols: all off. */
    opel_mid_symbols_t symbols = { .radio = 0, .tape = 0, .cd = 0 };

    while (1) {
        ESP_LOGI(TAG, "Sending text to display");
        ESP_ERROR_CHECK(opel_mid_send(display, "HELLO     ", &symbols));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
