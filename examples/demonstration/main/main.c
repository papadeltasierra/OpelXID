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
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "demonstration";

#define DEMO_STEP_DELAY_MS 1200

static opel_mid_handle_t s_display = NULL;
static opel_mid_type_t s_display_type;
static int32_t s_utc_offset_seconds = 0;
static int s_utc_offset_is_set = 0;
static int s_time_initialized = 0;

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

#define PIN_SDA CONFIG_OPEL_DISPLAY_PIN_SDA
#define PIN_SCL CONFIG_OPEL_DISPLAY_PIN_SCL
#define PIN_MRQ CONFIG_OPEL_DISPLAY_PIN_MRQ

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
typedef struct
{
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
 * @brief Give time to observe output between demo steps.
 */
static void wait_between_steps(void)
{
    vTaskDelay(pdMS_TO_TICKS(DEMO_STEP_DELAY_MS));
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
    for (size_t i = 0; i < n; i++)
    {
        if (!isdigit((unsigned char)s[i]))
        {
            return 0;
        }
        v = (v * 10) + (s[i] - '0');
    }
    *out = v;
    return 1;
}

/**
 * @brief Check if year is leap year in Gregorian calendar.
 */
static int is_leap_year(int year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

/**
 * @brief Return number of days in month.
 */
static int days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2)
    {
        return is_leap_year(year) ? 29 : 28;
    }
    return days[month - 1];
}

/**
 * @brief Parse RFC3339 timestamp (YYYY-MM-DDTHH:MM:SSZ or +/-HH:MM).
 */
static int parse_rfc3339_timestamp(const char *ts,
                                   int *year,
                                   int *month,
                                   int *day,
                                   int *hour,
                                   int *minute,
                                   int *second,
                                   int *offset_seconds)
{
    if (!ts)
    {
        return 0;
    }

    size_t len = strlen(ts);
    if (!((len == 20u && ts[19] == 'Z') || (len == 25u && (ts[19] == '+' || ts[19] == '-'))))
    {
        return 0;
    }

    if (ts[4] != '-' || ts[7] != '-' || ts[10] != 'T' || ts[13] != ':' || ts[16] != ':')
    {
        return 0;
    }

    if (!parse_decimal_n(&ts[0], 4u, year) ||
        !parse_decimal_n(&ts[5], 2u, month) ||
        !parse_decimal_n(&ts[8], 2u, day) ||
        !parse_decimal_n(&ts[11], 2u, hour) ||
        !parse_decimal_n(&ts[14], 2u, minute) ||
        !parse_decimal_n(&ts[17], 2u, second))
    {
        return 0;
    }

    if (*year < 1970 || *month < 1 || *month > 12 || *day < 1 ||
        *hour > 23 || *minute > 59 || *second > 59)
    {
        return 0;
    }

    if (*day > days_in_month(*year, *month))
    {
        return 0;
    }

    if (len == 20u)
    {
        *offset_seconds = 0;
        return 1;
    }

    if (ts[22] != ':')
    {
        return 0;
    }

    int off_hour = 0;
    int off_minute = 0;
    if (!parse_decimal_n(&ts[20], 2u, &off_hour) || !parse_decimal_n(&ts[23], 2u, &off_minute))
    {
        return 0;
    }

    if (off_hour > 23 || off_minute > 59)
    {
        return 0;
    }

    int sign = (ts[19] == '-') ? -1 : 1;
    *offset_seconds = sign * ((off_hour * 3600) + (off_minute * 60));
    return 1;
}

/**
 * @brief Convert civil date/time components to Unix epoch seconds.
 */
static int64_t epoch_seconds_from_ymdhms(int year, int month, int day, int hour, int minute, int second)
{
    int y = year;
    int m = month;
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    const int64_t days_since_epoch = (int64_t)era * 146097 + (int64_t)doe - 719468;

    return (days_since_epoch * 86400) + (hour * 3600) + (minute * 60) + second;
}

/**
 * @brief Demo: Full character set.
 */
