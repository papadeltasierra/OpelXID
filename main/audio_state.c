#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "audio_state.h"

static SemaphoreHandle_t s_lock;
static audio_metadata_t s_state;

void audio_state_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }

    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        memset(&s_state, 0, sizeof(s_state));
        s_state.playback_state = AUDIO_PLAYBACK_UNKNOWN;
        xSemaphoreGive(s_lock);
    }
}

void audio_state_set(const audio_metadata_t *metadata)
{
    if (s_lock == NULL || metadata == NULL) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state = *metadata;
    xSemaphoreGive(s_lock);
}

void audio_state_get(audio_metadata_t *metadata)
{
    if (s_lock == NULL || metadata == NULL) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *metadata = s_state;
    xSemaphoreGive(s_lock);
}
