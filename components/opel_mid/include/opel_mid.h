#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Display type ─────────────────────────────────────────────────────────── */

    /**
     * @brief Opel display variant.
     *
     * The 8-digit TID is fitted to the Astra F and Corsa B/Tigra.
     * The 10-digit TID/MID is fitted to the Astra G and Corsa C.
     */
    typedef enum
    {
        OPEL_MID_TYPE_TID_8 = 0,  /*!< 8-digit TID.  Slave address 0x4A.
                                        Frame: 2 symbol bytes + 8 data bytes. */
        OPEL_MID_TYPE_TID_10 = 1, /*!< 10-digit TID/MID.  Slave address 0x4D.
                                        Frame: 3 symbol bytes + 10 data bytes. */
    } opel_mid_type_t;

    /* ── Configuration ────────────────────────────────────────────────────────── */

    /**
     * @brief Device configuration passed to opel_mid_init().
     *
     * All three lines (SDA, SCL, MRQ) must be open-drain capable GPIOs.
     * The display provides its own pull-up resistors on the slave side.
     */
    typedef struct
    {
        gpio_num_t pin_sda;   /*!< SDA – serial data                     */
        gpio_num_t pin_scl;   /*!< SCL – serial clock                    */
        gpio_num_t pin_mrq;   /*!< MRQ – master request (extra I2C line) */
        opel_mid_type_t type; /*!< Display variant                       */
    } opel_mid_config_t;

/* ── Symbol flags ─────────────────────────────────────────────────────────── */

/** Radio Status byte (symbol byte 1) */
#define OPEL_MID_SYM_COMMA (1u << 7)      /*!< Comma symbol            */
#define OPEL_MID_SYM_RDS (1u << 6)        /*!< RDS symbol              */
#define OPEL_MID_SYM_TP (1u << 5)         /*!< TP symbol               */
#define OPEL_MID_SYM_STEREO (1u << 4)     /*!< Stereo symbol           */
#define OPEL_MID_SYM_AS (1u << 2)         /*!< AS symbol               */
#define OPEL_MID_SYM_TP_BRACKET (1u << 1) /*!< Bracket around TP       */

/** Tape Status byte (symbol byte 2) */
#define OPEL_MID_SYM_CD_IN (1u << 7)   /*!< CD-In symbol            */
#define OPEL_MID_SYM_DOLBY_C (1u << 6) /*!< Dolby C symbol          */
#define OPEL_MID_SYM_DOLBY_B (1u << 5) /*!< Dolby B symbol          */
#define OPEL_MID_SYM_CR (1u << 4)      /*!< cr symbol               */
#define OPEL_MID_SYM_CPS (1u << 3)     /*!< CPS symbol              */

