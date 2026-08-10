#include "opel_mid.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

static const char *TAG = "opel_mid";

/* ── Constants ────────────────────────────────────────────────────────────── */

#define OPEL_MID_ADDR_TID_8 0x4Au
#define OPEL_MID_ADDR_TID_10 0x4Du

#define OPEL_MID_DATA_BYTES_8 8u
#define OPEL_MID_DATA_BYTES_10 10u
#define OPEL_MID_SYM_BYTES_8 2u
#define OPEL_MID_SYM_BYTES_10 3u

/** Maximum send retries per byte on parity error (§Fehlerbehandlung). */
#define OPEL_MID_MAX_RETRIES 3u

/*
 * Timing (microseconds). Values are taken from the timing tables on
 * https://wiki.carluccio.de/index.php/Opel_TID.
 * Minimums are used throughout; increase if the display proves unreliable.
 */
#define T_SCL_HIGH_US 50u              /* TSCLHmin */
#define T_SCL_LOW_US 50u               /* TSCLLmin */
#define T_SETUP_US 5u                  /* Ts (data setup before SCL high) */
#define T_HOLD_US 5u                   /* Th (data hold after SCL low) */
#define T_MRQ_US 100u                  /* Generic MRQ pulse width */
#define T_SDA_WAIT_US 100u             /* Poll interval waiting for slave SDA response */
#define T_SDA_TIMEOUT_US 15000u        /* T1max: slave must respond within 15 ms */
#define T_POWER_ON_T1_MS 100u          /* Power-on test T1 minimum */
#define T_POWER_ON_T2_US 500u          /* Power-on test T2 minimum */
#define T_POWER_ON_T3_US 1000u         /* Power-on test T3 minimum */
#define T_SCL_STRETCH_TIMEOUT_US 1000u /* Clock stretching timeout (1 ms) */
#define T_ACK_CONTROL_US 500u          /* SDA release time for ACK cycle */
#define OPEL_MID_RDS_CT_GROUP 0x47u    /* RDS Clock Time group identifier */

/* Define for opel_mid10_send frame structure: address + data/mode bytes */
#define OPEL_MID_RDS_FRAME_BYTES 11u /* Total data/mode bytes in RDS frame */

/* ── Device structure ─────────────────────────────────────────────────────── */

struct opel_mid_dev_t
{
    opel_mid_config_t config;
    uint8_t addr;
    uint8_t sym_bytes;
    uint8_t data_bytes;
};

/* ── GPIO helpers ─────────────────────────────────────────────────────────── */

static inline void sda_high(const opel_mid_config_t *c) { gpio_set_level(c->pin_sda, 1); }
static inline void sda_low(const opel_mid_config_t *c) { gpio_set_level(c->pin_sda, 0); }
static inline void scl_high(const opel_mid_config_t *c) { gpio_set_level(c->pin_scl, 1); }
static inline void scl_low(const opel_mid_config_t *c) { gpio_set_level(c->pin_scl, 0); }
static inline void mrq_high(const opel_mid_config_t *c) { gpio_set_level(c->pin_mrq, 1); }
static inline void mrq_low(const opel_mid_config_t *c) { gpio_set_level(c->pin_mrq, 0); }
static inline int get_sda(const opel_mid_config_t *c) { return gpio_get_level(c->pin_sda); }
static inline int get_scl(const opel_mid_config_t *c) { return gpio_get_level(c->pin_scl); }
static inline int get_mrq(const opel_mid_config_t *c) { return gpio_get_level(c->pin_mrq); }

