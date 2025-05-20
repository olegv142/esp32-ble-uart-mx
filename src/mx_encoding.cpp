#include "mx_encoding.h"

#define LETTERS ('Z'-'A'+1)
#define INVALID 0xFF

// Transforms 6 bit code to symbol
static inline char b64symbol(uint8_t code)
{
  if (code < LETTERS)
    return 'A' + code;
  if (code < 2*LETTERS)
    return 'a' + code - LETTERS;
  if (code < 2*LETTERS + 10)
    return '0' + code - 2*LETTERS;
  if (code == 2*LETTERS + 10)
    return '+';
  if (code == 2*LETTERS + 11)
    return '/';
  return 0; // Invalid code
}

// Transforms symbol to 6 bit code
static inline uint8_t b64code(char symbol) {
  if (symbol >= 'A' && symbol <= 'Z')
    return symbol - 'A';
  if (symbol >= 'a' && symbol <= 'z')
    return symbol - 'a' + LETTERS;
  if (symbol >= '0' && symbol <= '9')
    return symbol - '0' + 2*LETTERS;
  if (symbol == '+')
    return 2*LETTERS + 10;
  if (symbol == '/')
    return 2*LETTERS + 11;
  if (symbol == '=')
    return 0;
  return INVALID; // Invalid symbol
}

union b64buffer {
  uint32_t word;
  uint8_t  byte[4];

  void set_byte(uint8_t b, int idx) {
    byte[2-idx] = b;
  }

  uint8_t get_byte(int idx) {
    return byte[2-idx];
  }

  uint8_t get_code(int idx) {
    return (word >> (6*(3-idx))) & ((1<<6)-1);
  }

  void set_code(uint8_t b, int idx) {
    word |= (b << (6*(3-idx)));
  }

  void reset() {
    word = 0;
  }
};

// Encode binary data to base64 with padding, returns encoded data length
size_t encode(uint8_t const * bin_data, size_t len, char asc_data[MAX_ENCODED_CHUNK_LEN])
{
  union b64buffer buf;
  size_t out_len = 0;
  while (len) {
    if (len >= 3) {
      buf.set_byte(bin_data[0], 0);
      buf.set_byte(bin_data[1], 1);
      buf.set_byte(bin_data[2], 2);
      asc_data[0] = b64symbol(buf.get_code(0));
      asc_data[1] = b64symbol(buf.get_code(1));
      asc_data[2] = b64symbol(buf.get_code(2));
      asc_data[3] = b64symbol(buf.get_code(3));
      out_len  += 4;
      asc_data += 4;
      bin_data += 3;
      len -= 3;
    } else if (len == 2) {
      buf.set_byte(bin_data[0], 0);
      buf.set_byte(bin_data[1], 1);
      buf.set_byte(0, 2);
      asc_data[0] = b64symbol(buf.get_code(0));
      asc_data[1] = b64symbol(buf.get_code(1));
      asc_data[2] = b64symbol(buf.get_code(2));
      asc_data[3] = '=';
      out_len  += 4;
      len = 0;
    } else if (len == 1) {
      buf.set_byte(bin_data[0], 0);
      buf.set_byte(0, 1);
      buf.set_byte(0, 2);
      asc_data[0] = b64symbol(buf.get_code(0));
      asc_data[1] = b64symbol(buf.get_code(1));
      asc_data[2] = '=';
      asc_data[3] = '=';
      out_len  += 4;
      len = 0;
    }
  }
  return out_len;
}

// Decode base64 encoded data (with padding or not), returns decoded data length or 0 in case of decoding error
size_t decode(const char * asc_data, size_t len, uint8_t bin_data[MAX_FRAME])
{
  union b64buffer buf;
  size_t out_len = 0;

  while (len > 0) {
    if (len < 4)
      return 0;
    buf.reset();
    uint8_t padding = 0, i;
    for (i = 0; i < 4; ++i) {
      char const c = asc_data[i];
      uint8_t const code = b64code(c);
      if (code == INVALID)
        return 0;
      buf.set_code(code, i);
      if (c == '=')
        ++padding;
    }
    if (padding > 2)
      return 0;
    uint8_t const bytes = 3 - padding;
    for (i = 0; i < bytes; ++i)
      bin_data[i] = buf.get_byte(i);
    out_len += bytes;
    bin_data += bytes;
    asc_data += 4;
    len -= 4;
  }
  return out_len;
}
