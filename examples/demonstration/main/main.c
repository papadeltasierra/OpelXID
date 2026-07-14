#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_console.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "opel_mid.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

static const char *TAG = "demonstration";

/* ── Configuration ────────────────────────────────────────────────────────── */

/**
 * Display type and GPIO pins are configured via SDKCONFIG.
 * Modify via: idf.py menuconfig -> OpelXID Demonstration Configuration
 */
#ifdef CONFIG_OPEL_DISPLAY_TYPE_TID_8
#define DISPLAY_TYPE OPEL_MID_TYPE_TID_8
#else
#define DISPLAY_TYPE OPEL_MID_TYPE_TID_10
#endif

#define PIN_SDA  CONFIG_OPEL_DISPLAY_PIN_SDA
#define PIN_SCL  CONFIG_OPEL_DISPLAY_PIN_SCL
#define PIN_MRQ  CONFIG_OPEL_DISPLAY_PIN_MRQ

/* ── Character mapping table (Option 3) ───────────────────────────────────── */

/**
 * @brief Character code mapping.
 *
 * Maps 7-bit display payload values (0x00–0x7F) to display behavior.
 * Initially configured for standard ASCII (0x20–0x7E). Can be extended
 * with custom mappings for verified control/del behavior.
 *
 * This structure allows:
 *   1. Testing non-printable 7-bit codes (0x00–0x1F, 0x7F)
 *   2. Documenting verified mappings as they're discovered
 *   3. Per-application customization (e.g., if a specific vehicle supports
 *      additional segment-based characters)
 */
typedef struct {
    uint8_t code;        /* Display code (what's sent on the bus) */
    const char *display; /* What it shows on the actual hardware */
    const char *note;    /* Notes (ASCII range, custom segment, etc.) */
} char_map_entry_t;

