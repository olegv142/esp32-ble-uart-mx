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

typedef enum {
  c_idle,
  c_establishing,
  c_active,
  c_passive,
  c_status_cnt
} c_status_t;

typedef enum {
  cx_idle,
  cx_establishing,
  cx_active,
  cx_passive,
  cx_active_congested,
  cx_passive_congested,
  cx_status_cnt
} cx_status_t;
