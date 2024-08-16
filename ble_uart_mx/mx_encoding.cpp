#include "mx_encoding.h"

// Check if there are bytes with values 0, 1, 2 in the buffer
bool is_data_binary(uint8_t const * data, size_t len)
{
  return false; // FIXME
}

// Encode binary data to base64 with padding, returns encoded data length
size_t encode(uint8_t const * bin_data, size_t len, uint8_t asc_data[MAX_ENCODED_DATA_LEN])
{
  return 0; // FIXME
}

// Decode base64 encoded data (with padding or not), returns decoded data length or 0 in case of decoding error
size_t decode(const char * asc_data, size_t len, uint8_t bin_data[MAX_CHUNK])
{
  return 0; // FIXME
}
