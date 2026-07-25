#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "boardtest";

#define PIN_SDA CONFIG_OPEL_BOARDTEST_PIN_SDA
#define PIN_SCL CONFIG_OPEL_BOARDTEST_PIN_SCL
#define PIN_MRQ CONFIG_OPEL_BOARDTEST_PIN_MRQ
#define PIN_OE CONFIG_OPEL_BOARDTEST_PIN_OE

#define SDA_SCL_MRQ_TOGGLE_PERIOD_MS 1000
#define OE_TOGGLE_PERIOD_MS 10000

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

static int oe_enabled_to_level(int enabled)
{
#if CONFIG_OPEL_BOARDTEST_OE_ACTIVE_HIGH
    return enabled ? 1 : 0;
#else
    return enabled ? 0 : 1;
#endif
}

void app_main(void)
{
    configure_pin_as_output((gpio_num_t)PIN_SDA);
    configure_pin_as_output((gpio_num_t)PIN_SCL);
    configure_pin_as_output((gpio_num_t)PIN_MRQ);
    configure_pin_as_output((gpio_num_t)PIN_OE);

    int bus_level = 0;
    int oe_enabled = 1;
    int oe_elapsed_ms = 0;

    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_SDA, bus_level));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_SCL, bus_level));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_MRQ, bus_level));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_OE, oe_enabled_to_level(oe_enabled)));

    ESP_LOGI(TAG,
             "Board test started. Pins: SDA=%d SCL=%d MRQ=%d OE=%d (OE active-%s)",
             PIN_SDA, PIN_SCL, PIN_MRQ, PIN_OE,
#if CONFIG_OPEL_BOARDTEST_OE_ACTIVE_HIGH
             "high"
#else
             "low"
#endif
    );

    while (1)
    {
        bus_level = !bus_level;
        ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_SDA, bus_level));
        ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_SCL, bus_level));
        ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_MRQ, bus_level));

        oe_elapsed_ms += SDA_SCL_MRQ_TOGGLE_PERIOD_MS;
        if (oe_elapsed_ms >= OE_TOGGLE_PERIOD_MS)
        {
            oe_elapsed_ms = 0;
            oe_enabled = !oe_enabled;
            ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)PIN_OE, oe_enabled_to_level(oe_enabled)));
        }

        ESP_LOGI(TAG,
                 "SDA/SCL/MRQ=%d, OE=%d (enabled=%d)",
                 bus_level,
                 oe_enabled_to_level(oe_enabled),
                 oe_enabled);

        vTaskDelay(pdMS_TO_TICKS(SDA_SCL_MRQ_TOGGLE_PERIOD_MS));
    }
}