static const char_map_entry_t char_map[] = {
    /* Standard printable ASCII (0x20–0x7E) — verified */
    {0x20, " ", "space"},
    {0x21, "!", "exclamation"},
    {0x22, "\"", "quote"},
    {0x23, "#", "hash"},
    {0x24, "$", "dollar"},
    {0x25, "%", "percent"},
    {0x26, "&", "ampersand"},
    {0x27, "'", "apostrophe"},
    {0x28, "(", "paren-left"},
    {0x29, ")", "paren-right"},
    {0x2A, "*", "asterisk"},
    {0x2B, "+", "plus"},
    {0x2C, ",", "comma"},
    {0x2D, "-", "hyphen"},
    {0x2E, ".", "period"},
    {0x2F, "/", "slash"},
    {0x30, "0", "digit-0"},
    {0x31, "1", "digit-1"},
    {0x32, "2", "digit-2"},
    {0x33, "3", "digit-3"},
    {0x34, "4", "digit-4"},
    {0x35, "5", "digit-5"},
    {0x36, "6", "digit-6"},
    {0x37, "7", "digit-7"},
    {0x38, "8", "digit-8"},
    {0x39, "9", "digit-9"},
    {0x3A, ":", "colon"},
    {0x3B, ";", "semicolon"},
    {0x3C, "<", "less-than"},
    {0x3D, "=", "equals"},
    {0x3E, ">", "greater-than"},
    {0x3F, "?", "question"},
    {0x40, "@", "at-sign"},
    {0x41, "A", "letter-A"},
    {0x42, "B", "letter-B"},
    {0x43, "C", "letter-C"},
    {0x44, "D", "letter-D"},
    {0x45, "E", "letter-E"},
    {0x46, "F", "letter-F"},
    {0x47, "G", "letter-G"},
    {0x48, "H", "letter-H"},
    {0x49, "I", "letter-I"},
    {0x4A, "J", "letter-J"},
    {0x4B, "K", "letter-K"},
    {0x4C, "L", "letter-L"},
    {0x4D, "M", "letter-M"},
    {0x4E, "N", "letter-N"},
    {0x4F, "O", "letter-O"},
    {0x50, "P", "letter-P"},
    {0x51, "Q", "letter-Q"},
    {0x52, "R", "letter-R"},
    {0x53, "S", "letter-S"},
    {0x54, "T", "letter-T"},
    {0x55, "U", "letter-U"},
    {0x56, "V", "letter-V"},
    {0x57, "W", "letter-W"},
    {0x58, "X", "letter-X"},
    {0x59, "Y", "letter-Y"},
    {0x5A, "Z", "letter-Z"},
    {0x5B, "[", "bracket-left"},
    {0x5C, "\\", "backslash"},
    {0x5D, "]", "bracket-right"},
    {0x5E, "^", "caret"},
    {0x5F, "_", "underscore"},
    {0x60, "`", "backtick"},
    {0x61, "a", "letter-a"},
    {0x62, "b", "letter-b"},
    {0x63, "c", "letter-c"},
    {0x64, "d", "letter-d"},
    {0x65, "e", "letter-e"},
    {0x66, "f", "letter-f"},
    {0x67, "g", "letter-g"},
    {0x68, "h", "letter-h"},
    {0x69, "i", "letter-i"},
    {0x6A, "j", "letter-j"},
    {0x6B, "k", "letter-k"},
    {0x6C, "l", "letter-l"},
    {0x6D, "m", "letter-m"},
    {0x6E, "n", "letter-n"},
    {0x6F, "o", "letter-o"},
    {0x70, "p", "letter-p"},
    {0x71, "q", "letter-q"},
    {0x72, "r", "letter-r"},
    {0x73, "s", "letter-s"},
    {0x74, "t", "letter-t"},
    {0x75, "u", "letter-u"},
    {0x76, "v", "letter-v"},
    {0x77, "w", "letter-w"},
    {0x78, "x", "letter-x"},
    {0x79, "y", "letter-y"},
    {0x7A, "z", "letter-z"},
    {0x7B, "{", "brace-left"},
    {0x7C, "|", "pipe"},
    {0x7D, "}", "brace-right"},
    {0x7E, "~", "tilde"},

    /* Non-printable 7-bit values (0x00–0x1F, 0x7F) — to be discovered */
    /* Placeholder entries for testing. Replace with findings from actual hardware. */
    {0x00, "?", "NUL (unknown)"},
    {0x01, "?", "SOH (unknown)"},
    {0x7F, "?", "DEL (unknown)"},
};

#define CHAR_MAP_LEN (sizeof(char_map) / sizeof(char_map[0]))

/* ── Helper functions ─────────────────────────────────────────────────────── */

/**
 * @brief Wait for user to press <return>.
 */
static void wait_for_return(void)
{
    printf("Press <return> to continue...\n");
    fflush(stdout);
    int c;
    do {
        c = fgetc(stdin);
    } while (c != '\n' && c != '\r' && c != EOF);
}

/**
 * @brief Get display width based on type.
 */
static int get_display_width(opel_mid_type_t type)
{
    return (type == OPEL_MID_TYPE_TID_8) ? 8 : 10;
}

/**
 * @brief Parse one decimal digit sequence into an integer.
 */
static int parse_decimal_n(const char *s, size_t n, int *out)
{
    int v = 0;
    for (size_t i = 0; i < n; i++) {
        if (!isdigit((unsigned char)s[i])) {
            return 0;
        }
        v = (v * 10) + (s[i] - '0');
    }
    *out = v;
    return 1;
}

/**
 * @brief Parse UTC timestamp in YYYYMMDDTHHmmss format.
 */