static esp_err_t expect_bus_levels(const opel_mid_config_t *cfg,
                                   int expected_sda,
                                   int expected_scl,
                                   int expected_mrq,
                                   const char *phase)
{
    int sda = get_sda(cfg);
    int scl = get_scl(cfg);
    int mrq = get_mrq(cfg);

    if (sda != expected_sda || scl != expected_scl || mrq != expected_mrq)
    {
        ESP_LOGE(TAG,
                 "%s: expected SDA/SCL/MRQ=%d/%d/%d, got %d/%d/%d",
                 phase,
                 expected_sda, expected_scl, expected_mrq,
                 sda, scl, mrq);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

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
    /* Data is sent in bits [7:1] only; original bit 7 is lost */
    uint8_t v = (uint8_t)((data << 1) & 0xFEu);

    /* Assume we need the odd parity bit*/
    uint8_t p = v | 0x01;
    p = p ^ (p >> 4);
    p = p ^ (p >> 2);
    p = p ^ (p >> 1);
    return v | (p & 0x01u);
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

    while (elapsed_us < T_SDA_TIMEOUT_US)
    {
        int sda_level = get_sda(cfg);
        if (sda_level == expected)
        {
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
    /* [1] 1. Master sets MRQ low */
    mrq_low(cfg);
    ets_delay_us(T_MRQ_US);

    /* [2] 2. Slave pulls SDA low (wait up to T1max = 15 ms).
     *    Failure here means the display is not responding or bus is shorted. */
    esp_err_t ret = wait_sda(cfg, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "bus_start step 2 failed: slave did not pull SDA low");
        bus_reset_to_idle(cfg);
        return ESP_ERR_INVALID_RESPONSE; /* Slave not responding */
    }
    ets_delay_us(T_MRQ_US);

    /* [3] 3. Master sets MRQ high */
    mrq_high(cfg);
    ets_delay_us(T_MRQ_US);

    /* [4] 4. Slave releases SDA high (wait up to T_SDA_TIMEOUT_US = 15 ms).
     *    Failure here means SDA is stuck low (short to ground). */
    ret = wait_sda(cfg, 1);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "bus_start step 4 failed: SDA stuck low (possible short to ground)");
        bus_reset_to_idle(cfg);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ets_delay_us(T_MRQ_US);

    /* 5. Master pulls SDA low */
    sda_low(cfg);
    ets_delay_us(T_MRQ_US);

    /* [6] 6. Master pulls SCL low */
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
    for (int bit_idx = 7; bit_idx >= 0; --bit_idx)
    {
        int bit_val = (byte >> bit_idx) & 1u;

        /* 1. Drive SDA to bit value */
        if (bit_val)
        {
            sda_high(cfg);
        }
        else
        {
            sda_low(cfg);
        }
        ets_delay_us(T_SETUP_US);

        /* 3. Set SCL high */
        scl_high(cfg);

        /* 4. Wait for slave to release SCL (clock stretching) with timeout.
         *    The slave may hold SCL low to signal "I'm busy", but must
         *    release it within a reasonable time. Per the spec, SCL must
         *    be high for at least T_SCL_HIGH_US (50 µs), so we use a safety
         *    margin. If exceeded, this indicates a fault. */
        uint32_t elapsed_us = 0u;
        while (get_scl(cfg) == 0 && elapsed_us < T_SCL_STRETCH_TIMEOUT_US)
        {
            ets_delay_us(10u);
            elapsed_us += 10u;
        }
        if (elapsed_us >= T_SCL_STRETCH_TIMEOUT_US)
        {
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
    ets_delay_us(T_ACK_CONTROL_US); /* Allow slave to take control of SDA */

    // At this point the slave will pull SDA low for an ACK or leave high for a NACK.

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
 * Takes a raw data byte, applies odd parity, and transmits it. Retries up to
 * OPEL_MID_MAX_RETRIES times; on final failure the caller should issue
 * bus_stop() — the display will show blank characters (§Fehlerbehandlung).
 *
 * @param cfg   Device config.
 * @param byte  Raw data byte (bits[7:0]); parity is computed and applied here.
 * @return ESP_OK on success, ESP_ERR_INVALID_RESPONSE after all retries.
 */
static esp_err_t bus_send_byte_with_retry(const opel_mid_config_t *cfg,
                                          uint8_t byte)
{
    /* Apply odd parity to the raw byte */
    uint8_t byte_with_parity = apply_odd_parity(byte);

    for (unsigned i = 0u; i < OPEL_MID_MAX_RETRIES; i++)
    {
        esp_err_t ret = bus_send_byte(cfg, byte_with_parity);
        if (ret == ESP_OK)
        {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Parity error on byte 0x%02X, retry %u/%u",
                 byte_with_parity, i + 1u, OPEL_MID_MAX_RETRIES);
    }
    return ESP_ERR_INVALID_RESPONSE;
}

/**
 * @brief Encode an ASCII character as a display byte (without parity).
 *
 * Each byte on the bus carries 7 data bits in bits[7:1] and an odd parity
 * bit in bit[0].  The display's character set maps 1-to-1 to ASCII: the
 * 7-bit ASCII code is placed directly into bits[7:1].
 *
 * The parity bit will be computed and applied during transmission by
 * bus_send_byte_with_retry().
 *
 * Characters outside the printable ASCII range (0x20–0x7E) are substituted
 * with a space.
 *
 * @param c  ASCII character.
 * @return   Raw 7-bit value; parity and shifting will be applied on send.
 */
static uint8_t char_to_display_byte(char c)
{
    return ((uint8_t)c >= 0x20u && (uint8_t)c <= 0x7Eu)
               ? (uint8_t)c
               : 0x20u;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t opel_mid_init(const opel_mid_config_t *config,
                        opel_mid_handle_t *out_handle)
{
    if (!config || !out_handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    struct opel_mid_dev_t *dev = calloc(1u, sizeof(*dev));
    if (!dev)
    {
        return ESP_ERR_NO_MEM;
    }

    dev->config = *config;

    if (config->type == OPEL_MID_TYPE_TID_8)
    {
        dev->addr = OPEL_MID_ADDR_TID_8;
        dev->sym_bytes = OPEL_MID_SYM_BYTES_8;
        dev->data_bytes = OPEL_MID_DATA_BYTES_8;
    }
    else
    {
        dev->addr = OPEL_MID_ADDR_TID_10;
        dev->sym_bytes = OPEL_MID_SYM_BYTES_10;
        dev->data_bytes = OPEL_MID_DATA_BYTES_10;
    }

    /* Configure all three lines as open-drain outputs, initially idle-high.
     * The display supplies pull-up resistors on the slave side (§Elektrische
     * Daten: High > 4 V, Low < 1 V). */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << config->pin_sda) |
                        (1ULL << config->pin_scl) |
                        (1ULL << config->pin_mrq),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK)
    {
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
    if (!handle)
    {
        return ESP_ERR_INVALID_ARG;
    }
    free(handle);
    return ESP_OK;
}

esp_err_t opel_mid_power_on(opel_mid_handle_t handle)
{
    if (!handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    struct opel_mid_dev_t *dev = handle;
    const opel_mid_config_t *cfg = &dev->config;
    esp_err_t ret;

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
     *   - SDA high (release)
     *   - SCL high (release)
     *   - MRQ high (release)
     *   - Wait T1 (≥ 100 ms)
     *   - SDA, SCL, MRQ all expected high
     *   - SDA low (pull)
     *   - SCL low (pull)
     *   - MRQ low (pull)
     *   - Wait T2 (≥ 500 µs)
     *   - SDA, SCL, MRQ all expected low
     *   - SDA high (release)
     *   - SCL high (release)
     *   - MRQ high (release)
     *   - SDA, SCL, MRQ all expected high
     *   - Wait T2 (≥ 500 µs)
     *   - SDA low (pull)
     *   - Wait T2 (≥ 500 µs)
     *   - SDQ expected low, SCL/MRQ expected high
     *   - SDA release (high)
     *   - Wait T2 (≥ 500 µs)
     *   - SDA, SCL, MRQ all expected high
     *   - SCL low (pull)
     *   - Wait T2 (≥ 500 µs)
     *   - SCL expected low, SDA/MRQ expected high
     *   - SCL release (high)
     *   - Wait T2 (≥ 500 µs)
     *   - SDA, SCL, MRQ all expected high
     *   - MRQ low (pull)
     *   - Wait T2 (≥ 500 µs)
     *   - MRQ expected low, SDA/SCL expected high
     *   - MRQ release (high)
     *   - Wait T3 (≥ 1 ms)
     *   - SDA high (release)
     *   - SCL high (release)
     *   - MRQ high (release)
     */

    sda_high(cfg);
    scl_high(cfg);
    mrq_high(cfg);

    vTaskDelay(pdMS_TO_TICKS(T_POWER_ON_T1_MS));
    ret = expect_bus_levels(cfg, 1, 1, 1, "power-on step 1 idle-high check");
    if (ret != ESP_OK)
    {
        bus_reset_to_idle(cfg);
        return ret;
    }

    sda_low(cfg);
    scl_low(cfg);
    mrq_low(cfg);
    ets_delay_us(T_POWER_ON_T2_US);
    ret = expect_bus_levels(cfg, 0, 0, 0, "power-on step 2 all-low check");
    if (ret != ESP_OK)
    {
        bus_reset_to_idle(cfg);
        return ret;
    }

    sda_high(cfg);
    scl_high(cfg);
    mrq_high(cfg);
    ret = expect_bus_levels(cfg, 1, 1, 1, "power-on step 3 all-high check");
    if (ret != ESP_OK)
    {
        bus_reset_to_idle(cfg);
        return ret;
    }
    ets_delay_us(T_POWER_ON_T2_US);

    sda_low(cfg);
    ets_delay_us(T_POWER_ON_T2_US);
    ret = expect_bus_levels(cfg, 0, 1, 1, "power-on step 4 SDA-low check");
    if (ret != ESP_OK)
    {
        bus_reset_to_idle(cfg);
        return ret;
    }

    sda_high(cfg);
    ets_delay_us(T_POWER_ON_T2_US);
    ret = expect_bus_levels(cfg, 1, 1, 1, "power-on step 5 SDA-release check");
    if (ret != ESP_OK)
    {
        bus_reset_to_idle(cfg);
        return ret;
    }

    scl_low(cfg);
    ets_delay_us(T_POWER_ON_T2_US);
    ret = expect_bus_levels(cfg, 1, 0, 1, "power-on step 6 SCL-low check");
    if (ret != ESP_OK)
    {
        bus_reset_to_idle(cfg);
        return ret;
    }

    scl_high(cfg);
    ets_delay_us(T_POWER_ON_T2_US);
    ret = expect_bus_levels(cfg, 1, 1, 1, "power-on step 7 SCL-release check");
    if (ret != ESP_OK)
    {
        bus_reset_to_idle(cfg);
        return ret;
    }

    mrq_low(cfg);
    ets_delay_us(T_POWER_ON_T2_US);
    ret = expect_bus_levels(cfg, 1, 1, 0, "power-on step 8 MRQ-low check");
    if (ret != ESP_OK)
    {
        bus_reset_to_idle(cfg);
        return ret;
    }

    mrq_high(cfg);
    ets_delay_us(T_POWER_ON_T3_US);

    sda_high(cfg);
    scl_high(cfg);
    mrq_high(cfg);

    ESP_LOGI(TAG, "Power-on test complete");
    return ESP_OK;
}

esp_err_t opel_mid_send(opel_mid_handle_t handle,
                        const char *text,
                        const opel_mid_symbols_t *symbols)
{
    if (!handle || !text)
    {
        return ESP_ERR_INVALID_ARG;
    }

    struct opel_mid_dev_t *dev = handle;
    const opel_mid_config_t *cfg = &dev->config;

    /* Default to all symbols off when caller passes NULL. */
    static const opel_mid_symbols_t no_symbols = {0u, 0u, 0u};
    if (!symbols)
    {
        symbols = &no_symbols;
    }

    /*
     * Build the three symbol bytes (7-bit values before parity is added).
     *
     *   Radio Status (7-bit value)
     *     bit 6  COMMA          bit 5  RDS
     *     bit 4  TP             bit 3  STEREO
     *     bit 2  0              bit 1  AS
     *     bit 0  TP_BRACKET
     *
     *   Tape Status (7-bit value)
     *     bit 6  CD_IN          bit 5  DOLBY_C
     *     bit 4  DOLBY_B        bit 3  CR
     *     bit 2  CPS            bit 1  0
     *     bit 0  0
     *
     *   CD Status (7-bit value, 10-digit only)
     *     bit 6  0              bit 5  TRACK
     *     bit 4  RDM            bit 3  PGM
     *     bit 2  DISC           bit 1  0
     *     bit 0  0
     *
     * Parity will be computed and applied during transmission.
     */
    uint8_t sym[3] = {
        symbols->radio,
        symbols->tape,
        symbols->cd};

    /*
     * Address byte: the 7-bit slave address is placed in bits[7:1].
     * Parity will be applied by bus_send_byte_with_retry().
     */
    uint8_t addr_byte = dev->addr;

    /* ── Transmit frame (§Format einer Nachricht) ─────────────────────────── */
    esp_err_t ret;

    ret = bus_start(cfg);
    if (ret != ESP_OK)
    {
        /* bus_start() calls bus_reset_to_idle() on error, so no cleanup needed */
        ESP_LOGE(TAG, "opel_mid_send: bus_start() failed; frame transmission aborted");
        return ret;
    }

    /* 1. Slave address */
    ret = bus_send_byte_with_retry(cfg, addr_byte);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "opel_mid_send: address byte transmission failed (0x%02X)", addr_byte);
        bus_stop(cfg);
        bus_reset_to_idle(cfg); /* Extra recovery after stop */
        return ret;
    }

    /* 2. Symbol bytes (2 for TID-8, 3 for TID-10/MID) */
    for (uint8_t i = 0u; i < dev->sym_bytes; i++)
    {
        ret = bus_send_byte_with_retry(cfg, sym[i]);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "opel_mid_send: symbol byte %u transmission failed (0x%02X)", i, sym[i]);
            bus_stop(cfg);
            bus_reset_to_idle(cfg);
            return ret;
        }
    }

    /* 3. Data bytes — ASCII text, space-padded to display width */
    size_t text_len = strlen(text);
    for (uint8_t i = 0u; i < dev->data_bytes; i++)
    {
        char c = (i < text_len) ? text[i] : ' ';
        ret = bus_send_byte_with_retry(cfg, char_to_display_byte(c));
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "opel_mid_send: data byte %u transmission failed (char='%c')", i, c);
            bus_stop(cfg);
            bus_reset_to_idle(cfg);
            return ret;
        }
    }

    ret = bus_stop(cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "opel_mid_send: bus_stop() failed");
        bus_reset_to_idle(cfg);
    }
    return ret;
}

