#pragma once

#include <stdint.h>

#define FNV32_PRIME  16777619
#define FNV32_OFFSET 2166136261

static inline uint32_t fnv1a_up(uint8_t byte, uint32_t hash)
{
    return (hash ^ byte) * FNV32_PRIME;
}

static inline uint32_t fnv1a_(uint8_t const * buff, size_t len, uint32_t hash)
{
  for (size_t i = 0; i < len; ++i)
    hash = fnv1a_up(buff[i], hash);
  return hash;
}

static inline uint32_t fnv1a(uint8_t const * buff, size_t len)
{
  return fnv1a_(buff, len, FNV32_OFFSET);
}

static inline uint32_t fnv1a_copy_(uint8_t const * buff, size_t len, uint8_t * out_buff, uint32_t hash)
{
  for (size_t i = 0; i < len; ++i) {
    uint8_t const byte = buff[i];
    hash = fnv1a_up(byte, hash);
    *out_buff++ = byte;
  }
  return hash;
}

static inline uint32_t fnv1a_copy(uint8_t const * buff, size_t len, uint8_t * out_buff)
{
  return fnv1a_copy_(buff, len, out_buff, FNV32_OFFSET);
}
