/*
 Example of the simple peripheral device sending dummy data with regular intervals.
 Use https://olegv142.github.io/esp32-ble-uart-mx/?dual&xf for receiving those messages.
 Author: Oleg Volkov
*/

#include "mx_config.h"
#include "mx_types.h"
#include "mx_uart.h"
#include "bt_device.h"
#include "stream_tags.h"
#include "watchdog.h"
#include "debug.h"

#include <malloc.h>

#ifdef NEO_PIXEL_PIN
#include "neopix.h"
#endif

#ifdef EXT_FRAMES
#include "checksum.h"
#include "xframe.h"
#endif

#ifndef TELL_UPTIME
#define TELL_UPTIME 1000
#endif

static bool is_congested;

#ifdef NEO_PIXEL_PIN
#define NPX_LED_BITS (3*8)
#define NPX_IDLE_DELAY 2
static rmt_data_t  neopix_led[cx_status_cnt][NPX_LED_BITS];
static rmt_data_t* neopix_write_data;
static cx_status_t neopix_conn_status;
static bool        neopix_user_controlled;

static inline void neopix_conn_set(cx_status_t sta)
{
  if (!neopix_user_controlled)
    neopix_write_data = neopix_led[sta];
  neopix_conn_status = sta;
}

static void neopix_init()
{
  neopix_led_data_init(neopix_led[cx_idle],              IDLE_RGB);
  neopix_led_data_init(neopix_led[cx_establishing],      CONNECTING_RGB);
  neopix_led_data_init(neopix_led[cx_active],            ACTIVE_RGB);
  neopix_led_data_init(neopix_led[cx_passive],           PASSIVE_RGB);
  neopix_led_data_init(neopix_led[cx_active_congested],  ACTIVE_CONGESTED_RGB);
  neopix_led_data_init(neopix_led[cx_passive_congested], PASSIVE_CONGESTED_RGB);

  if (!neopix_led_init(NEO_PIXEL_PIN)) {
    debug_strz("neopixel pin init failed");
    return;
  }
  neopix_conn_set(cx_idle);
  delay(NPX_IDLE_DELAY+1);
}

static void neopix_process()
{
  if (!neopix_write_data)
    return;
  static uint32_t last_set;
  uint32_t const now = millis();
  if (elapsed(last_set, now) < NPX_IDLE_DELAY)
    return;
  neopix_led_write(NEO_PIXEL_PIN, neopix_write_data);
  neopix_write_data = NULL;
  last_set = now;
}

static inline void neopix_conn_up(cx_status_t sta)
{
  if (neopix_conn_status != sta)
    neopix_conn_set(sta);
}
#endif // NEO_PIXEL_PIN

static inline c_status_t get_connect_status()
{
  return !bt_connected_centrals ? c_idle : c_passive;
}

static inline cx_status_t get_connect_status_ex(bool congested)
{
  switch (get_connect_status()) {
  case c_idle:
    return cx_idle;
  case c_establishing:
    return cx_establishing;
  case c_active:
    return congested ? cx_active_congested : cx_active;
  case c_passive:
    return congested ? cx_passive_congested : cx_passive;
  default:
    BUG();
    return cx_status_cnt;
  }
}

static inline bool get_connected_indicator()
{
  return get_connect_status() >= c_active;
}

static void show_conn_status(bool congested = false)
{
#ifdef NEO_PIXEL_PIN
  neopix_conn_up(get_connect_status_ex(congested));
#elif defined(CONNECTED_LED)
  digitalWrite(CONNECTED_LED, get_connected_indicator() ? CONNECTED_LED_LVL : !(CONNECTED_LED_LVL));
#endif
}

#if defined(EXT_FRAMES)
static uint8_t* get_chunk_buff(size_t sz, void* ctx)
{
  static uint8_t buff[MAX_SIZE];
  BUG_ON(sz > MAX_SIZE);
  return buff;
}
#endif

static void hw_init()
{
  uart_init();

  Serial.begin(UART_BAUD_RATE);

#ifdef NEO_PIXEL_PIN
  neopix_init();
#elif defined(CONNECTED_LED)
  pinMode(CONNECTED_LED, OUTPUT);
  digitalWrite(CONNECTED_LED, !(CONNECTED_LED_LVL));
#endif
}

void setup()
{
  hw_init();
  watchdog_init();
  bt_device_init(nullptr);
  bt_device_start();
}

static bool transmit_chunk_to_central(uint8_t* pdata, size_t sz, void* ctx)
{
  bool const res = bt_transmit_to_central(pdata, sz);
  if (res)
    taskYIELD();
  return res;
}

static bool transmit_to_central(uint8_t const* data, size_t len, bool binary=false)
{
  if (!bt_advertising_enabled) {
    debug_strz("can't transmit while hidden");
    return true;
  }
  if (!len) {
    debug_strz("bad data to transmit");
    return true;
  }
  if (len > MAX_FRAME) {
    debug_strz("data size exceeds limit");
    return true;
  }
#ifdef EXT_FRAMES
  return transmit_xframe(data, len, binary, get_chunk_buff, transmit_chunk_to_central, nullptr);
#else
  return transmit_chunk_to_central((uint8_t*)data, len, nullptr);
#endif
}

static void send_dummy_message()
{
  static uint32_t last_uptime;
  static uint32_t last_uptime_sn;
  uint32_t const uptime = millis();
  if (uptime >= last_uptime + TELL_UPTIME) {
    last_uptime = uptime;
    String msg(++last_uptime_sn);
    msg += "#";
    msg += uptime;
    msg += "#";
    msg += bt_dev_addr;
    msg += "#";
    msg += bt_dev_name;
    if (!transmit_to_central((uint8_t const*)msg.c_str(), msg.length()))
      is_congested = true;
  }
}

static unsigned chk_errors()
{
  return chk_error_cnt(&bt_notify_err, "notify failed");
}

void loop()
{
  bool const was_congested = is_congested;
  is_congested = false;

  bt_maybe_start_advertising();

  if (bt_advertising_enabled)
    send_dummy_message();

  static uint32_t last_err_ts;
  static unsigned last_err_cnt;
  uint32_t const now = millis();
  if (elapsed(last_err_ts, now) > 1000) {
    last_err_cnt = chk_errors();
    if (last_err_cnt)
      last_err_ts = now;
  }
  show_conn_status(was_congested || is_congested || last_err_cnt);
#ifdef NEO_PIXEL_PIN
  neopix_process();
#endif
  if (!is_congested) {
    watchdog_reset();
    delay(IDLE_DELAY);
  } else
    delay(CONGESTION_DELAY);
}
