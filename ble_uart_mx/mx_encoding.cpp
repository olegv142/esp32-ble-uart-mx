#include "mx_encoding.h"

// Encode binary data to base64 with padding, returns encoded data length
size_t encode(uint8_t const * bin_data, size_t len, char asc_data[MAX_ENCODED_CHUNK_LEN])
{
  return 0; // FIXME
}

// Decode base64 encoded data (with padding or not), returns decoded data length or 0 in case of decoding error
size_t decode(const char * asc_data, size_t len, uint8_t bin_data[MAX_FRAME])
{
  return 0; // FIXME
}