static void demo_charset(opel_mid_handle_t display, opel_mid_type_t type)
{
    int width = get_display_width(type);
    char text[16];

    printf("\n=== CHARACTER SET DEMONSTRATION ===\n");
    printf("Displaying all printable ASCII characters (0x20–0x7E, %u chars).\n", CHAR_MAP_LEN - (0x7E - 0x20 + 1));
    printf("Display width: %d characters.\n\n", width);

    /* Show characters in groups of width size */
    for (int i = 0x20; i <= 0x7E; i += width)
    {
        int group_size = (0x7E - i + 1 < width) ? (0x7E - i + 1) : width;

        /* Build the display string */
        memset(text, ' ', width);
        for (int j = 0; j < group_size; j++)
        {
            text[j] = (char)(i + j);
        }
        text[width] = '\0';

        printf("Characters 0x%02X–0x%02X: %s\n", i, i + group_size - 1, text);

        /* Send to display */
        esp_err_t ret = opel_mid_send(display, text, NULL);
        if (ret != ESP_OK)
        {
            printf("ERROR: opel_mid_send() failed: 0x%X\n", ret);
            return;
        }

        wait_between_steps();
    }

    printf("\nCharacter set demonstration complete.\n");
}

/**
 * @brief Display BBC Radio 4 info; as capturered by logic probe
 */
static void bbcr4(opel_mid_handle_t display, opel_mid_type_t type)
{
    // int width = get_display_width(type);
    char text[16];
    opel_mid_symbols_t symbols = {0, 0, 0};

    symbols.radio = 0x2A; // 0b00101100 = > 0b01011000 with parity(0x58)

    text[0] = 0x0A; // 0b0001010 => 0b00010101 with parity (0x15).
    text[1] = 0x04; // 0b0000100 => 0b00001000 with parity (0x08).
    text[2] = 'B';
    text[3] = 'B';
    text[4] = 'C';
    text[5] = ' ';
    text[6] = 'R';
    text[7] = '4';
    text[8] = ' ';
    text[9] = ' ';
    printf("\nBBCR4 demonstration complete.\n");

    esp_err_t ret = opel_mid_send(display, text, &symbols);
    if (ret != ESP_OK)
    {
        printf("  ERROR: BBCR4 failed (0x%X)\n", ret);
    }
    else
    {
        printf("  BBCR4 sent successfully. Note what appears on display.\n");
    }
}

/**
 * @brief mode 10, clock/date override mode
 */
static void mode10(opel_mid_handle_t display, opel_mid_type_t type)
{
    // int width = get_display_width(type);
    uint8_t text[16];

    text[0] = 0x08; // 0b0001000 => 0b00100000 with parity (0x20).
    text[1] = 'M';
    text[2] = 'O';
    text[3] = 'D';
    text[4] = 'E';
    text[5] = ' ';
    text[6] = '1';
    text[7] = '0';
    text[9] = ' ';
    text[10] = ' ';
    printf("\nMode 10 demonstration complete.\n");

    esp_err_t ret = opel_mid10_send(display, text);
    if (ret != ESP_OK)
    {
        printf("  ERROR: Mode10 failed (0x%X)\n", ret);
    }
    else
    {
        printf("  Mode10 sent successfully. Note what appears on display.\n");
    }
}

/**
 * @brief mode 11, trip counter mode
 */
