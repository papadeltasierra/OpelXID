#include "opel_mid.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

static const char *TAG = "opel_mid";

/* ── Constants ────────────────────────────────────────────────────────────── */

#define OPEL_MID_ADDR_TID_8    0x4Au
#define OPEL_MID_ADDR_TID_10   0x4Du

#define OPEL_MID_DATA_BYTES_8   8u
#define OPEL_MID_DATA_BYTES_10  10u
#define OPEL_MID_SYM_BYTES_8    2u
#define OPEL_MID_SYM_BYTES_10   3u

/** Maximum send retries per byte on parity error (§Fehlerbehandlung). */
#define OPEL_MID_MAX_RETRIES    3u

/*
 * Timing (microseconds). Values are taken from the timing tables on
 * https://wiki.carluccio.de/index.php/Opel_TID.
 * Minimums are used throughout; increase if the display proves unreliable.
 */
#define T_SCL_HIGH_US    50u  /* TSCLHmin */
#define T_SCL_LOW_US     50u  /* TSCLLmin */
#define T_SETUP_US        5u  /* Ts (data setup before SCL high) */
#define T_HOLD_US         5u  /* Th (data hold after SCL low) */
#define T_MRQ_US        100u  /* Generic MRQ pulse width */
#define T_SDA_WAIT_US   100u  /* Poll interval waiting for slave SDA response */
#define T_SDA_TIMEOUT_US 15000u /* T1max: slave must respond within 15 ms */

/* ── Device structure ─────────────────────────────────────────────────────── */

struct opel_mid_dev_t {
    opel_mid_config_t config;
    uint8_t           addr;
    uint8_t           sym_bytes;
    uint8_t           data_bytes;
};

/* ── GPIO helpers ─────────────────────────────────────────────────────────── */

static inline void sda_high(const opel_mid_config_t *c) { gpio_set_level(c->pin_sda, 1); }
static inline void sda_low (const opel_mid_config_t *c) { gpio_set_level(c->pin_sda, 0); }
static inline void scl_high(const opel_mid_config_t *c) { gpio_set_level(c->pin_scl, 1); }
static inline void scl_low (const opel_mid_config_t *c) { gpio_set_level(c->pin_scl, 0); }
static inline void mrq_high(const opel_mid_config_t *c) { gpio_set_level(c->pin_mrq, 1); }
static inline void mrq_low (const opel_mid_config_t *c) { gpio_set_level(c->pin_mrq, 0); }
static inline int  get_sda (const opel_mid_config_t *c) { return gpio_get_level(c->pin_sda); }
static inline int  get_scl (const opel_mid_config_t *c) { return gpio_get_level(c->pin_scl); }

/* ── Parity ───────────────────────────────────────────────────────────────── */

/**
 * @brief Apply odd parity.
 *
 * The bus uses 7-bit data + 1 parity bit (bit 0, LSB).
 * Odd parity: total number of 1-bits in the byte must be odd.
 *
 * @param data  Value with data in bits [7:1]; bit 0 is ignored.
 * @return      @p data with bit 0 set so that total 1-bit count is odd.
 */
static uint8_t apply_odd_parity(uint8_t data)
{
    /* Work on bits [7:1] only */
    uint8_t v = (data >> 1) & 0x7Fu;
    uint8_t ones = 0u;
    while (v) {
        ones += v & 1u;
        v >>= 1u;
    }
    uint8_t parity = (ones % 2u == 0u) ? 1u : 0u;
    return (data & 0xFEu) | parity;
}

/* ── Low-level bus ────────────────────────────────────────────────────────── */

/**
 * @brief Wait for the slave to drive SDA to the expected level, with timeout.
 *
 * @param cfg       Device config.
 * @param expected  1 = wait for SDA high, 0 = wait for SDA low.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if the slave does not respond.
 */
static esp_err_t wait_sda(const opel_mid_config_t *cfg, int expected)
{
    /* TODO: implement wait loop with timeout */
    (void)cfg;
    (void)expected;
    return ESP_OK;
}

/**
 * @brief Send the start-of-transmission handshake.
 *
 * Sequence (§Datenübertragung):
 *   1. Master sets MRQ low
 *   2. Slave pulls SDA low          (wait up to T_SDA_TIMEOUT_US)
 *   3. Master sets MRQ high         (hold T_MRQ_US)
 *   4. Slave releases SDA high      (wait)
 *   5. Master pulls SDA low         (T4 ≥ 100 µs)
 *   6. Master pulls SCL low         (T6 ≥ 100 µs)
 *   → Ready to clock out address byte.
 *
 * @param cfg Device config.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if slave does not respond.
 */
static esp_err_t bus_start(const opel_mid_config_t *cfg)
{
    /* TODO: implement full handshake */
    (void)cfg;
    return ESP_OK;
}

/**
 * @brief Clock out one byte MSB-first and read the slave ACK/parity bit.
 *
 * Bit transmission (§Bit Synchronisation):
 *   1. Drive SDA to bit value
 *   2. Wait Ts (5 µs setup)
 *   3. Set SCL high
 *   4. Set SCL low                  (hold T_SCL_HIGH_US ≥ 50 µs)
 *   5. Wait Th (5 µs hold)
 *   6. Drive next bit on SDA
 *   7. Wait remainder of T_SCL_LOW_US
 *   8. Set SCL high; wait for slave to stretch if needed
 *
 * After LSB, perform ACK cycle (§Bestätigung am Ende des Bytes):
 *   – Release SDA; slave asserts SDA low if parity OK, high on error.
 *
 * @param cfg  Device config.
 * @param byte Byte to send (bit 0 must already contain the parity bit).
 * @return ESP_OK on ACK, ESP_ERR_INVALID_RESPONSE on NACK/parity error.
 */
