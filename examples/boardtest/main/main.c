#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "boardtest";

#define PIN_SDA CONFIG_OPEL_BOARDTEST_PIN_SDA
#define PIN_SCL CONFIG_OPEL_BOARDTEST_PIN_SCL
#define PIN_MRQ CONFIG_OPEL_BOARDTEST_PIN_MRQ

#define SDA_SCL_MRQ_TOGGLE_PERIOD_MS 1000

static void configure_pin_as_output(gpio_num_t pin)
{
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io));
}

// static int oe_enabled_to_level(int enabled)
// {
// #if CONFIG_OPEL_BOARDTEST_OE_ACTIVE_HIGH
//     return enabled ? 1 : 0;
// #else
//     return enabled ? 0 : 1;
// #endif
// }

void app_main(void)
{
    configure_pin_as_output((gpio_num_t)PIN_SDA);
    configure_pin_as_output((gpio_num_t)PIN_SCL);
    configure_pin_as_output((gpio_num_t)PIN_MRQ);

    int bus_level = 0;

    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_SDA, bus_level));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_SCL, bus_level));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_MRQ, bus_level));

    ESP_LOGI(TAG,
             "Board test started. Pins: SDA=%d SCL=%d MRQ=%d",
             PIN_SDA, PIN_SCL, PIN_MRQ);

    while (1)
    {
        bus_level = !bus_level;
        ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_SDA, bus_level));
        ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_SCL, bus_level));
        ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_MRQ, bus_level));

        ESP_LOGI(TAG,
                 "SDA/SCL/MRQ=%d",
                 bus_level);

        vTaskDelay(pdMS_TO_TICKS(SDA_SCL_MRQ_TOGGLE_PERIOD_MS));
    }
}