static int parse_utc_timestamp(const char *ts,
                               int *year,
                               int *month,
                               int *day,
                               int *hour,
                               int *minute,
                               int *second)
{
    if (!ts || strlen(ts) != 15u || ts[8] != 'T') {
        return 0;
    }

    if (!parse_decimal_n(&ts[0], 4u, year) ||
        !parse_decimal_n(&ts[4], 2u, month) ||
        !parse_decimal_n(&ts[6], 2u, day) ||
        !parse_decimal_n(&ts[9], 2u, hour) ||
        !parse_decimal_n(&ts[11], 2u, minute) ||
        !parse_decimal_n(&ts[13], 2u, second)) {
        return 0;
    }

    if (*month < 1 || *month > 12 || *day < 1 || *day > 31 ||
        *hour > 23 || *minute > 59 || *second > 59) {
        return 0;
    }

    return 1;
}

/**
 * @brief Format a test string to fit the display width.
 */
static void format_text(char *out, size_t max_len, const char *text, int width)
{
    memset(out, ' ', max_len);
    size_t text_len = strlen(text);
    size_t copy_len = (text_len < (size_t)width) ? text_len : width;
    memcpy(out, text, copy_len);
    out[width] = '\0';
}

/**
 * @brief Demo: Full character set.
 */
static void demo_charset(opel_mid_handle_t display, opel_mid_type_t type)
{
    int width = get_display_width(type);
    char text[16];

    printf("\n=== CHARACTER SET DEMONSTRATION ===\n");
    printf("Displaying all printable ASCII characters (0x20–0x7E, %lu chars).\n", CHAR_MAP_LEN - (0x7E - 0x20 + 1));
    printf("Display width: %d characters.\n\n", width);

    /* Show characters in groups of width size */
    for (int i = 0x20; i <= 0x7E; i += width) {
        int group_size = (0x7E - i + 1 < width) ? (0x7E - i + 1) : width;

        /* Build the display string */
        memset(text, ' ', width);
        for (int j = 0; j < group_size; j++) {
            text[j] = (char)(i + j);
        }
        text[width] = '\0';

        printf("Characters 0x%02X–0x%02X: %s\n", i, i + group_size - 1, text);

        /* Send to display */
        esp_err_t ret = opel_mid_send(display, text, NULL);
        if (ret != ESP_OK) {
            printf("ERROR: opel_mid_send() failed: 0x%X\n", ret);
            return;
        }

        wait_for_return();
    }

    printf("\nCharacter set demonstration complete.\n");
}

/**
 * @brief Demo: Non-printable 7-bit code testing (Option 2).
 *
 * Allows testing of non-printable 7-bit payload values to discover what
 * the display shows. Useful for documenting control-code behavior.
 */
static void demo_extended_chars(opel_mid_handle_t display, opel_mid_type_t type)
{
    int width = get_display_width(type);
    char text[16];

    printf("\n=== 7-BIT NON-PRINTABLE CODE TESTING ===\n");
    printf("Testing payload codes 0x00–0x1F (control chars) and 0x7F (DEL).\n");
    printf("Transport is 7-bit data + odd parity; 0x80–0xFF are not distinct payload codes.\n");
    printf("Observe what appears on the hardware and document findings.\n\n");

    /* Test control characters (0x00–0x1F) */
    printf("Control characters (0x00–0x1F):\n");
    for (int i = 0x00; i <= 0x1F; i += 2) {
        char c1 = (char)i;
        char c2 = (char)(i + 1);

        memset(text, ' ', width);
        text[0] = c1;
        if (width > 1) text[1] = c2;
        text[width] = '\0';

        printf("0x%02X / 0x%02X: Sending...\n", i, i + 1);

        /* Send each as a single character for clarity */
        memset(text, ' ', width);
        text[0] = c1;
        text[width] = '\0';

        esp_err_t ret = opel_mid_send(display, text, NULL);
        if (ret != ESP_OK) {
            printf("  ERROR: 0x%02X failed (0x%X)\n", i, ret);
        } else {
            printf("  0x%02X sent successfully. Note what appears on display.\n", i);
        }

        wait_for_return();
    }

    /* Test DEL (0x7F) */
    printf("\nDEL (0x7F):\n");
    for (int i = 0x7F; i <= 0x7F; i++) {
        char c1 = (char)i;

        printf("0x%02X: Sending...\n", i);

        memset(text, ' ', width);
        text[0] = c1;
        text[width] = '\0';

        esp_err_t ret = opel_mid_send(display, text, NULL);
        if (ret != ESP_OK) {
            printf("  ERROR: 0x%02X failed (0x%X)\n", i, ret);
        } else {
            printf("  0x%02X sent successfully. Note what appears on display.\n", i);
        }

        wait_for_return();
    }

    printf("\nNon-printable 7-bit code testing complete.\n");
}

