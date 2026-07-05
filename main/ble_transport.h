#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*ble_transport_rx_cb_t)(const uint8_t *data, size_t len, void *ctx);

esp_err_t ble_transport_start(const char *device_name, ble_transport_rx_cb_t rx_cb, void *rx_ctx);