static void mode11(opel_mid_handle_t display, opel_mid_type_t type)
{
    // int width = get_display_width(type);
    uint8_t text[16];

    text[0] = 0x81; // 0b0001001 => 0b00100011 with parity (0x23).
    text[1] = 'M';
    text[2] = 'O';
    text[3] = 'D';
    text[4] = 'E';
    text[5] = ' ';
    text[6] = '1';
    text[7] = '1';
    text[9] = ' ';
    text[10] = ' ';
    printf("\nMode 11 demonstration complete.\n");

    esp_err_t ret = opel_mid10_send(display, text);
    if (ret != ESP_OK)
    {
        printf("  ERROR: Mode11 failed (0x%X)\n", ret);
    }
    else
    {
        printf("  Mode11 sent successfully. Note what appears on display.\n");
    }
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
    for (int i = 0x00; i <= 0x1F; i += 2)
    {
        char c1 = (char)i;
        char c2 = (char)(i + 1);

        memset(text, ' ', width);
        text[0] = c1;
        if (width > 1)
            text[1] = c2;
        text[width] = '\0';

        printf("0x%02X / 0x%02X: Sending...\n", i, i + 1);

        /* Send each as a single character for clarity */
        memset(text, ' ', width);
        text[0] = c1;
        text[width] = '\0';

        esp_err_t ret = opel_mid_send(display, text, NULL);
        if (ret != ESP_OK)
        {
            printf("  ERROR: 0x%02X failed (0x%X)\n", i, ret);
        }
        else
        {
            printf("  0x%02X sent successfully. Note what appears on display.\n", i);
        }

        wait_between_steps();
    }

    /* Test DEL (0x7F) */
    printf("\nDEL (0x7F):\n");
    for (int i = 0x7F; i <= 0x7F; i++)
    {
        char c1 = (char)i;

        printf("0x%02X: Sending...\n", i);

        memset(text, ' ', width);
        text[0] = c1;
        text[width] = '\0';

        esp_err_t ret = opel_mid_send(display, text, NULL);
        if (ret != ESP_OK)
        {
            printf("  ERROR: 0x%02X failed (0x%X)\n", i, ret);
        }
        else
        {
            printf("  0x%02X sent successfully. Note what appears on display.\n", i);
        }

        wait_between_steps();
    }

    printf("\nNon-printable 7-bit code testing complete.\n");
}

/**
 * @brief Demo: Symbol toggling.
 */
