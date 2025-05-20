#pragma once

#include <stddef.h>
#include <stdint.h>
#include "mx_config.h"

static inline bool is_stream_tag(uint8_t c)
{
  return c >= STREAM_TAG_FIRST && c < STREAM_TAG_FIRST + STREAM_TAGS_MOD;
}

static inline uint8_t next_stream_tag(uint8_t c)
{
  if (++c < STREAM_TAG_FIRST + STREAM_TAGS_MOD)
    return c;
  return STREAM_TAG_FIRST;
}

static inline uint8_t closing_stream_tag(uint8_t open_tag, size_t msg_sz)
{
  return STREAM_TAG_FIRST + (open_tag - STREAM_TAG_FIRST + msg_sz) % STREAM_TAGS_MOD;
}
