#pragma once

#include "mx_config.h"
#include "fnv_hash.h"

#define CHKSUM_INI FNV32_OFFSET

static inline uint32_t chksum_up(uint8_t byte, uint32_t hash)
{
    return fnv1a_up(byte, hash);
}

static inline uint32_t chksum_update(uint8_t const * data, size_t len, uint32_t ini_chksum)
{
  return fnv1a_(data, len, ini_chksum);
}

// Copy data to the buffer and append checksum
static inline uint32_t chksum_copy(uint8_t const * data, size_t len, uint8_t * out_buff, uint32_t ini_chksum)
{
  union {
    uint32_t val;
    uint8_t byte[4];
  } chksum;
  chksum.val = fnv1a_copy_(data, len, out_buff, ini_chksum);
  out_buff += len;
  out_buff[0] = chksum.byte[0];
  out_buff[1] = chksum.byte[1];
  out_buff[2] = chksum.byte[2] ^ chksum.byte[3];
  return chksum.val;
}

// Validate checksum given the buffer with data followed by checksum value
static inline bool chksum_validate(uint8_t const * buff, size_t len, uint32_t * inout_chksum)
{
  union {
    uint32_t val;
    uint8_t byte[4];
  } chksum;
  *inout_chksum = chksum.val = fnv1a_(buff, len, *inout_chksum);
  buff += len;
  return
    buff[0] == chksum.byte[0] &&
    buff[1] == chksum.byte[1] &&
    buff[2] == (chksum.byte[2] ^ chksum.byte[3]);
}
