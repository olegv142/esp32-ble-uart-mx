#pragma once

#include <esp32-hal-rmt.h>

#define NPX_LED_BITS (3*8)

static inline void neopix_led_data_init(rmt_data_t led_data[NPX_LED_BITS], uint8_t r, uint8_t g, uint8_t b)
{
  int i = 0;
  // Color coding is in order GREEN, RED, BLUE
  uint8_t const color[] = {g, r, b};
  for (int col = 0; col < 3; col++) {
    for (int bit = 0; bit < 8; bit++) {
      if ((color[col] & (1 << (7 - bit)))) {
        // HIGH bit
        led_data[i].level0 = 1;     // T1H
        led_data[i].duration0 = 8;  // 0.8us
        led_data[i].level1 = 0;     // T1L
        led_data[i].duration1 = 4;  // 0.4us
      } else {
        // LOW bit
        led_data[i].level0 = 1;     // T0H
        led_data[i].duration0 = 4;  // 0.4us
        led_data[i].level1 = 0;     // T0L
        led_data[i].duration1 = 8;  // 0.8us
      }
      i++;
    }
  }
}

static inline bool neopix_led_init(int pin)
{
  return rmtInit(NEO_PIXEL_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
}

static inline void neopix_led_write(int pin, rmt_data_t led_data[NPX_LED_BITS])
{
  rmtWrite(pin, led_data, NPX_LED_BITS, RMT_WAIT_FOR_EVER);
}
