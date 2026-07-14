#include "opel_mid.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

static const char *TAG = "opel_mid";

/* ── Constants ────────────────────────────────────────────────────────────── */

#define OPEL_MID_ADDR_TID_8       0x4Au
#define OPEL_MID_ADDR_TID_10      0x4Du

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
 * @brief Force the bus to the idle state (all lines high).
 *
 * Used during error recovery to ensure the bus is in a known, safe state
 * before retrying or aborting transmission.
 *
 * @param cfg Device config.
 */
static void bus_reset_to_idle(const opel_mid_config_t *cfg)
{
    sda_high(cfg);
    scl_high(cfg);
    mrq_high(cfg);
    ets_delay_us(T_MRQ_US);
}

/**
 * @brief Wait for the slave to drive SDA to the expected level, with timeout.
 *
 * Poll SDA at regular intervals until it matches @p expected or the timeout
 * elapses. Used during bus handshakes to detect slave responses.
 *
 * Reference: §Datenübertragung (handshake timing T1min–T1max = 100 µs–15 ms).
 *
 * @param cfg       Device config.
 * @param expected  1 = wait for SDA high, 0 = wait for SDA low.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if the slave does not respond.
 */
static esp_err_t wait_sda(const opel_mid_config_t *cfg, int expected)
{
    uint32_t elapsed_us = 0u;
    const uint32_t poll_interval_us = T_SDA_WAIT_US; /* 100 µs */

    while (elapsed_us < T_SDA_TIMEOUT_US) {
        int sda_level = get_sda(cfg);
        if (sda_level == expected) {
            return ESP_OK;
        }
        ets_delay_us(poll_interval_us);
        elapsed_us += poll_interval_us;
    }

    ESP_LOGE(TAG, "wait_sda timeout: expected %d, got %d after %u µs",
             expected, get_sda(cfg), elapsed_us);
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief Send the start-of-transmission handshake.
 *
 * Sequence (§Datenübertragung, steps 1–6):
 *   1. Master sets MRQ low
 *   2. Slave pulls SDA low          (wait up to T_SDA_TIMEOUT_US, T1 = 100 µs–15 ms)
 *   3. Master sets MRQ high         (hold ≥ 100 µs)
 *   4. Slave releases SDA high      (wait T4 = 100 µs–200 µs)
 *   5. Master pulls SDA low         (T5 = 100 µs–500 µs)
 *   6. Master pulls SCL low         (T6 = 100 µs–200 µs)
 *   → Ready to clock out address byte.
 *
 * @param cfg Device config.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if slave does not respond.
 */
static esp_err_t bus_start(const opel_mid_config_t *cfg)
{
    /* 1. Master sets MRQ low */
    mrq_low(cfg);
    ets_delay_us(T_MRQ_US);

    /* 2. Slave pulls SDA low (wait up to T1max = 15 ms).
     *    Failure here means the display is not responding or bus is shorted. */
    esp_err_t ret = wait_sda(cfg, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bus_start step 2 failed: slave did not pull SDA low");
        bus_reset_to_idle(cfg);
        return ESP_ERR_INVALID_RESPONSE; /* Slave not responding */
    }
    ets_delay_us(T_MRQ_US);

    /* 3. Master sets MRQ high */
    mrq_high(cfg);
    ets_delay_us(T_MRQ_US);

    /* 4. Slave releases SDA high (wait up to T_SDA_TIMEOUT_US = 15 ms).
     *    Failure here means SDA is stuck low (short to ground). */
    ret = wait_sda(cfg, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bus_start step 4 failed: SDA stuck low (possible short to ground)");
        bus_reset_to_idle(cfg);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ets_delay_us(T_MRQ_US);

    /* 5. Master pulls SDA low */
    sda_low(cfg);
    ets_delay_us(T_MRQ_US);

    /* 6. Master pulls SCL low */
    scl_low(cfg);
    ets_delay_us(T_MRQ_US);

    return ESP_OK;
}

/**
 * @brief Clock out one byte MSB-first and read the slave ACK/parity bit.\n *
 * Bit transmission (§Bit Synchronisation):
 *   – For each of the 8 bits (MSB first):\n *       1. Drive SDA to bit value
 *       2. Wait Ts (≥ 5 µs) for setup
 *       3. Set SCL high
 *       4. Wait for slave to release SCL (clock stretching) or timeout
 *       5. Set SCL low, hold ≥ 50 µs (TSCLLmin)
 *       6. Wait Th (≥ 5 µs) for hold before next bit\n *
 * After all 8 bits, perform ACK cycle (§Bestätigung am Ende des Bytes):
 *       7. Release SDA (pull high via open-drain)
 *       8. Set SCL high
 *       9. Wait ≥ 50 µs, then read SDA:\n *          – SDA low = ACK (parity OK)\n *          – SDA high = NACK (parity error)\n *      10. Set SCL low
 *      11. SDA remains at slave's control momentarily, then revert to low\n *
 * @param cfg  Device config.
 * @param byte Byte to send (bit 0 is parity, bits[7:1] are data).
 * @return ESP_OK on ACK, ESP_ERR_INVALID_RESPONSE on NACK/parity error.
 */
static esp_err_t bus_send_byte(const opel_mid_config_t *cfg, uint8_t byte)
{
    /* Send 8 bits, MSB first. */
    for (int bit_idx = 7; bit_idx >= 0; --bit_idx) {
        int bit_val = (byte >> bit_idx) & 1u;

        /* 1. Drive SDA to bit value */
        if (bit_val) {
            sda_high(cfg);
        } else {
            sda_low(cfg);
        }
        ets_delay_us(T_SETUP_US);

        /* 3. Set SCL high */
        scl_high(cfg);

        /* 4. Wait for slave to release SCL (clock stretching) with timeout.
         *    The slave may hold SCL low to signal "I'm busy", but must
         *    release it within a reasonable time. Per the spec, SCL must
         *    be high for at least T_SCL_HIGH_US (50 µs), so we use 1 ms
         *    as a safety margin. If exceeded, this indicates a fault. */
        uint32_t stretch_timeout_us = 1000u; /* 1 ms */
        uint32_t elapsed_us = 0u;
        while (get_scl(cfg) == 0 && elapsed_us < stretch_timeout_us) {
            ets_delay_us(10u);
            elapsed_us += 10u;
        }
        if (elapsed_us >= stretch_timeout_us) {
            /* SCL stuck low: slave is unresponsive or bus is shorted. */
            ESP_LOGE(TAG, "SCL clock stretch timeout at bit %d (possibly shorted to ground)", bit_idx);
            scl_high(cfg); /* Release SCL to try to recover */
            return ESP_ERR_INVALID_RESPONSE;
        }

        ets_delay_us(T_SCL_HIGH_US);

        /* 5. Set SCL low */
        scl_low(cfg);
        ets_delay_us(T_SCL_LOW_US);

        /* 6. Wait for hold time before next bit */
        ets_delay_us(T_HOLD_US);
    }

    /* ── ACK cycle ─────────────────────────────────────────────────────────── */

    /* 7. Release SDA (open-drain pull-up) */
    sda_high(cfg);
    ets_delay_us(T_SETUP_US);

    /* 8. Set SCL high */
    scl_high(cfg);
    ets_delay_us(T_SCL_HIGH_US);

    /* 9. Read ACK bit: slave pulls SDA low if parity OK, stays high on error */
    int ack_bit = get_sda(cfg);

    /* 10. Set SCL low */
    scl_low(cfg);
    ets_delay_us(T_SCL_LOW_US);

    /* 11. Master reclaims SDA (open-drain pull-down) */
    sda_low(cfg);
    ets_delay_us(T_HOLD_US);

    return (ack_bit == 0) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

/**
 * @brief Send the end-of-transmission sequence.
 *
 * Sequence (§Ende der Übertragung):
 *    9. SDA low   (≥ 100 µs) — already held low from last bit
 *   10. MRQ high  (100 µs – 1 ms)
 *   11. SCL high  (≥ 100 µs)
 *   12. SDA high  (≥ 100 µs)
 *
 * After transmission completes, master must wait ≥ 100 µs before initiating
 * the next bus_start() sequence (§Fehlerbehandlung).
 *
 * @param cfg Device config.
 * @return ESP_OK on success.
 */
static esp_err_t bus_stop(const opel_mid_config_t *cfg)
{
    /* 9. SDA low (already held) */
    sda_low(cfg);
    ets_delay_us(T_MRQ_US);

    /* 10. MRQ high */
    mrq_high(cfg);
    ets_delay_us(T_MRQ_US);

    /* 11. SCL high */
    scl_high(cfg);
    ets_delay_us(T_MRQ_US);

    /* 12. SDA high */
    sda_high(cfg);
    ets_delay_us(T_MRQ_US);

    return ESP_OK;
}

/**
 * @brief Send one byte with automatic retry on parity error.
 *
 * Retries up to OPEL_MID_MAX_RETRIES times; on final failure the caller
 * should issue bus_stop() — the display will show blank characters
 * (§Fehlerbehandlung).
 *
 * @param cfg  Device config.
 * @param byte Fully-formed byte: data in bits[7:1], odd parity in bit[0].
 *             Use apply_odd_parity() or char_to_display_byte() to build it.
 * @return ESP_OK on success, ESP_ERR_INVALID_RESPONSE after all retries.
 */
static esp_err_t bus_send_byte_with_retry(const opel_mid_config_t *cfg,
                                          uint8_t                  byte)
{
    for (unsigned i = 0u; i < OPEL_MID_MAX_RETRIES; i++) {
        esp_err_t ret = bus_send_byte(cfg, byte);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Parity error on byte 0x%02X, retry %u/%u",
                 byte, i + 1u, OPEL_MID_MAX_RETRIES);
    }
    return ESP_ERR_INVALID_RESPONSE;
}

/**
 * @brief Encode an ASCII character as a display byte ready for transmission.
 *
 * Each byte on the bus carries 7 data bits in bits[7:1] and an odd parity
 * bit in bit[0].  The display's character set maps 1-to-1 to ASCII: the
 * 7-bit ASCII code is placed directly into bits[7:1].
 *
 * Verified against observed wire values, e.g.:
 *   'A' (0x41) → 0x83   'B' (0x42) → 0x85   ' ' (0x20) → 0x40
 *
 * Characters outside the printable ASCII range (0x20–0x7E) are substituted
 * with a space.
 *
 * @param c  ASCII character.
 * @return   Byte with data in bits[7:1] and odd parity in bit[0].
 */
static uint8_t char_to_display_byte(char c)
{
    uint8_t ascii = ((uint8_t)c >= 0x20u && (uint8_t)c <= 0x7Eu)
                    ? (uint8_t)c : 0x20u;
    return apply_odd_parity((uint8_t)(ascii << 1u));
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

    struct opel_mid_dev_t   *dev = handle;
    const opel_mid_config_t *cfg = &dev->config;

    /*
     * Power-on test sequence (§Power on Test).
     *
     * The master must send this pulse sequence on SCL and MRQ after the AA
     * (Antenna Amplifier / radio-on) signal is asserted, before any data is
     * exchanged. Timing:
     *   T1 (SCL pulse duration)   100 ms – 500 ms
     *   T2 (MRQ pulse duration)   500 µs – 1 ms
     *   T3 (gap to first data)    1 ms   – 2 ms
     *
     * The slave detects this sequence and verifies line integrity:
     *   – Constant low on any line → short to ground
     *   – Constant high on any line → short to +Vbatt
     *   – Signal appears on wrong line → cross-short between lines
     *
     * The sequence:
     *   1. SCL high (release)
     *   2. Wait T1 (≥ 100 ms)
     *   3. SCL low (pull)
     *   4. Wait T1 (≥ 100 ms)
     *   5. SCL high (release)
     *   6. MRQ low (pull)
     *   7. Wait T2 (500 µs – 1 ms)
     *   8. MRQ high (release)
     *   9. Wait T3 (1 ms – 2 ms) before first bus_start()
     */

    /* Ensure bus is idle-high before test */
    scl_high(cfg);
    sda_high(cfg);
    mrq_high(cfg);

    ESP_LOGI(TAG, "Power-on test: SCL pulse (T1=100ms)...");

    /* T1: SCL low pulse (100 ms) */
    scl_low(cfg);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Release SCL */
    scl_high(cfg);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Power-on test: MRQ pulse (T2=500µs)...");

    /* T2: MRQ low pulse (500 µs – 1 ms, use 1 ms for safety) */
    mrq_low(cfg);
    ets_delay_us(1000);

    /* Release MRQ */
    mrq_high(cfg);

    /* T3: Gap before first data (1 ms – 2 ms) */
    ets_delay_us(2000);

    ESP_LOGI(TAG, "Power-on test complete");
    return ESP_OK;
}

esp_err_t opel_mid_send(opel_mid_handle_t         handle,
                        const char               *text,
                        const opel_mid_symbols_t *symbols)
{
    if (!handle || !text) {
        return ESP_ERR_INVALID_ARG;
    }

    struct opel_mid_dev_t   *dev = handle;
    const opel_mid_config_t *cfg = &dev->config;

    /* Default to all symbols off when caller passes NULL. */
    static const opel_mid_symbols_t no_symbols = {0u, 0u, 0u};
    if (!symbols) {
        symbols = &no_symbols;
    }

    /*
     * Build the three symbol bytes.
     *
     * Each symbol byte layout (bit 0 = parity, per §Format der Status-Bytes):
     *
     *   Radio Status (byte 1)
     *     bit 7  COMMA          bit 6  RDS
     *     bit 5  TP             bit 4  STEREO
     *     bit 3  0              bit 2  AS
     *     bit 1  TP_BRACKET     bit 0  parity
     *
     *   Tape Status (byte 2)
     *     bit 7  CD_IN          bit 6  DOLBY_C
     *     bit 5  DOLBY_B        bit 4  CR
     *     bit 3  CPS            bit 2  0
     *     bit 1  0              bit 0  parity
     *
     *   CD Status (byte 3, 10-digit only)
     *     bit 7  0              bit 6  TRACK
     *     bit 5  RDM            bit 4  PGM
     *     bit 3  DISC           bit 2  0
     *     bit 1  0              bit 0  parity
     *
     * apply_odd_parity() treats bits[7:1] as data and computes bit[0],
     * so we mask off bit 0 of the caller-supplied flags before passing in.
     */
    uint8_t sym[3] = {
        apply_odd_parity(symbols->radio & 0xFEu), /* Radio Status */
        apply_odd_parity(symbols->tape  & 0xFEu), /* Tape  Status */
        apply_odd_parity(symbols->cd    & 0xFEu), /* CD    Status (10-digit only) */
    };

    /*
     * Address byte: the 7-bit slave address is placed in bits[7:1] and odd
     * parity applied to bit[0], consistent with all other bus bytes.
     */
    uint8_t addr_byte = apply_odd_parity((uint8_t)(dev->addr << 1u));

    /* ── Transmit frame (§Format einer Nachricht) ─────────────────────────── */
    esp_err_t ret;

    ret = bus_start(cfg);
    if (ret != ESP_OK) {
        /* bus_start() calls bus_reset_to_idle() on error, so no cleanup needed */
        ESP_LOGE(TAG, "opel_mid_send: bus_start() failed; frame transmission aborted");
        return ret;
    }

    /* 1. Slave address */
    ret = bus_send_byte_with_retry(cfg, addr_byte);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "opel_mid_send: address byte transmission failed (0x%02X)", addr_byte);
        bus_stop(cfg);
        bus_reset_to_idle(cfg); /* Extra recovery after stop */
        return ret;
    }

    /* 2. Symbol bytes (2 for TID-8, 3 for TID-10/MID) */
    for (uint8_t i = 0u; i < dev->sym_bytes; i++) {
        ret = bus_send_byte_with_retry(cfg, sym[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "opel_mid_send: symbol byte %u transmission failed (0x%02X)", i, sym[i]);
            bus_stop(cfg);
            bus_reset_to_idle(cfg);
            return ret;
        }
    }

    /* 3. Data bytes — ASCII text, space-padded to display width */
    size_t text_len = strlen(text);
    for (uint8_t i = 0u; i < dev->data_bytes; i++) {
        char c = (i < text_len) ? text[i] : ' ';
        ret = bus_send_byte_with_retry(cfg, char_to_display_byte(c));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "opel_mid_send: data byte %u transmission failed (char='%c')", i, c);
            bus_stop(cfg);
            bus_reset_to_idle(cfg);
            return ret;
        }
    }

    ret = bus_stop(cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "opel_mid_send: bus_stop() failed");
        bus_reset_to_idle(cfg);
    }
    return ret;
}