/**
 * @brief Demo: Symbol toggling.
 */
static void demo_symbols(opel_mid_handle_t display, opel_mid_type_t type)
{
    int width = get_display_width(type);
    char text[16];

    printf("\n=== SYMBOL DEMONSTRATION ===\n");
    printf("Toggling all available symbols.\n");
    printf("Radio symbols: COMMA, RDS, TP, STEREO, AS, TP_BRACKET\n");
    printf("Tape symbols: CD_IN, DOLBY_C, DOLBY_B, CR, CPS\n");
    if (type == OPEL_MID_TYPE_TID_10) {
        printf("CD symbols: TRACK, RDM, PGM, DISC\n");
    }
    printf("\n");

    format_text(text, sizeof(text), "SYMBOLS", width);

    /* Radio symbols */
    printf("Radio Status Symbols:\n");
    opel_mid_symbols_t symbols = {0, 0, 0};

    if (DISPLAY_TYPE == OPEL_MID_TYPE_TID_8 || DISPLAY_TYPE == OPEL_MID_TYPE_TID_10) {
        uint8_t radio_flags[] = {
            OPEL_MID_SYM_COMMA,
            OPEL_MID_SYM_RDS,
            OPEL_MID_SYM_TP,
            OPEL_MID_SYM_STEREO,
            OPEL_MID_SYM_AS,
            OPEL_MID_SYM_TP_BRACKET,
        };
        const char *radio_names[] = {"COMMA", "RDS", "TP", "STEREO", "AS", "TP_BRACKET"};

        for (size_t i = 0; i < sizeof(radio_flags) / sizeof(radio_flags[0]); i++) {
            symbols.radio = radio_flags[i];
            symbols.tape = 0;
            symbols.cd = 0;

            printf("  %s: ", radio_names[i]);
            fflush(stdout);

            esp_err_t ret = opel_mid_send(display, text, &symbols);
            if (ret != ESP_OK) {
                printf("ERROR (0x%X)\n", ret);
            } else {
                printf("ON\n");
            }

            wait_for_return();
        }
    }

    /* Tape symbols */
    printf("Tape Status Symbols:\n");
    if (DISPLAY_TYPE == OPEL_MID_TYPE_TID_8 || DISPLAY_TYPE == OPEL_MID_TYPE_TID_10) {
        uint8_t tape_flags[] = {
            OPEL_MID_SYM_CD_IN,
            OPEL_MID_SYM_DOLBY_C,
            OPEL_MID_SYM_DOLBY_B,
            OPEL_MID_SYM_CR,
            OPEL_MID_SYM_CPS,
        };
        const char *tape_names[] = {"CD_IN", "DOLBY_C", "DOLBY_B", "CR", "CPS"};

        for (size_t i = 0; i < sizeof(tape_flags) / sizeof(tape_flags[0]); i++) {
            symbols.radio = 0;
            symbols.tape = tape_flags[i];
            symbols.cd = 0;

            printf("  %s: ", tape_names[i]);
            fflush(stdout);

            esp_err_t ret = opel_mid_send(display, text, &symbols);
            if (ret != ESP_OK) {
                printf("ERROR (0x%X)\n", ret);
            } else {
                printf("ON\n");
            }

            wait_for_return();
        }
    }

    /* CD symbols (10-digit only) */
    if (type == OPEL_MID_TYPE_TID_10) {
        printf("CD Status Symbols (10-digit TID/MID only):\n");
        uint8_t cd_flags[] = {
            OPEL_MID_SYM_TRACK,
            OPEL_MID_SYM_RDM,
            OPEL_MID_SYM_PGM,
            OPEL_MID_SYM_DISC,
        };
        const char *cd_names[] = {"TRACK", "RDM", "PGM", "DISC"};

        for (size_t i = 0; i < sizeof(cd_flags) / sizeof(cd_flags[0]); i++) {
            symbols.radio = 0;
            symbols.tape = 0;
            symbols.cd = cd_flags[i];

            printf("  %s: ", cd_names[i]);
            fflush(stdout);

            esp_err_t ret = opel_mid_send(display, text, &symbols);
            if (ret != ESP_OK) {
                printf("ERROR (0x%X)\n", ret);
            } else {
                printf("ON\n");
            }

            wait_for_return();
        }
    }

    /* All symbols on */
    printf("All symbols ON: ");
    fflush(stdout);
    symbols.radio = 0xFF & 0xFE; /* All radio flags except parity bit */
    symbols.tape = 0xFF & 0xFE;
    symbols.cd = 0xFF & 0xFE;

    esp_err_t ret = opel_mid_send(display, text, &symbols);
    if (ret != ESP_OK) {
        printf("ERROR (0x%X)\n", ret);
    } else {
        printf("ON\n");
    }

    wait_for_return();

    /* All symbols off */
    printf("All symbols OFF: ");
    fflush(stdout);
    symbols.radio = 0;
    symbols.tape = 0;
    symbols.cd = 0;

    ret = opel_mid_send(display, text, &symbols);
    if (ret != ESP_OK) {
        printf("ERROR (0x%X)\n", ret);
    } else {
        printf("OFF\n");
    }

    printf("\nSymbol demonstration complete.\n");
}

