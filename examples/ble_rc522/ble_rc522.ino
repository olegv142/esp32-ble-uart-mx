/*
 Example of the RF ID detector based on MFRC522 board sending data via Bluetooth.
 It broadcasts 2 types of messages:
  I<version in hex><chip version in hex>  - idle message, for ex. I0118 is transmitted for chines RC522 clone
  D<UID bytes in hex>                     - card detected message, for example D350b1806
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
#include "rc522_cfg.h"

#include <SPI.h>
#include <MFRC522.h>
#include <malloc.h>

#ifdef NEO_PIXEL_PIN
#include "neopix.h"
#endif

#ifdef EXT_FRAMES
#include "checksum.h"
#include "xframe.h"
#endif

static void rc522_start();

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
  rc522_start();
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

static unsigned chk_errors()
{
  return chk_error_cnt(&bt_notify_err, "notify failed");
}

//------------------------- MFRC522 stuff

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance

static void rc522_start()
{
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  mfrc522.PCD_Init();   // Init MFRC522
}

static void rc522_tell_version()
{
  unsigned const chip_ver = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  char buff[6] = {'I'};
  print_hex_byte(VERSION, buff + 1);
  print_hex_byte(chip_ver, buff + 3);
  if (!transmit_to_central((uint8_t const*)buff, 5))
    is_congested = true;
}

static void rc522_tell_uid()
{
  char buff[1+2*sizeof(mfrc522.uid.uidByte)+1] = {'D'};
  for (byte i = 0; i < mfrc522.uid.size; ++i)
    print_hex_byte(mfrc522.uid.uidByte[i], buff + 1 + 2*i);
  if (!transmit_to_central((uint8_t const*)buff, 1 + 2*mfrc522.uid.size))
    is_congested = true;
}

static void rc522_poll()
{
  static uint32_t last_status_ts;
  uint32_t const now = millis();
  bool new_card = false;
  if ((new_card = mfrc522.PICC_IsNewCardPresent()) || elapsed(last_status_ts, now) >= STATUS_REPORT_INTERVAL) {
    last_status_ts = now;
    if (!new_card) {
      byte atqa_answer[2];
      byte atqa_size = 2;
      mfrc522.PICC_WakeupA(atqa_answer, &atqa_size);
    }
    if (!mfrc522.PICC_ReadCardSerial()) {
      rc522_tell_version();
    } else {
      rc522_tell_uid();
    }
    mfrc522.PICC_HaltA();
  }
}

//------------------------- Main loop

void loop()
{
  bool const was_congested = is_congested;
  is_congested = false;

  bt_maybe_start_advertising();

  if (bt_advertising_enabled)
    rc522_poll();

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
