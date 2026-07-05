#include "app_events.h"

static app_event_cb_t s_cb;
static void *s_cb_ctx;

void app_events_init(void)
{
    s_cb = NULL;
    s_cb_ctx = NULL;
}

void app_events_register_callback(app_event_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
}

void app_events_publish(const app_event_t *event)
{
    if (s_cb != NULL && event != NULL) {
        s_cb(event, s_cb_ctx);
    }
}