esp_err_t opel_mid_set_time(opel_mid_handle_t handle,
                            uint8_t           day,
                            uint8_t           month,
                            uint8_t           year,
                            uint8_t           hours,
                            uint8_t           minutes)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (day < 1u || day > 31u || month < 1u || month > 12u || year > 99u ||
        hours > 23u || minutes > 59u) {
        ESP_LOGE(TAG, "opel_mid_set_time: invalid datetime %02u-%02u-%02u %02u:%02u",
                 day, month, year, hours, minutes);
        return ESP_ERR_INVALID_ARG;
    }

    struct opel_mid_dev_t   *dev = handle;
    const opel_mid_config_t *cfg = &dev->config;

    /* Hardware time-sync frame (13 bytes) using command 0x60. */
    uint8_t frame[13] = {0};
    frame[0]  = (dev->addr == OPEL_MID_ADDR_TID_8) ? 0x10u : 0x12u;
    frame[1]  = 0x60u;
    frame[2]  = 0x00u;
    frame[3]  = 0x00u;
    frame[4]  = 0x00u;
    frame[5]  = day;
    frame[6]  = month;
    frame[7]  = year;
    frame[8]  = hours;
    frame[9]  = minutes;
    frame[10] = 0x00u;
    frame[11] = 0x00u;

    /* Inverted XOR checksum over bytes 0..11. */
    uint8_t checksum = 0u;
    for (size_t i = 0u; i < 12u; i++) {
        checksum ^= frame[i];
    }
    frame[12] = (uint8_t)~checksum;

    esp_err_t ret = bus_start(cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "opel_mid_set_time: bus_start() failed");
        return ret;
    }

    for (size_t i = 0u; i < sizeof(frame); i++) {
        ret = bus_send_byte_with_retry(cfg, frame[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "opel_mid_set_time: frame byte %u failed (0x%02X)", (unsigned)i, frame[i]);
            bus_stop(cfg);
            bus_reset_to_idle(cfg);
            return ret;
        }
    }

    ret = bus_stop(cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "opel_mid_set_time: bus_stop() failed");
        bus_reset_to_idle(cfg);
        return ret;
    }

    ESP_LOGI(TAG, "Set time sync: %02u-%02u-%02u %02u:%02u (cmd=0x60, checksum=0x%02X)",
             day, month, year, hours, minutes, frame[12]);
    return ESP_OK;
}
