#pragma once

#include "mx_config.h"

#include <stddef.h>
#include <stdint.h>

// Starts the base64 encoded data that follows
#define ENCODED_DATA_START_TAG '\2'

// Get the length of base64 encoded data (with padding)
#define MAX_BASE64_ENCODED_LEN(raw_len) ((((raw_len)+2)/3)*4)

#define MAX_ENCODED_FRAME_LEN MAX_BASE64_ENCODED_LEN(MAX_FRAME)
#define MAX_ENCODED_CHUNK_LEN MAX_BASE64_ENCODED_LEN(MAX_CHUNK)

static inline bool is_data_binary(uint8_t const * data, size_t len) {
  for (size_t i = 0; i < len; ++i)
    switch (data[i]) {
    case UART_END:
#ifdef UART_BEGIN
    case UART_BEGIN:
#endif
    case ENCODED_DATA_START_TAG:
      return true;
    }
  return false;
}

// Encode binary data to base64 with padding, returns encoded data length
size_t encode(uint8_t const * bin_data, size_t len, char asc_data[MAX_ENCODED_CHUNK_LEN]);

// Decode base64 encoded data (with padding or not), returns decoded data length or 0 in case of decoding error
size_t decode(const char * asc_data, size_t len, uint8_t bin_data[MAX_FRAME]);