/** CD Status byte (symbol byte 3 – 10-digit displays only) */
#define OPEL_MID_SYM_TRACK (1u << 6) /*!< Track symbol            */
#define OPEL_MID_SYM_RDM (1u << 5)   /*!< RDM symbol              */
#define OPEL_MID_SYM_PGM (1u << 4)   /*!< PGM symbol              */
#define OPEL_MID_SYM_DISC (1u << 3)  /*!< DISC symbol             */

    /**
     * @brief Symbol byte values for one display frame.
     *
     * Populate the relevant fields using the OPEL_MID_SYM_* flags above.
     * Parity bits (bit 0 of each byte) are computed automatically by the driver.
     * The @p cd field is ignored for 8-digit displays.
     */
    typedef struct
    {
        uint8_t radio; /*!< Radio status byte (byte 1) */
        uint8_t tape;  /*!< Tape  status byte (byte 2) */
        uint8_t cd;    /*!< CD    status byte (byte 3, 10-digit only) */
    } opel_mid_symbols_t;

    /* ── Handle ───────────────────────────────────────────────────────────────── */

    /** Opaque device handle returned by opel_mid_init(). */
    typedef struct opel_mid_dev_t *opel_mid_handle_t;

    /* ── API ──────────────────────────────────────────────────────────────────── */

    /**
     * @brief Initialise the library and configure GPIO pins.
     *
     * All three GPIO lines are configured as open-drain outputs and set idle-high.
     *
     * @param[in]  config     Pointer to a populated configuration structure.
     * @param[out] out_handle Handle to pass to all other API functions.
     *
     * @return ESP_OK              on success.
     * @return ESP_ERR_INVALID_ARG if @p config or @p out_handle is NULL.
     * @return ESP_ERR_NO_MEM      if allocation fails.
     * @return other               propagated from gpio_config().
     */
    esp_err_t opel_mid_init(const opel_mid_config_t *config,
                            opel_mid_handle_t *out_handle);

    /**
     * @brief Release resources acquired by opel_mid_init().
     *
     * @param handle Handle obtained from opel_mid_init().
     *
     * @return ESP_OK              on success.
     * @return ESP_ERR_INVALID_ARG if @p handle is NULL.
     */
    esp_err_t opel_mid_deinit(opel_mid_handle_t handle);

    /**
     * @brief Send the power-on test sequence.
     *
     * Must be called once after the AA (Antenna Amplifier / radio-on) signal
     * is asserted and before the first opel_mid_send().
     * Timing requirements (from §Power on Test):
     *   T1 (SCL pulse)  100–500 ms
     *   T2 (MRQ pulse)  500 µs – 1 ms
     *   T3 (gap to data) 1–2 ms
     *
     * @param handle Handle obtained from opel_mid_init().
     *
     * @return ESP_OK              on success.
     * @return ESP_ERR_INVALID_ARG if @p handle is NULL.
     */
    esp_err_t opel_mid_power_on(opel_mid_handle_t handle);

    /**
     * @brief Send a text frame to the display.
     *
     * The bus protocol follows the modified I2C scheme documented at
     * https://wiki.carluccio.de/index.php/Opel_TID :
     *   – 3-wire bus: SDA, SCL, MRQ
     *   – MSB-first, 7 data bits + 1 odd parity bit per byte
     *   – Per-byte ACK; up to 3 retries on parity error before aborting
     *
     * @p text is space-padded to the display width if shorter; characters
     * beyond the display width are silently truncated.
     *
     * @param handle  Handle obtained from opel_mid_init().
     * @param text    Null-terminated ASCII string.  Must not be NULL.
     * @param symbols Symbol flags, or NULL to clear all symbols.
     *
     * @return ESP_OK              on success.
     * @return ESP_ERR_INVALID_ARG if @p handle or @p text is NULL.
     * @return ESP_ERR_TIMEOUT     if the slave does not respond.
     */
    esp_err_t opel_mid_send(opel_mid_handle_t handle,
                            const char *text,
                            const opel_mid_symbols_t *symbols);

    // !!PDS: Explain this.
    /**
     * @brief Send a text frame to the display.
     *
     * The bus protocol follows the modified I2C scheme documented at
     * https://wiki.carluccio.de/index.php/Opel_TID :
     *   – 3-wire bus: SDA, SCL, MRQ
     *   – MSB-first, 7 data bits + 1 odd parity bit per byte
     *   – Per-byte ACK; up to 3 retries on parity error before aborting
     *
     * @p text is space-padded to the display width if shorter; characters
     * beyond the display width are silently truncated.
     *
     * @param handle  Handle obtained from opel_mid_init().
     * @param text    Null-terminated ASCII string.  Must not be NULL.
     * @param symbols Symbol flags, or NULL to clear all symbols.
     *
     * @return ESP_OK              on success.
     * @return ESP_ERR_INVALID_ARG if @p handle or @p text is NULL.
     * @return ESP_ERR_TIMEOUT     if the slave does not respond.
     */
    esp_err_t opel_mid10_send(opel_mid_handle_t handle,
                              const uint8_t *data);

    /**
     * @brief Send an RDS-style clock/time update frame.
     *
     * @param handle                   Handle obtained from opel_mid_init().
     * @param mjd                      Modified Julian Date (17-bit value).
     * @param utc_hour                 UTC hour (0–23).
     * @param utc_minute               UTC minute (0–59).
     * @param local_offset_half_hours  Local offset from UTC in 30-minute steps.
     *                                 Negative values indicate west of UTC.
     *
     * @return ESP_OK              on success.
     * @return ESP_ERR_INVALID_ARG if input is out of range.
     * @return ESP_ERR_TIMEOUT     if the slave does not respond.
     */
    esp_err_t opel_mid_set_time(opel_mid_handle_t handle,
                                uint32_t mjd,
                                uint8_t utc_hour,
                                uint8_t utc_minute,
                                int8_t local_offset_half_hours);

#ifdef __cplusplus
}
#endif
