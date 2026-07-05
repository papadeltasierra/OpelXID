#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

bool time_sync_apply_from_frame(const uint8_t *frame, size_t frame_len, time_t *epoch_out);