static esp_err_t bus_send_byte(const opel_mid_config_t *cfg, uint8_t byte)
{
    /* TODO: implement bit-bang send with clock stretching and ACK check */
    (void)cfg;
    (void)byte;
    return ESP_OK;
}

/**
 * @brief Send the end-of-transmission sequence.
 *
 * Sequence (§Ende der Übertragung):
 *   9.  SDA low   (≥ 100 µs)
 *   10. MRQ high  (100 µs – 1 ms)
 *   11. SCL high  (≥ 100 µs)
 *   12. SDA high  (≥ 100 µs)
 *
 * @param cfg Device config.
 * @return ESP_OK on success.
 */
static esp_err_t bus_stop(const opel_mid_config_t *cfg)
{
    /* TODO: implement end-of-transmission sequence */
    (void)cfg;
    return ESP_OK;
}

/**
 * @brief Send one byte with automatic retry on parity error.
 *
 * Retries up to OPEL_MID_MAX_RETRIES times; on final failure the
 * caller should issue bus_stop() and display will show blank characters
 * (§Fehlerbehandlung).
 *
 * @param cfg  Device config.
 * @param byte Byte to send (without parity; parity is applied here).
 * @return ESP_OK on success, ESP_ERR_INVALID_RESPONSE after all retries.
 */
static esp_err_t bus_send_byte_with_retry(const opel_mid_config_t *cfg,
                                          uint8_t                  byte)
{
    uint8_t framed = apply_odd_parity(byte << 1u); /* shift data to bits[7:1] */
    for (unsigned i = 0u; i < OPEL_MID_MAX_RETRIES; i++) {
        esp_err_t ret = bus_send_byte(cfg, framed);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Parity error on byte 0x%02X, retry %u/%u",
                 byte, i + 1u, OPEL_MID_MAX_RETRIES);
    }
    return ESP_ERR_INVALID_RESPONSE;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t opel_mid_init(const opel_mid_config_t *config,
                        opel_mid_handle_t       *out_handle)
{
    if (!config || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    struct opel_mid_dev_t *dev = calloc(1u, sizeof(*dev));
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }

    dev->config = *config;

    if (config->type == OPEL_MID_TYPE_TID_8) {
        dev->addr       = OPEL_MID_ADDR_TID_8;
        dev->sym_bytes  = OPEL_MID_SYM_BYTES_8;
        dev->data_bytes = OPEL_MID_DATA_BYTES_8;
    } else {
        dev->addr       = OPEL_MID_ADDR_TID_10;
        dev->sym_bytes  = OPEL_MID_SYM_BYTES_10;
        dev->data_bytes = OPEL_MID_DATA_BYTES_10;
    }

    /* Configure all three lines as open-drain outputs, initially idle-high.
     * The display supplies pull-up resistors on the slave side (§Elektrische
     * Daten: High > 4 V, Low < 1 V). */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << config->pin_sda) |
                        (1ULL << config->pin_scl) |
                        (1ULL << config->pin_mrq),
        .mode         = GPIO_MODE_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        free(dev);
        return ret;
    }

    sda_high(config);
    scl_high(config);
    mrq_high(config);

    *out_handle = dev;
    ESP_LOGI(TAG, "Init OK: type=%s addr=0x%02X pins SDA=%d SCL=%d MRQ=%d",
             config->type == OPEL_MID_TYPE_TID_8 ? "TID-8" : "TID-10",
             dev->addr,
             config->pin_sda, config->pin_scl, config->pin_mrq);
    return ESP_OK;
}

esp_err_t opel_mid_deinit(opel_mid_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    free(handle);
    return ESP_OK;
}

esp_err_t opel_mid_power_on(opel_mid_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    /*
     * TODO: implement the power-on test sequence (§Power on Test).
     *
     * The master must send a specific pulse sequence on SCL and MRQ after the
     * AA line goes high, within the timing window:
     *   T1 (SCL pulse)    100 ms – 500 ms
     *   T2 (MRQ pulse)    500 µs – 1 ms
     *   T3 (gap to data)  1 ms   – 2 ms
     *
     * This allows the slave to verify line integrity before data is exchanged.
     */
    ESP_LOGW(TAG, "opel_mid_power_on: not yet implemented");
    return ESP_OK;
}

esp_err_t opel_mid_send(opel_mid_handle_t         handle,
                        const char               *text,
                        const opel_mid_symbols_t *symbols)
{
    if (!handle || !text) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * TODO: implement full frame transmission.
     *
     * Frame layout (§Format einer Nachricht):
     *   1. bus_start()                          – MRQ/SDA handshake
     *   2. bus_send_byte_with_retry(addr)        – slave address
     *   3. bus_send_byte_with_retry(radio_sym)   – Radio status byte  }
     *   4. bus_send_byte_with_retry(tape_sym)    – Tape  status byte  } symbol bytes
     *  [5. bus_send_byte_with_retry(cd_sym)]     – CD    status byte  } (10-digit only)
     *   6..N. bus_send_byte_with_retry(char[i])  – data bytes (space-padded)
     *   N+1. bus_stop()                          – end-of-transmission
     *
     * Character encoding: each ASCII character is mapped to a 7-bit display
     * code before parity is applied. Mapping table to be added.
     */
    ESP_LOGW(TAG, "opel_mid_send: not yet implemented (text=\"%s\")", text);
    (void)symbols;
    return ESP_OK;
}