/**
 * @brief Demo: Edge cases.
 */
static void demo_edge_cases(opel_mid_handle_t display, opel_mid_type_t type)
{
    int width = get_display_width(type);
    char text[16];

    printf("\n=== EDGE CASES ===\n");

    /* Empty string */
    printf("Empty string (should show spaces): ");
    fflush(stdout);
    memset(text, ' ', width);
    text[width] = '\0';
    esp_err_t ret = opel_mid_send(display, "", NULL);
    printf("%s\n", ret == ESP_OK ? "OK" : "ERROR");
    wait_for_return();

    /* Long text (truncation) */
    printf("Text longer than display width (should truncate): ");
    fflush(stdout);
    const char *long_text = "THIS_TEXT_IS_WAY_TOO_LONG";
    ret = opel_mid_send(display, long_text, NULL);
    printf("%s (displayed: %.*s)\n", ret == ESP_OK ? "OK" : "ERROR", width, long_text);
    wait_for_return();

    /* Short text (padding) */
    printf("Short text (should be space-padded): ");
    fflush(stdout);
    ret = opel_mid_send(display, "HI", NULL);
    printf("%s\n", ret == ESP_OK ? "OK" : "ERROR");
    wait_for_return();

    /* Non-printable characters (become spaces) */
    printf("Non-printable characters (0x01, 0x7F, 0xFF treated as non-printable and become spaces): ");
    fflush(stdout);
    text[0] = '\x01';
    text[1] = 'T';
    text[2] = '\x7F';
    text[3] = 'S';
    text[4] = '\xFF';
    for (int i = 5; i < width; i++) text[i] = ' ';
    text[width] = '\0';
    ret = opel_mid_send(display, text, NULL);
    printf("%s\n", ret == ESP_OK ? "OK" : "ERROR");
    wait_for_return();

    printf("Edge case testing complete.\n");
}

/**
 * @brief Demo: Hardware time sync from UTC timestamp input.
 */
