#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AUDIO_PLAYBACK_UNKNOWN = 0,
    AUDIO_PLAYBACK_PLAYING,
    AUDIO_PLAYBACK_PAUSED,
} audio_playback_state_t;

typedef struct {
    audio_playback_state_t playback_state;
    char track[96];
    char artist[96];
    int64_t updated_ms;
} audio_metadata_t;

void audio_state_init(void);
void audio_state_set(const audio_metadata_t *metadata);
void audio_state_get(audio_metadata_t *metadata);
