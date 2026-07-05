#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "app_events.h"
#include "audio_state.h"
#include "config.h"
#include "protocol_decoder.h"
#include "time_sync.h"

static const char *TAG = "protocol_decoder";

static char *trim_left(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s)) {
        ++s;
    }
    return s;
}

static void trim_right(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        --len;
    }
}

static char *trim(char *s)
{
    char *left = trim_left(s);
    if (left != s) {
        memmove(s, left, strlen(left) + 1);
    }
    trim_right(s);
    return s;
}

static void sanitize_text(char *dst, const uint8_t *src, size_t src_len)
{
    size_t copy_len = src_len;
    if (copy_len >= OPX_MAX_TEXT_PAYLOAD) {
        copy_len = OPX_MAX_TEXT_PAYLOAD - 1;
    }

    for (size_t i = 0; i < copy_len; ++i) {
        uint8_t c = src[i];
        if (c >= 32 && c <= 126) {
            dst[i] = (char)c;
        } else if (c == '\t') {
            dst[i] = ' ';
        } else {
            dst[i] = '?';
        }
    }

    dst[copy_len] = '\0';
}

static void copy_bounded_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || src == NULL || dst_size == 0) {
        return;
    }

    size_t n = strnlen(src, dst_size - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void handle_audio_metadata(const uint8_t *frame, size_t len)
{
    if (len <= 8) {
        return;
    }

    char text[OPX_MAX_TEXT_PAYLOAD];
    sanitize_text(text, &frame[8], len - 8);

    char *first = text;
    char *second = strchr(first, ':');
    char *third = NULL;

    if (second != NULL) {
        *second = '\0';
        ++second;
        third = strchr(second, ':');
        if (third != NULL) {
            *third = '\0';
            ++third;
        }
    }

    trim(first);
    if (second != NULL) {
        trim(second);
    }
    if (third != NULL) {
        trim(third);
    }

    audio_metadata_t md = {
        .playback_state = AUDIO_PLAYBACK_UNKNOWN,
        .updated_ms = esp_timer_get_time() / 1000,
    };

    const char *track = "";
    const char *artist = "";

    if (third != NULL && strcasecmp(first, "Playing") == 0) {
        md.playback_state = AUDIO_PLAYBACK_PLAYING;
        track = second;
        artist = third;
    } else if (third != NULL && strcasecmp(first, "Paused") == 0) {
        md.playback_state = AUDIO_PLAYBACK_PAUSED;
        track = second;
        artist = third;
    } else {
        track = first;
        artist = (second != NULL) ? second : "";
    }

    copy_bounded_string(md.track, sizeof(md.track), track);
    copy_bounded_string(md.artist, sizeof(md.artist), artist);

    audio_state_set(&md);

    app_event_t event = {
        .type = APP_EVENT_AUDIO_METADATA,
    };
    event.data.audio = md;
    app_events_publish(&event);
}

static void handle_time_sync(const uint8_t *frame, size_t len)
{
    time_t epoch = 0;
    bool ok = time_sync_apply_from_frame(frame, len, &epoch);

    app_event_t event = {
        .type = APP_EVENT_TIME_SYNC,
        .data.time = {
            .success = ok,
            .epoch_seconds = epoch,
        },
    };
    app_events_publish(&event);

    if (!ok) {
        ESP_LOGW(TAG, "Rejected invalid time sync frame");
    }
}

void protocol_decoder_process(const uint8_t *frame, size_t len)
{
    if (frame == NULL || len < 5) {
        return;
    }

    uint8_t opcode = frame[4];
    switch (opcode) {
    case 0x72:
        handle_audio_metadata(frame, len);
        break;
    case 0x93:
        handle_time_sync(frame, len);
        break;
    default:
        break;
    }
}