static void demo_symbols(opel_mid_handle_t display, opel_mid_type_t type)
{
    // int width = get_display_width(type);
    char text[16];

    printf("\n=== SYMBOL DEMONSTRATION ===\n");
    printf("Toggling all available symbols.\n");
    printf("Radio symbols: COMMA, RDS, TP, STEREO, AS, TP_BRACKET\n");
    printf("Tape symbols: CD_IN, DOLBY_C, DOLBY_B, CR, CPS\n");
    if (type == OPEL_MID_TYPE_TID_10)
    {
        printf("CD symbols: TRACK, RDM, PGM, DISC\n");
    }
    printf("\n");

    // format_text(text, sizeof(text), "SYMBOLS", width);

    /* Radio symbols */
    printf("Radio Status Symbols:\n");
    opel_mid_symbols_t symbols = {0, 0, 0};

    for (int i = 0; i < 22; i++)
    {
        symbols.radio = 0;
        symbols.tape = 0;
        symbols.cd = 0;
        if (i == 0)
        {
            sprintf(text, " NONE  ");
        }
        else if (i > 14)
        {
            sprintf(text, " CD %d    ", (i - 15));
            symbols.cd = (1 << (i - 15));
        }
        else if (i > 7)
        {
            sprintf(text, " TAPE %d  ", (i - 8));
            symbols.tape = (1 << (i - 8));
        }
        else
        {
            sprintf(text, " RADIO %d  ", (i - 1));
            symbols.radio = (1 << (i - 1));
        }
        esp_err_t ret = opel_mid_send(display, text, &symbols);
        if (ret != ESP_OK)
        {
            printf("ERROR (0x%X)\n", ret);
        }
        else
        {
            printf("ON\n");
        }

        wait_between_steps();
    }

    /* Tape symbols */
    printf("Tape Status Symbols:\n");
    if (DISPLAY_TYPE == OPEL_MID_TYPE_TID_8 || DISPLAY_TYPE == OPEL_MID_TYPE_TID_10)
    {
        uint8_t tape_flags[] = {
            OPEL_MID_SYM_CD_IN,
            OPEL_MID_SYM_DOLBY_C,
            OPEL_MID_SYM_DOLBY_B,
            OPEL_MID_SYM_CR,
            OPEL_MID_SYM_CPS,
        };
        const char *tape_names[] = {"CD_IN", "DOLBY_C", "DOLBY_B", "CR", "CPS"};

        for (size_t i = 0; i < sizeof(tape_flags) / sizeof(tape_flags[0]); i++)
        {
            symbols.radio = 0;
            symbols.tape = tape_flags[i];
            symbols.cd = 0;

            printf("  %s: ", tape_names[i]);
            fflush(stdout);

            esp_err_t ret = opel_mid_send(display, text, &symbols);
            if (ret != ESP_OK)
            {
                printf("ERROR (0x%X)\n", ret);
            }
            else
            {
                printf("ON\n");
            }

            wait_between_steps();
        }
    }

    /* CD symbols (10-digit only) */
    if (type == OPEL_MID_TYPE_TID_10)
    {
        printf("CD Status Symbols (10-digit TID/MID only):\n");
        uint8_t cd_flags[] = {
            OPEL_MID_SYM_TRACK,
            OPEL_MID_SYM_RDM,
            OPEL_MID_SYM_PGM,
            OPEL_MID_SYM_DISC,
        };
        const char *cd_names[] = {"TRACK", "RDM", "PGM", "DISC"};

        for (size_t i = 0; i < sizeof(cd_flags) / sizeof(cd_flags[0]); i++)
        {
            symbols.radio = 0;
            symbols.tape = 0;
            symbols.cd = cd_flags[i];

            printf("  %s: ", cd_names[i]);
            fflush(stdout);

            esp_err_t ret = opel_mid_send(display, text, &symbols);
            if (ret != ESP_OK)
            {
                printf("ERROR (0x%X)\n", ret);
            }
            else
            {
                printf("ON\n");
            }

            wait_between_steps();
        }
    }

    /* All symbols on */
    printf("All symbols ON: ");
    fflush(stdout);
    symbols.radio = 0xFF & 0xFE; /* All radio flags except parity bit */
    symbols.tape = 0xFF & 0xFE;
    symbols.cd = 0xFF & 0xFE;

    esp_err_t ret = opel_mid_send(display, text, &symbols);
    if (ret != ESP_OK)
    {
        printf("ERROR (0x%X)\n", ret);
    }
    else
    {
        printf("ON\n");
    }

    wait_between_steps();

    /* All symbols off */
    printf("All symbols OFF: ");
    fflush(stdout);
    symbols.radio = 0;
    symbols.tape = 0;
    symbols.cd = 0;

    ret = opel_mid_send(display, text, &symbols);
    if (ret != ESP_OK)
    {
        printf("ERROR (0x%X)\n", ret);
    }
    else
    {
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
    wait_between_steps();

    /* Long text (truncation) */
    printf("Text longer than display width (should truncate): ");
    fflush(stdout);
    const char *long_text = "THIS_TEXT_IS_WAY_TOO_LONG";
    ret = opel_mid_send(display, long_text, NULL);
    printf("%s (displayed: %.*s)\n", ret == ESP_OK ? "OK" : "ERROR", width, long_text);
    wait_between_steps();

    /* Short text (padding) */
    printf("Short text (should be space-padded): ");
    fflush(stdout);
    ret = opel_mid_send(display, "HI", NULL);
    printf("%s\n", ret == ESP_OK ? "OK" : "ERROR");
    wait_between_steps();

    /* Non-printable characters (become spaces) */
    printf("Non-printable characters (0x01, 0x7F, 0xFF treated as non-printable and become spaces): ");
    fflush(stdout);
    text[0] = '\x01';
    text[1] = 'T';
    text[2] = '\x7F';
    text[3] = 'S';
    text[4] = '\xFF';
    for (int i = 5; i < width; i++)
        text[i] = ' ';
    text[width] = '\0';
    ret = opel_mid_send(display, text, NULL);
    printf("%s\n", ret == ESP_OK ? "OK" : "ERROR");
    wait_between_steps();

    printf("Edge case testing complete.\n");
}

/**
 * @brief Demo: Hardware time sync from ESP32 UTC clock + stored local offset.
 *
 * Encodes the current UTC time and local offset into the exact RDS byte stream
 * format that would be received from an actual RDS radio decoder, then sends it
 * to the display. This demonstrates how a real application would receive CT
 * (Clock Time) groups from an RDS tuner and forward them directly to the display.
 */
static void demo_time_sync(opel_mid_handle_t display)
{
    printf("\n=== HARDWARE TIME SYNC (RDS CLOCK TIME) ===\n");

    if (!s_time_initialized || !s_utc_offset_is_set)
    {
        printf("No time set ready to send. Run: set-time <RFC3339> first.\n");
        return;
    }

    if ((s_utc_offset_seconds % 1800) != 0)
    {
        printf("Stored UTC offset (%ld s) is not representable in 30-minute RDS steps.\n",
               (long)s_utc_offset_seconds);
        return;
    }

    time_t now_utc = time(NULL);
    if (now_utc <= 0)
    {
        printf("ESP32 UTC clock is not initialized. Run set-time first.\n");
        return;
    }

    struct tm utc_tm;
    gmtime_r(&now_utc, &utc_tm);

    /* Calculate Modified Julian Date from Unix epoch timestamp */
    uint32_t mjd = (uint32_t)((int64_t)now_utc / 86400LL + 40587LL);

    /* Encode UTC time and offset into exact RDS byte stream format.
     *
     * RDS Clock Time (CT) group byte layout:
     *   Byte 0: [sign(1) | offset_value(4) | mjd_bits_16_14(3)]
     *           Offset: 0-31 half-hour steps (-12:00 to +14:00)
     *           Sign bit: 0=east/positive, 1=west/negative
     *   Byte 1: [minute_5_0(6) | mjd_bits_13_12(2)]
     *   Byte 2: [hour_4_0(5) | mjd_bits_11_9(3)]
     *   Byte 3: [mjd_bits_8_1(8)]
     *
     * This format allows real RDS receivers to pass their CT bytes directly.
     */
    uint8_t rds_time_block[4];

    /* Encode offset with sign bit and value bits */
    int offset_half_hours = (int)(s_utc_offset_seconds / 1800);
    uint8_t offset_abs = (offset_half_hours < 0) ? -offset_half_hours : offset_half_hours;
    uint8_t offset_byte = (offset_half_hours < 0) ? (uint8_t)(offset_abs | 0x20u) : offset_abs;

    /* Extract MJD bits for packing into bytes 0-3 */
    uint8_t mjd_bit_16_14 = (uint8_t)((mjd >> 14) & 0x07); /* Bits 16-14 of 17-bit MJD */
    uint8_t mjd_bit_13_12 = (uint8_t)((mjd >> 12) & 0x03); /* Bits 13-12 */
    uint8_t mjd_bit_11_9 = (uint8_t)((mjd >> 9) & 0x07);   /* Bits 11-9 */
    uint8_t mjd_bit_8_1 = (uint8_t)((mjd >> 1) & 0xFF);    /* Bits 8-1 */

    /* Pack into 4-byte RDS format */
    rds_time_block[0] = (uint8_t)((offset_byte & 0x1F) | (mjd_bit_16_14 << 5));
    rds_time_block[1] = (uint8_t)(((uint8_t)utc_tm.tm_min & 0x3F) | (mjd_bit_13_12 << 6));
    rds_time_block[2] = (uint8_t)(((uint8_t)utc_tm.tm_hour & 0x1F) | (mjd_bit_11_9 << 5));
    rds_time_block[3] = mjd_bit_8_1;

    /* Send the exact RDS byte stream to the display */
    esp_err_t ret = opel_mid_set_time(display, rds_time_block);

    if (ret != ESP_OK)
    {
        printf("Time sync failed: 0x%X\n", ret);
        return;
    }

    printf("Time sync sent (RDS bytes): [0x%02X 0x%02X 0x%02X 0x%02X]\n",
           rds_time_block[0], rds_time_block[1], rds_time_block[2], rds_time_block[3]);
    printf("  MJD=%lu UTC=%04d-%02d-%02dT%02d:%02d:%02dZ offset=%+03d:%02d\n",
           (unsigned long)mjd,
           utc_tm.tm_year + 1900,
           utc_tm.tm_mon + 1,
           utc_tm.tm_mday,
           utc_tm.tm_hour,
           utc_tm.tm_min,
           utc_tm.tm_sec,
           (int)(s_utc_offset_seconds / 3600),
           (int)((s_utc_offset_seconds < 0 ? -s_utc_offset_seconds : s_utc_offset_seconds) % 3600 / 60));
    wait_between_steps();
}

static int cmd_demo_charset(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    demo_charset(s_display, s_display_type);
    return 0;
}

static int cmd_bbcr4(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bbcr4(s_display, s_display_type);
    return 0;
}

static int cmd_mode10(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    mode10(s_display, s_display_type);
    return 0;
}

static int cmd_mode11(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    mode11(s_display, s_display_type);
    return 0;
}

static int cmd_demo_extended(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    demo_extended_chars(s_display, s_display_type);
    return 0;
}

static int cmd_demo_symbols(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    demo_symbols(s_display, s_display_type);
    return 0;
}

static int cmd_demo_edge(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    demo_edge_cases(s_display, s_display_type);
    return 0;
}

static int cmd_demo_all(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    demo_charset(s_display, s_display_type);
    demo_extended_chars(s_display, s_display_type);
    demo_symbols(s_display, s_display_type);
    demo_edge_cases(s_display, s_display_type);
    printf("\n=== ALL DEMONSTRATIONS COMPLETE ===\n");
    return 0;
}

static int cmd_time_sync(int argc, char **argv)
{
    (void)argv;

    if (argc != 1)
    {
        printf("Usage: time-sync\n");
        printf("Note: run set-time <RFC3339> first.\n");
        return 1;
    }

    demo_time_sync(s_display);
    return 0;
}

static int cmd_set_time(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Usage: set-time <RFC3339>\n");
        printf("Example: set-time 2026-08-06T13:51:00+01:00\n");
        return 1;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int offset_seconds = 0;

    if (!parse_rfc3339_timestamp(argv[1], &year, &month, &day, &hour, &minute, &second, &offset_seconds))
    {
        printf("Invalid RFC3339 timestamp. Expected YYYY-MM-DDTHH:MM:SSZ or YYYY-MM-DDTHH:MM:SS+/-HH:MM\n");
        return 1;
    }

    int64_t local_epoch = epoch_seconds_from_ymdhms(year, month, day, hour, minute, second);
    int64_t utc_epoch = local_epoch - (int64_t)offset_seconds;

    struct timeval tv = {
        .tv_sec = (time_t)utc_epoch,
        .tv_usec = 0,
    };

    if (settimeofday(&tv, NULL) != 0)
    {
        printf("Failed to set system time.\n");
        return 1;
    }

    s_utc_offset_seconds = (int32_t)offset_seconds;
    s_utc_offset_is_set = 1;
    s_time_initialized = 1;

    struct tm utc_tm;
    gmtime_r(&tv.tv_sec, &utc_tm);

    printf("System UTC time set to %04d-%02d-%02dT%02d:%02d:%02dZ\n",
           utc_tm.tm_year + 1900,
           utc_tm.tm_mon + 1,
           utc_tm.tm_mday,
           utc_tm.tm_hour,
           utc_tm.tm_min,
           utc_tm.tm_sec);
    printf("Stored UTC offset: %+03d:%02d (%ld seconds)\n",
           offset_seconds / 3600,
           (offset_seconds < 0 ? -offset_seconds : offset_seconds) % 3600 / 60,
           (long)s_utc_offset_seconds);

    return 0;
}

static int cmd_power_on(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!s_display)
    {
        printf("Display is not initialized.\n");
        return 1;
    }

    printf("Running display power-on test sequence...\n");
    esp_err_t ret = opel_mid_power_on(s_display);
    if (ret != ESP_OK)
    {
        printf("power-on failed: 0x%X\n", ret);
        return 1;
    }

    printf("power-on completed successfully.\n");
    return 0;
}

