#pragma once

#include <stdint.h>

static uint32_t fnv1a(uint8_t const * buff, size_t len)
{
  uint32_t const prime = 16777619;  
  uint32_t hash = 2166136261;
  for (size_t i = 0; i < len; ++i) {
    hash = hash ^ buff[i];
    hash *= prime;
  }
  return hash;
}

static uint32_t fnv1a_copy(uint8_t const * buff, size_t len, uint8_t * out_buff)
{
  uint32_t const prime = 16777619;  
  uint32_t hash = 2166136261;
  for (size_t i = 0; i < len; ++i) {
    uint8_t const byte = buff[i];
    hash = hash ^ byte;
    hash *= prime;
    *out_buff++ = byte;
  }
  return hash;
}
