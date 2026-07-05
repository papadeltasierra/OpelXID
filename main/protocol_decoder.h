#pragma once

#include <stddef.h>
#include <stdint.h>

void protocol_decoder_process(const uint8_t *frame, size_t len);
