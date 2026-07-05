#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "frame_reassembly.h"

static void frame_reassembly_reset(frame_reassembly_t *st)
{
    st->used = 0;
    st->expected = 0;
}

void frame_reassembly_init(frame_reassembly_t *st)
{
    if (st == NULL) {
        return;
    }

    frame_reassembly_reset(st);
}

void frame_reassembly_push(frame_reassembly_t *st,
                           const uint8_t *data,
                           size_t len,
                           frame_complete_cb_t on_frame,
                           void *ctx)
{
    if (st == NULL || data == NULL || len == 0) {
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];

        if (st->used == 0) {
            if (b != OPX_FRAME_MARKER) {
                continue;
            }

            st->buffer[st->used++] = b;
            continue;
        }

        if (st->used >= sizeof(st->buffer)) {
            frame_reassembly_reset(st);
            continue;
        }

        st->buffer[st->used++] = b;

        if (st->used == 3) {
            st->expected = (((size_t)st->buffer[1] << 8U) | st->buffer[2]) + 3U;
            if (st->expected < OPX_MIN_FRAME_SIZE || st->expected > OPX_MAX_FRAME_SIZE) {
                frame_reassembly_reset(st);
                continue;
            }
        }

        if (st->expected > 0 && st->used == st->expected) {
            if (on_frame != NULL) {
                on_frame(st->buffer, st->expected, ctx);
            }
            frame_reassembly_reset(st);
        }
    }
}