static int cmd_list(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Display type: %s (%d characters)\n",
           (s_display_type == OPEL_MID_TYPE_TID_8) ? "TID-8" : "TID-10/MID",
           get_display_width(s_display_type));
    printf("Commands:\n");
    printf("  demo-charset   - show printable ASCII\n");
    printf("  demo-extended  - test non-printable 7-bit codes\n");
    printf("  demo-symbols   - toggle symbol groups\n");
    printf("  demo-edge      - run edge case tests\n");
    printf("  demo-all       - run all demos\n");
    printf("  bbcr4          - send \"BBC R4\" command\n");
    printf("  mode10         - send \"Mode 10\" command\n");
    printf("  mode11         - send \"Mode 11\" command\n");
    printf("  power-on       - run display power-on test sequence\n");
    printf("  time-sync      - send RDS MJD time from ESP32 UTC clock\n");
    printf("  set-time <ts>  - set ESP32 UTC clock from RFC3339\n");
    if (s_utc_offset_is_set)
    {
        printf("  stored-offset  - %+03ld:%02ld (%ld seconds)\n",
               (long)(s_utc_offset_seconds / 3600),
               (long)((s_utc_offset_seconds < 0 ? -s_utc_offset_seconds : s_utc_offset_seconds) % 3600 / 60),
               (long)s_utc_offset_seconds);
    }
    printf("  help           - list registered commands\n");
    return 0;
}