static void demo_time_sync(opel_mid_handle_t display)
{
    char input[64];
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    printf("\n=== HARDWARE TIME SYNC (0x60) ===\n");
    printf("Enter UTC timestamp as YYYYMMDDTHHmmss (example: 20260714T154500).\n");
    printf("Century and seconds are ignored by the display protocol.\n\n");
    printf("Timestamp: ");
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("Input error.\n");
        return;
    }

    input[strcspn(input, "\r\n")] = '\0';

    if (!parse_utc_timestamp(input, &year, &month, &day, &hour, &minute, &second)) {
        printf("Invalid format. Expected exactly YYYYMMDDTHHmmss with valid ranges.\n");
        return;
    }

    uint8_t year_2digit = (uint8_t)(year % 100);
    esp_err_t ret = opel_mid_set_time(display,
                                      (uint8_t)day,
                                      (uint8_t)month,
                                      year_2digit,
                                      (uint8_t)hour,
                                      (uint8_t)minute);

    if (ret != ESP_OK) {
        printf("Time sync failed: 0x%X\n", ret);
        return;
    }

    printf("Time sync sent: input=%s -> day=%02d month=%02d year=%02u hour=%02d minute=%02d (seconds ignored: %02d)\n",
           input, day, month, year_2digit, hour, minute, second);
    wait_for_return();
}

/**
 * @brief Main menu.
 */
static void main_menu(opel_mid_handle_t display, opel_mid_type_t type)
{
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║   OpelXID MID/TID Demonstration Menu   ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║ 1. Character Set (all printable ASCII) ║\n");
    printf("║ 2. Non-printable codes (0x00–0x1F,7F) ║\n");
    printf("║ 3. All Symbols (Radio/Tape/CD)        ║\n");
    printf("║ 4. Edge Cases                          ║\n");
    printf("║ 5. Run All Demonstrations             ║\n");
    printf("║ 6. Time Sync (UTC timestamp)          ║\n");
    printf("║ 7. Exit                                ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("Display type: %s (%d characters)\n\n",
           (type == OPEL_MID_TYPE_TID_8) ? "TID-8" : "TID-10/MID",
           get_display_width(type));

    while (1) {
        printf("Select (1–7): ");
        fflush(stdout);

        char input[16];
        if (fgets(input, sizeof(input), stdin) == NULL) {
            continue;
        }

        switch (input[0]) {
        case '1':
            demo_charset(display, type);
            break;
        case '2':
            demo_extended_chars(display, type);
            break;
        case '3':
            demo_symbols(display, type);
            break;
        case '4':
            demo_edge_cases(display, type);
            break;
        case '5':
            demo_charset(display, type);
            demo_extended_chars(display, type);
            demo_symbols(display, type);
            demo_edge_cases(display, type);
            printf("\n=== ALL DEMONSTRATIONS COMPLETE ===\n");
            break;
        case '6':
            demo_time_sync(display);
            break;
        case '7':
            printf("Exiting.\n");
            return;
        default:
            printf("Invalid selection. Try again.\n");
        }
    }
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "Starting OpelXID Demonstration");

    /* Initialize the display */
    opel_mid_config_t config = {
        .pin_sda = (gpio_num_t)PIN_SDA,
        .pin_scl = (gpio_num_t)PIN_SCL,
        .pin_mrq = (gpio_num_t)PIN_MRQ,
        .type    = DISPLAY_TYPE,
    };

    opel_mid_handle_t display = NULL;
    esp_err_t ret = opel_mid_init(&config, &display);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "opel_mid_init() failed: 0x%X", ret);
        return;
    }

    /* Send power-on test sequence */
    ret = opel_mid_power_on(display);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "opel_mid_power_on() failed: 0x%X", ret);
        opel_mid_deinit(display);
        return;
    }

    printf("\nOpelXID MID/TID Demonstration\n");
    printf("==============================\n");
    printf("\nDisplay initialized and power-on test sent.\n");
    printf("The display should now be ready.\n\n");

    /* Run the menu */
    main_menu(display, DISPLAY_TYPE);

    /* Cleanup */
    opel_mid_deinit(display);
    ESP_LOGI(TAG, "Demonstration complete");
}
