#pragma once

#include <Arduino.h>

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x

#define STRZ_LEN(s) (sizeof(s)-1)

static inline char hex_digit(uint8_t v)
{
    return v < 10 ? '0' + v : 'A' + v - 10;
}

static inline uint32_t elapsed(uint32_t from, uint32_t to)
{
  return to < from ? 0 : to - from;
}

static inline uint32_t elapsed_since(uint32_t from_ms)
{
  return elapsed(from_ms, millis());
}
