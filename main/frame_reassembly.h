#pragma once

#include <stddef.h>
#include <stdint.h>

typedef void (*frame_complete_cb_t)(const uint8_t *frame, size_t len, void *ctx);

typedef struct {
    uint8_t buffer[512];
    size_t used;
    size_t expected;
} frame_reassembly_t;

void frame_reassembly_init(frame_reassembly_t *st);
void frame_reassembly_push(frame_reassembly_t *st,
                           const uint8_t *data,
                           size_t len,
                           frame_complete_cb_t on_frame,
                           void *ctx);
