#pragma once

#include <stddef.h>
#include <stdint.h>

struct data_chunk {
  uint8_t* data;
  size_t len;
};

struct err_count {
  unsigned cnt;
  unsigned reported;
  err_count() : cnt(0), reported(0) {}
};