esp_err_t opel_mid10_send(opel_mid_handle_t handle,
                          const uint8_t *data)
{
    if (!handle || !data)
    {
        return ESP_ERR_INVALID_ARG;
    }

    struct opel_mid_dev_t *dev = handle;
    const opel_mid_config_t *cfg = &dev->config;
    uint8_t addr_byte = dev->addr;

    /* ── Transmit frame (§Format einer Nachricht) ─────────────────────────── */
    esp_err_t ret;

    ret = bus_start(cfg);
    if (ret != ESP_OK)
    {
        /* bus_start() calls bus_reset_to_idle() on error, so no cleanup needed */
        ESP_LOGE(TAG, "opel_mid_send: bus_start() failed; frame transmission aborted");
        return ret;
    }

    /* 1. Slave address */
    ret = bus_send_byte_with_retry(cfg, addr_byte);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "opel_mid10_send: address byte transmission failed (0x%02X)", addr_byte);
        bus_stop(cfg);
        bus_reset_to_idle(cfg); /* Extra recovery after stop */
        return ret;
    }

    /* 2. Data/mode bytes */
    for (uint8_t i = 0u; i < OPEL_MID_RDS_FRAME_BYTES; i++)
    {
        ret = bus_send_byte_with_retry(cfg, data[i]);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "opel_mid10_send: data byte %u transmission failed (data=0x%02X)", i, data[i]);
            bus_stop(cfg);
            bus_reset_to_idle(cfg);
            return ret;
        }
    }

    ret = bus_stop(cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "opel_mid_send: bus_stop() failed");
        bus_reset_to_idle(cfg);
    }
    return ret;
}

