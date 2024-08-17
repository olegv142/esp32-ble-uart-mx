#pragma once

#include "mx_config.h"
#include "fnv_hash.h"
#include "debug.h"

STATIC_ASSERT(CHKSUM_SIZE==sizeof(uint32_t));

// Copy data to the buffer and append checksum
static inline void chksum_copy(uint8_t const * data, size_t len, uint8_t * out_buff)
{
  union {
    uint32_t val;
    uint8_t byte[4];
  } chksum;
  chksum.val = fnv1a_copy(data, len, out_buff);
  out_buff += len;
  out_buff[0] = chksum.byte[0];
  out_buff[1] = chksum.byte[1];
  out_buff[2] = chksum.byte[2];
  out_buff[3] = chksum.byte[3];
}

// Validate checksum given the buffer with data followed by checksum value
static inline bool chksum_validate(uint8_t const * buff, size_t len)
{
  union {
    uint32_t val;
    uint8_t byte[4];
  } chksum;
  if (len <= 4)
    return false;
  size_t const data_len = len - 4;
  chksum.val = fnv1a(buff, data_len);
  buff += data_len;
  return
    buff[0] == chksum.byte[0] &&
    buff[1] == chksum.byte[1] &&
    buff[2] == chksum.byte[2] &&
    buff[3] == chksum.byte[3];
}