static void register_console_commands(void)
{
    const esp_console_cmd_t list_cmd = {
        .command = "list",
        .help = "Show display information and available demo commands",
        .hint = NULL,
        .func = &cmd_list,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&list_cmd));

    const esp_console_cmd_t charset_cmd = {
        .command = "demo-charset",
        .help = "Display all printable ASCII characters",
        .hint = NULL,
        .func = &cmd_demo_charset,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&charset_cmd));

    const esp_console_cmd_t bbcr4_cmd = {
        .command = "bbcr4",
        .help = "Display BBC radio 4 info",
        .hint = NULL,
        .func = &cmd_bbcr4,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&bbcr4_cmd));

    const esp_console_cmd_t mode10_cmd = {
        .command = "mode10",
        .help = "Send \"Mode 10\" command",
        .hint = NULL,
        .func = &cmd_mode10,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mode10_cmd));

    const esp_console_cmd_t mode11_cmd = {
        .command = "mode11",
        .help = "Send \"Mode 11\" command",
        .hint = NULL,
        .func = &cmd_mode11,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mode11_cmd));

    const esp_console_cmd_t extended_cmd = {
        .command = "demo-extended",
        .help = "Test non-printable 7-bit payload codes",
        .hint = NULL,
        .func = &cmd_demo_extended,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&extended_cmd));

    const esp_console_cmd_t symbols_cmd = {
        .command = "demo-symbols",
        .help = "Toggle all supported radio/tape/cd symbols",
        .hint = NULL,
        .func = &cmd_demo_symbols,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&symbols_cmd));

    const esp_console_cmd_t edge_cmd = {
        .command = "demo-edge",
        .help = "Run string handling edge case tests",
        .hint = NULL,
        .func = &cmd_demo_edge,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&edge_cmd));

    const esp_console_cmd_t all_cmd = {
        .command = "demo-all",
        .help = "Run all demonstration sequences",
        .hint = NULL,
        .func = &cmd_demo_all,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&all_cmd));

    const esp_console_cmd_t power_on_cmd = {
        .command = "power-on",
        .help = "Run display power-on test sequence",
        .hint = NULL,
        .func = &cmd_power_on,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&power_on_cmd));

    const esp_console_cmd_t time_sync_cmd = {
        .command = "time-sync",
        .help = "Sync display time from UTC timestamp",
        .hint = "<YYYYMMDDTHHmmss>",
        .func = &cmd_time_sync,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&time_sync_cmd));

    const esp_console_cmd_t set_time_cmd = {
        .command = "set-time",
        .help = "Set ESP32 UTC clock from RFC3339 timestamp",
        .hint = "<YYYY-MM-DDTHH:MM:SS+/-HH:MM|Z>",
        .func = &cmd_set_time,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_time_cmd));
}

static void start_console_repl(void)
{
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "opelxid> ";

    esp_console_register_help_command();
    register_console_commands();

    esp_console_repl_t *repl = NULL;

#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
#else
#error "No supported ESP console transport enabled"
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "Starting OpelXID Demonstration");

    esp_err_t ret;

    /* Initialize the display */
    opel_mid_config_t config = {
        .pin_sda = (gpio_num_t)PIN_SDA,
        .pin_scl = (gpio_num_t)PIN_SCL,
        .pin_mrq = (gpio_num_t)PIN_MRQ,
        .type = DISPLAY_TYPE,
    };

    opel_mid_handle_t display = NULL;
    ret = opel_mid_init(&config, &display);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "opel_mid_init() failed: 0x%X", ret);
        return;
    }

    s_display = display;
    s_display_type = DISPLAY_TYPE;

    printf("\nOpelXID MID/TID Demonstration Console\n");
    printf("======================================\n");
    printf("Display initialized.\n");
    printf("Run 'power-on' when you want to execute the display power-on sequence.\n");
    printf("Type 'help' to list commands, then run 'demo-info' for examples.\n\n");

    start_console_repl();
}