esp_err_t opel_mid_set_time(opel_mid_handle_t handle,
                            const uint8_t *rds_time_block)
{
    if (!handle || !rds_time_block)
    {
        return ESP_ERR_INVALID_ARG;
    }

    struct opel_mid_dev_t *dev = handle;
    const opel_mid_config_t *cfg = &dev->config;

    /* Decode 4-byte RDS time block into components */
    uint8_t offset_byte = rds_time_block[0] & 0x1F;
    uint8_t mjd_bits_16_14 = (rds_time_block[0] >> 5) & 0x07;
    uint8_t utc_minute = rds_time_block[1] & 0x3F;
    uint8_t mjd_bits_13_12 = (rds_time_block[1] >> 6) & 0x03;
    uint8_t utc_hour = rds_time_block[2] & 0x1F;
    uint8_t mjd_bits_11_9 = (rds_time_block[2] >> 5) & 0x07;
    uint8_t mjd_bits_8_1 = rds_time_block[3];

    /* Reconstruct 17-bit MJD from scattered bits */
    uint32_t mjd = ((uint32_t)mjd_bits_16_14 << 14) |
                   ((uint32_t)mjd_bits_13_12 << 12) |
                   ((uint32_t)mjd_bits_11_9 << 9) |
                   ((uint32_t)mjd_bits_8_1 << 1);

    /* Decode offset: sign bit in bit 5, value in bits[4:0] */
    int8_t local_offset_half_hours = (int8_t)((offset_byte & 0x20) ? -(offset_byte & 0x1F) : (offset_byte & 0x1F));

    /* Encode into 8-byte transmission frame using the same layout as the original implementation */
    uint8_t rawPacket[8];

    rawPacket[0] = OPEL_MID_RDS_CT_GROUP;

    uint8_t offset_abs = (uint8_t)((local_offset_half_hours < 0) ? -local_offset_half_hours : local_offset_half_hours);
    uint8_t offsetField = (local_offset_half_hours < 0) ? (uint8_t)(offset_abs | 0x20u) : offset_abs;
    rawPacket[1] = offsetField;

    rawPacket[2] = utc_minute;
    rawPacket[3] = utc_hour;

    /* 17-bit MJD spread across protocol bytes */
    rawPacket[4] = (mjd >> 9) & 0x7F; /* Upper bits */
    rawPacket[5] = (mjd >> 2) & 0x7F; /* Middle block */
    rawPacket[6] = ((mjd & 0x03) << 5) | 0x03;
    rawPacket[7] = (mjd >> 14) & 0x07;

    esp_err_t ret = bus_start(cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "opel_mid_set_time: bus_start() failed");
        return ret;
    }

    for (size_t i = 0u; i < sizeof(rawPacket); i++)
    {
        ret = bus_send_byte_with_retry(cfg, rawPacket[i]);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "opel_mid_set_time: frame byte %u failed (0x%02X)", (unsigned)i, rawPacket[i]);
            bus_stop(cfg);
            bus_reset_to_idle(cfg);
            return ret;
        }
    }
    ret = bus_stop(cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "opel_mid_set_time: bus_stop() failed");
        bus_reset_to_idle(cfg);
        return ret;
    }

    ESP_LOGI(TAG, "Set time sync: mjd=%lu utc=%02u:%02u offset_half_hours=%d",
             (unsigned long)mjd, utc_hour, utc_minute, local_offset_half_hours);
    return ESP_OK;
}
