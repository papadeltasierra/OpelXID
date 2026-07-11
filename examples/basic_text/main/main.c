#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "opel_mid.h"

static const char *TAG = "basic_text";

/*
 * Display type and GPIO pins are configured via SDKCONFIG.
 * Modify via: idf.py menuconfig -> OpelXID Basic Text Configuration
 */
#ifdef CONFIG_OPEL_DISPLAY_TYPE_TID_8
#define DISPLAY_TYPE OPEL_MID_TYPE_TID_8
#else
#define DISPLAY_TYPE OPEL_MID_TYPE_TID_10
#endif

#define PIN_SDA  CONFIG_OPEL_DISPLAY_PIN_SDA
#define PIN_SCL  CONFIG_OPEL_DISPLAY_PIN_SCL
#define PIN_MRQ  CONFIG_OPEL_DISPLAY_PIN_MRQ

void app_main(void)
{
    opel_mid_config_t config = {
        .pin_sda = (gpio_num_t)PIN_SDA,
        .pin_scl = (gpio_num_t)PIN_SCL,
        .pin_mrq = (gpio_num_t)PIN_MRQ,
        .type    = DISPLAY_TYPE,
    };

    opel_mid_handle_t display = NULL;
    ESP_ERROR_CHECK(opel_mid_init(&config, &display));

    /* Send power-on test sequence (must be called after AA line goes high). */
    ESP_ERROR_CHECK(opel_mid_power_on(display));

    /* Show RDS and Stereo icons active; all tape/CD icons off. */
    opel_mid_symbols_t symbols = {
        .radio = OPEL_MID_SYM_RDS | OPEL_MID_SYM_STEREO,
        .tape  = 0,
        .cd    = 0,
    };

    while (1) {
        ESP_LOGI(TAG, "Sending text to display");
        ESP_ERROR_CHECK(opel_mid_send(display, "HELLO     ", &symbols));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
