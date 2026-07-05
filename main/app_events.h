#pragma once

#include <stdbool.h>
#include <time.h>

#include "audio_state.h"

typedef enum {
    APP_EVENT_AUDIO_METADATA = 0,
    APP_EVENT_TIME_SYNC,
} app_event_type_t;

typedef struct {
    bool success;
    time_t epoch_seconds;
} app_time_sync_event_t;

typedef struct {
    app_event_type_t type;
    union {
        audio_metadata_t audio;
        app_time_sync_event_t time;
    } data;
} app_event_t;

typedef void (*app_event_cb_t)(const app_event_t *event, void *ctx);

void app_events_init(void);
void app_events_register_callback(app_event_cb_t cb, void *ctx);
void app_events_publish(const app_event_t *event);
