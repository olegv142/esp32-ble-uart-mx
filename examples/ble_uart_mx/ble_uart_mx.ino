/*
 This is the dual role BLE device capable of connecting to multiple peripheral devices.
 It was designed as multipurpose BLE to serial adapter accepting commands from controlling host via serial link.
 The primary use case is gathering telemetry data from transmitters and providing communication link for other
 central for commands / responses. The controlling host uses the following protocol:

 Commands:
  '#C addr0 addr1 ..' - connect to peripherals with given addresses (up to 4)
  '#A'                - start advertising if was hidden
  '#R'                - reset to idle state
  '#L r g b'          - manually control the on-board neo-pixel LED by setting rgb values
  '#L'                - switch to auto control of the on-board neo-pixel LED
  'Kseed&salt'        - set authentication key
 Connect command will be disabled if AUTOCONNECT is defined. The L command is not available
 unless NEO_PIXEL_PIN and LED_CONTROL_API are defined.

 Status messages:
  ':I[h] vmaj.vmin-maxframe-variant [passkey]' - idle, not connected, 'h' if hidden
  ':Cn'      - connecting to the n-th peripheral
  ':D[h]'    - all peripherals connected, data receiving, 'h' if hidden
  Status messages will be disabled if STATUS_REPORT_INTERVAL is undefined

 Debug messages:
  '-message'

 Every in/out message on physical UART is started with '\1' end with '\0'. 
 In case USB VCP is used for communications there is no start symbol, end symbol is '\n' by default

 Second symbol of out message is
  ':' for status messages
  '-' for debug messages
  '<' if message is received from connected central
  '0', '1' .. '7'  for data received from peripheral 0, 1, .. 7
 The first data stream message after connection / re-connection has no data payload. This is the stream start token.

 The second symbol for input message is
  '#' for commands
  '>' for message to be sent to connected central
  '0', '1' .. '7'  for data to be sent to peripheral 0, 1, .. 7

 The maximum size of data in single message is MAX_CHUNK.
 Larger amount of data should be split onto chunks before sending them to the adapter.

 Tested on ESP32 C3 with SDK v.3.0
 Use python/ble_multi_adapter.py for interfacing at the host side.

 Author: Oleg Volkov
*/

#include <BLEDevice.h>
#include <freertos/queue.h>
#include <rom/md5_hash.h>
#include <malloc.h>

#include "mx_config.h"
#include "mx_types.h"
#include "mx_uart.h"
#include "bt_device.h"
#include "bt_remote.h"
#include "stream_tags.h"
#include "watchdog.h"
#include "debug.h"

#ifdef NEO_PIXEL_PIN
#include "neopix.h"
#endif

#ifdef BINARY_DATA_SUPPORT
#include "mx_encoding.h"
#endif

#ifdef EXT_FRAMES
#include "checksum.h"
#include "xframe.h"
#endif

#ifdef SIMPLE_LINK
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

//----- Static data ----------------

class Peer;
static Peer*    peers[MAX_PEERS];
static unsigned npeers;
static int      connected_peers;

#ifdef EXT_FRAMES
static XFrameReceiverToUart centr_xrx('<');
#endif

static uint8_t  auth_key[] = { AUTH_KEY };
static uint8_t  passkey[16];
static bool     passkey_valid;

#define PASSKEY_B64_LEN MAX_BASE64_ENCODED_LEN(PASSKEY_LEN)
static char     passkey_b64[PASSKEY_B64_LEN];

#ifdef STATUS_REPORT_INTERVAL
static uint32_t last_status_ts;
#endif

#define CLI_BUFF_SZ UART_RX_BUFFER_SZ
static uint8_t   cli_buff[CLI_BUFF_SZ];
static size_t    cli_buff_data_sz;

static uint8_t last_rx_tag;

static QueueHandle_t rx_queue;

static struct err_count rx_queue_full;
static struct err_count write_err;
static struct err_count parse_err;
static struct err_count lost_frames;

static bool   is_congested;

#ifdef NEO_PIXEL_PIN
#define NPX_LED_BITS (3*8)
#define NPX_IDLE_DELAY 2
static rmt_data_t  neopix_led[cx_status_cnt][NPX_LED_BITS];
static rmt_data_t* neopix_write_data;
static cx_status_t neopix_conn_status;
static bool        neopix_user_controlled;
#ifdef LED_CONTROL_API
static rmt_data_t  neopix_user_data[NPX_LED_BITS];
#endif

//----- Helper functions ----------------

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
  if (!npeers)
    return !bt_connected_centrals ? c_idle : c_passive;
  else
    return connected_peers < npeers ? c_establishing : c_active;
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

static inline bool is_idle()
{
  return !npeers;
}

static inline bool is_connected()
{
  return npeers && connected_peers >= npeers;
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

static inline bool transmit_plain(
    uint8_t* pdata, size_t len,
    uint8_t* (*get_chunk)(size_t sz, void* ctx),
    bool (*tx_chunk)(uint8_t* chunk, size_t sz, void* ctx),
    void* ctx
  )
{
  if (get_chunk) {
    uint8_t* const chunk_buff = get_chunk(len, ctx);
    if (!chunk_buff)
      return false;
    memcpy(chunk_buff, pdata, len);
    pdata = chunk_buff;
  }
  return tx_chunk(pdata, len, ctx);
}

// Optionally split frame onto fragments and transmit it by calling provided callback.
// Returns false if callback returns false which means BLE stack congestion detected.
static bool transmit_frame(
    const char* data, size_t len,
    uint8_t* (*get_chunk)(size_t sz, void* ctx),
    bool (*tx_chunk)(uint8_t* chunk, size_t sz, void* ctx),
    void* ctx
  )
{
  uint8_t* tx_data = (uint8_t*)data;
#ifdef BINARY_DATA_SUPPORT
  static uint8_t* tx_buff;
  uint8_t binary = 0;
  if (len && (binary = (data[0] == ENCODED_DATA_START_TAG))) {
    if (len > 1 + MAX_ENCODED_FRAME_LEN) {
      // All such errors may be due to uart buffer overflow while not using RTS
      // flow control. So just print debug message and return true.
      // Note that returning false means BLE stack congestion.
      debug_strz("encoded data size exceeds limit");
      return true;
    }
    if ((len % 4) != 1) {
      debug_strz("invalid encoded data size");
      return true;
    }
    if (!tx_buff) {
        tx_buff = (uint8_t*)malloc(MAX_FRAME);
        if (!tx_buff) {
            debug_strz("failed to allocate transmit buffer");
            return true;
        }
    }
    len = decode(data + 1, len - 1, tx_data = tx_buff);
  }
#endif

  if (!len) {
    debug_strz("bad data to transmit");
    return true;
  }
  if (len > MAX_FRAME) {
    debug_strz("data size exceeds limit");
    return true;
  }

#ifdef EXT_FRAMES
  return transmit_xframe(tx_data, len, binary, get_chunk, tx_chunk, ctx);
#else
  return transmit_plain(tx_data, len, get_chunk, tx_chunk, ctx);
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

#ifndef EXT_FRAMES
static void uart_print_data(uint8_t const* data, size_t len, char tag)
{
  if (len > MAX_CHUNK)
    fatal("Data size exceeds limit");
  char const * out_data = (char const*)data;
#ifdef BINARY_DATA_SUPPORT
  char enc_buff[1+MAX_ENCODED_CHUNK_LEN] = {ENCODED_DATA_START_TAG};
  if (is_data_binary(data, len)) {
    len = 1 + encode(data, len, enc_buff + 1);
    out_data = enc_buff;
  }
#endif
  uart_begin();
#ifndef SIMPLE_LINK
  uart_print(tag);
#endif
  uart_write(out_data, len);
  uart_end();
}
#endif

//----- Remote client connection class ----------------

class Peer : public RemoteClient
{
public:
  void onConnect(BLEClient *pclient) {
    // post connected event to receive queue
    struct data_chunk ch = {.data = nullptr, .len = 0};
    if (!xQueueSend(m_rx_queue, &ch, 0)) {
      ++m_rx_queue_full.cnt;
      is_congested = true;
    }
    m_connected = true;
  }

  void onDisconnect(BLEClient *pclient) {
    m_connected = false;
    xSemaphoreGive(m_wr_sem);
  }

  void report_connecting() {
#ifdef STATUS_REPORT_INTERVAL
    uart_begin();
    uart_print_strz(":C");
    uart_print(m_tag);
    uart_end();
#endif
  }

  void notify_connected() {
    uart_begin();
#ifndef SIMPLE_LINK
    uart_print(m_tag);
#endif
    uart_end();
#ifndef NO_DEBUG
    BLEAddress addr(m_addr);
    esp_gap_conn_params_t params;
    esp_err_t const err = esp_ble_get_current_conn_params(*addr.getNative(), &params);
    if (err == ESP_OK) {
        uart_debug_begin();
        uart_print_strz("[");
        uart_print(m_tag);
        uart_print_strz("] interval=");
        uart_print(params.interval);
        uart_print_strz(" latency=");
        uart_print(params.latency);
        uart_print_strz(" timeout=");
        uart_print(params.timeout);
        uart_end();
    } else {
        debug_strz("failed to query connection params");
    }
#endif
  }

  void connect()
  {
    report_connecting();
    show_conn_status();
    if (!m_rx_queue) {
      m_rx_queue = xQueueCreate(RX_QUEUE, sizeof(struct data_chunk));
      if (!m_rx_queue)
        fatal("No memory");
    }
    RemoteClient::connect();
  }

  bool remote_write(uint8_t* data, size_t len)
  {
    xSemaphoreTake(m_wr_sem, portMAX_DELAY);
    if (!m_connected) {
      xSemaphoreGive(m_wr_sem);
      return true;
    }
    if (!send_data(data, len, true))
    {
      // failed due to congestion
      xSemaphoreGive(m_wr_sem);
      ++write_err.cnt;
      is_congested = true;
      return false;
    }
    taskYIELD();
    return true;
  }

  bool on_gattc_evt(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
  {
    if (gattc_if != m_Client->getGattcIf())
      return false;
    if (event == ESP_GATTC_WRITE_CHAR_EVT)
      xSemaphoreGive(m_wr_sem);
    return true;
  }

  uint8_t* alloc_chunk_queued(size_t sz)
  {
    void* pchunk = nullptr;
    BaseType_t const res = xRingbufferSendAcquire(m_wr_queue, &pchunk, sz, pdMS_TO_TICKS(CONGESTION_DELAY));
    if (res != pdTRUE) {
      ++m_tx_queue_full.cnt;
      return nullptr;
    }
    return (uint8_t*)pchunk;
  }

  bool transmit_chunk_queued(uint8_t* chunk, size_t sz)
  {
    BaseType_t const res = xRingbufferSendComplete(m_wr_queue, chunk);
    BUG_ON(res != pdTRUE);
    return true;
  }

  static uint8_t* alloc_chunk_queued_(size_t sz, void* ctx)
  {
    return ((Peer*)ctx)->alloc_chunk_queued(sz);
  }

  static bool transmit_chunk_queued_(uint8_t* chunk, size_t sz, void* ctx)
  {
    return ((Peer*)ctx)->transmit_chunk_queued(chunk, sz);
  }

  bool transmit(const char* data, size_t len)
  {
    if (!m_writable)
      fatal("Peer is not writable");
    return transmit_frame(data, len, alloc_chunk_queued_, transmit_chunk_queued_, this);
  }

  virtual void notify_data(uint8_t *pData, size_t length)
  {
    struct data_chunk ch = {.data = (uint8_t*)malloc(length), .len = length};
    if (!ch.data)
      fatal("No memory");
    memcpy(ch.data, pData, length);
    if (!xQueueSend(m_rx_queue, &ch, 0)) {
      free(ch.data);
      ++m_rx_queue_full.cnt;
      is_congested = true;
    }
  }

  void receive(struct data_chunk const* chunk)
  {
#ifdef ECHO
    if (m_writable && is_connected())
      if (!send_data(chunk->data, chunk->len, false)) {
        ++write_err.cnt;
        is_congested = true;
      }
#endif
#ifdef EXT_FRAMES
    m_xrx.receive(chunk);
#else
    uart_print_data(chunk->data, chunk->len, m_tag);
    free(chunk->data);
#endif
  }

  bool monitor()
  {
    if (m_connected != m_was_connected) {
      m_was_connected = m_connected;
      String msg("peripheral [");
      msg += m_tag;
      msg += "] ";
      msg += m_addr;
      if (m_connected) {
        ++connected_peers;
        msg += " connected";
        debug_msg(msg.c_str());
      } else {
        --connected_peers;
        msg += " disconnected";
        fatal(msg.c_str());
      }
    }
    if (!m_connected) {
      connect();
      return false;
    }
    if (is_connected()) {
      // Make sure all connected before going further
      if (!m_subscribed) {
        subscribe();
        return false;
      }
      struct data_chunk ch;
      while (m_rx_queue && xQueueReceive(m_rx_queue, &ch, 0)) {
        if (ch.data) {
          receive(&ch);
        } else {
          notify_connected();
#ifdef EXT_FRAMES
          m_xrx.reset();
#endif
        }
      }
    }
    return true;
  }

  unsigned chk_errors()
  {
    return chk_error_cnt2(&m_rx_queue_full, "rx queue [", m_tag, "] full")
         + chk_error_cnt2(&m_tx_queue_full, "tx queue [", m_tag, "] full");
  }

  void write_worker()
  {
    for (;;) {
      size_t size = 0;
      uint8_t *data = (uint8_t*)xRingbufferReceive(m_wr_queue, &size, portMAX_DELAY);
      if (!data || !size)
        continue;
      BUG_ON(size > MAX_SIZE);
      while (!remote_write(data, size))
        vTaskDelay(pdMS_TO_TICKS(CONGESTION_DELAY));
      vRingbufferReturnItem(m_wr_queue, data);
    }
  }

  static void write_worker_(void* ctx) {
    ((Peer*)ctx)->write_worker();
  }

  Peer(unsigned idx, String const& addr)
    : RemoteClient(addr)
    , m_tag('0' + idx)
    , m_connected(false)
    , m_was_connected(false)
    , m_wr_task(nullptr)
    , m_wr_queue(xRingbufferCreateNoSplit(MAX_SIZE, TX_QUEUE))
    , m_wr_sem(xSemaphoreCreateBinary())
    , m_rx_queue(0)
#ifdef EXT_FRAMES
    , m_xrx('0' + idx)
#endif
  {
    BaseType_t const rc = xTaskCreate(write_worker_, "write_worker", 4096, this, uxTaskPriorityGet(nullptr), &m_wr_task);
    BUG_ON(rc != pdPASS);
    BUG_ON(!m_wr_task);
    BUG_ON(!m_wr_queue);
    BUG_ON(!m_wr_sem);
    xSemaphoreGive(m_wr_sem);
  }

private:
  char const  m_tag;
  bool        m_connected;
  bool        m_was_connected;

  TaskHandle_t             m_wr_task;
  RingbufHandle_t          m_wr_queue;
  SemaphoreHandle_t        m_wr_sem;
  QueueHandle_t            m_rx_queue;
  struct err_count         m_rx_queue_full;
  struct err_count         m_tx_queue_full;
#ifdef EXT_FRAMES
  XFrameReceiverToUart m_xrx;
#endif
};

#ifndef PASSIVE_ONLY
static void add_peer(unsigned idx, String const& addr)
{
  if (idx >= MAX_PEERS)
    fatal("Only " STRINGIZE(MAX_PEERS) " peer(s) allowed");
  if (peers[idx])
    fatal("Peer already exist");
  peers[idx] = new Peer(idx, addr);
  ++npeers;
#ifndef NO_DEBUG
  uart_debug_begin();
  uart_print_strz("connection [");
  uart_print(idx);
  uart_print_strz("] initialized, ");
  uart_print(ESP.getFreeHeap());
  uart_print_strz(" bytes free");
  uart_end();
#endif
}
#endif

//----- Initialization routines ----------------

static void bt_gattc_event_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
  for (unsigned i = 0; i < MAX_PEERS; ++i)
    if (peers[i] && peers[i]->on_gattc_evt(event, gattc_if, param))
      return;
}

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

static void bt_rx_cb(struct data_chunk* ch)
{
   if (!xQueueSend(rx_queue, ch, 0)) {
      free(ch->data);
      ++rx_queue_full.cnt;
      is_congested = true;
    }
}

void setup()
{
  rx_queue = xQueueCreate(RX_QUEUE, sizeof(struct data_chunk));
  if (!rx_queue)
    fatal("No memory");

#ifdef PEER_ADDR
  add_peer(0, PEER_ADDR);
#endif
#ifdef PEER_ADDR1
  add_peer(1, PEER_ADDR1);
#endif
#ifdef PEER_ADDR2
  add_peer(2, PEER_ADDR2);
#endif
#ifdef PEER_ADDR3
  add_peer(3, PEER_ADDR3);
#endif

  hw_init();
  watchdog_init();
  bt_device_init(bt_rx_cb);
  BLEDevice::setCustomGattcHandler(bt_gattc_event_cb);
  bt_device_start();
}

//----- Message parsing and API implementation ----------------

static bool transmit_chunk_to_central(uint8_t* pdata, size_t sz, void* ctx)
{
  bool const res = bt_transmit_to_central(pdata, sz);
  if (res)
    taskYIELD();
  return res;
}

static bool transmit_to_central(const char* data, size_t len)
{
  if (!bt_advertising_enabled) {
    debug_strz("can't transmit while hidden");
    return true;
  }
#ifdef EXT_FRAMES
  return transmit_frame(data, len, get_chunk_buff, transmit_chunk_to_central, nullptr);
#else
  return transmit_frame(data, len, nullptr, transmit_chunk_to_central, nullptr);
#endif
}

static bool transmit_to_peer(unsigned idx, const char* str, size_t len)
{
  if (idx >= MAX_PEERS || !peers[idx]) {
    debug_strz("bad peripheral index");
    return true;
  }
  return peers[idx]->transmit(str, len);
}

#ifndef AUTOCONNECT
static void cmd_connect(const char* param, size_t len)
{
  if (npeers) {
    debug_strz("already connected");
    return;
  }
  String params(param, len);
  const char *str = params.c_str();
  const char *ptr = str;
  while (*ptr)
  {
    while (isspace(*ptr))
      ++ptr;
    const char* begin = ptr;
    while (*ptr && !isspace(*ptr))
      ++ptr;
    if (ptr != begin)
      add_peer(npeers, params.substring(begin - str, ptr - str));
  }
}
#endif

static void cmd_key(const char* param, size_t len)
{
  const char* const sep = (const char*)memchr(param, '&', len);
  if (!sep) {
    debug_strz("key separator not found");
    return;
  }
  size_t const seed_len = sep - param;
  size_t const salt_len = len - seed_len - 1;
  if (!seed_len) {
    debug_strz("seed is empty");
    return;
  }
  if (!salt_len) {
    debug_strz("salt is empty");
    return;
  }
  struct MD5Context ctx;
  unsigned char key[16];
  MD5Init(&ctx);
  MD5Update(&ctx, (const unsigned char*)param, seed_len);
  MD5Update(&ctx, auth_key, sizeof(auth_key));
  MD5Final(key, &ctx);
  MD5Init(&ctx);
  MD5Update(&ctx, key, sizeof(key));
  MD5Update(&ctx, (const unsigned char*)(param + seed_len + 1), salt_len);
  MD5Final(passkey, &ctx);
  size_t const sz = encode(passkey, PASSKEY_LEN, passkey_b64);
  BUG_ON(sz != PASSKEY_B64_LEN);
  passkey_valid = true;
}

#ifdef LED_CONTROL_API
static inline void led_cmd_auto(void)
{
  neopix_user_controlled = false;
  neopix_write_data = neopix_led[neopix_conn_status];
}

static inline void led_cmd_rgb(unsigned r, unsigned g, unsigned b)
{
  neopix_user_controlled = true;
  neopix_led_data_init(neopix_user_data, r, g, b);
  neopix_write_data = neopix_user_data;
}

static void cmd_led(const char* param, size_t len)
{
  if (!len) {
      led_cmd_auto();
      return;
  }
  String params(param, len);
  unsigned r, g, b;
  if (3 == sscanf(params.c_str(), " %u %u %u", &r, &g, &b))
      led_cmd_rgb(r, g, b);
  else
      debug_strz("unrecognized parameter");
}
#endif

static void process_cmd(const char* cmd, size_t len)
{
  switch (cmd[0]) {
    case 'R':
      if (len != 1) {
        ++parse_err.cnt;
        return;
      }
      reset_self();
      break;
#ifndef AUTOCONNECT
    case 'C':
      cmd_connect(cmd + 1, len - 1);
      break;
#endif
#if defined(HIDDEN) && !defined(CENTRAL_ONLY)
    case 'A':
      if (len != 1) {
        ++parse_err.cnt;
        return;
      }
      bt_advertising_enabled = true;
      break;
#endif
    case 'K':
      cmd_key(cmd + 1, len - 1);
      break;
#ifdef LED_CONTROL_API
    case 'L':
      cmd_led(cmd + 1, len - 1);
      break;
#endif
    default:
      debug_strz("unrecognized command");
      ++parse_err.cnt;
  }
}

static inline bool chk_stream_tags(uint8_t topen, uint8_t tclose, size_t len)
{
  if (len <= 2) {
    ++parse_err.cnt;
    return false;
  }
  if (tclose != closing_stream_tag(topen, len - 2)) {
    ++parse_err.cnt;
    return false;
  }
  if (last_rx_tag) {
    uint8_t const next_tag = next_stream_tag(last_rx_tag);
    if (topen != next_tag) {
      unsigned const lost = topen > next_tag ? topen - next_tag : topen + STREAM_TAGS_MOD - next_tag;
      lost_frames.cnt += lost;
    }
  }
  return true;
}

static bool process_msg(const char* str, size_t len)
{
#ifndef SIMPLE_LINK
  switch (str[0]) {
    case '#':
      process_cmd(str + 1, len - 1);
      return true;
    case '>':
      return transmit_to_central(str + 1, len - 1);
    default:
      return transmit_to_peer(str[0] - '0', str + 1, len - 1);
  }
#else
#ifdef CENTRAL_ONLY
  return transmit_to_peer(0, str, len);
#else
  return transmit_to_central(str, len);
#endif
#endif
}

static bool process_msg_(const char* str, size_t len)
{
  uint8_t topen = 0;
  if (!len) {
    ++parse_err.cnt;
    return true;
  }
#if defined(STREAM_TAGS) || !defined(SIMPLE_LINK)
#if defined(STREAM_TAGS) &&  defined(SIMPLE_LINK)
  if (!is_stream_tag(str[0])) {
    ++parse_err.cnt;
    return true;
  }
#else
  // Otherwise stream tags are optional on input
  if (is_stream_tag(str[0]))
#endif
  {
    if (!chk_stream_tags(topen = str[0], str[len-1], len))
      return true;
    str += 1;
    len -= 2;
  }
#endif
  bool const res = process_msg(str, len);
  if (res && topen)
      last_rx_tag = topen;
  return res;
}

static bool cli_process()
{
  size_t const len = cli_buff_data_sz;
  const char * const buff = (const char*)cli_buff;
  const char *next = buff, *end = buff + len;
  bool done = true;

#ifdef UART_BEGIN
  const char* begin = (const char*)memchr(buff, UART_BEGIN, len);
#else
  const char* begin = buff;
#endif
  while (begin && begin < end)
  {
    char* const tail = (char*)memchr(begin, UART_END, len - (begin - buff));
    if (!tail)
      break;
    BUG_ON(tail >= end);
#ifdef UART_BEGIN
    begin += 1;
    char* next_begin;
    for (;;) {
      next_begin = (char*)memchr(begin, UART_BEGIN, len - (begin - buff));
      if (next_begin && next_begin < tail)
        begin = next_begin + 1;
      else
        break;
    }
#endif
    if (!process_msg_(begin, tail - begin)) {
      done = false;
      break;
    }
#ifdef UART_BEGIN
    if (begin != next + 1)
      ++parse_err.cnt;
#endif
    next = tail + 1;
#ifdef UART_BEGIN
    begin = next_begin;
#else
    begin = next;
#endif
  }
  if (next != buff) {
    memmove(cli_buff, next, cli_buff_data_sz = end - next);
  } else if (cli_buff_data_sz >= CLI_BUFF_SZ) {
    debug_strz("rx buffer reset");
    cli_buff_data_sz = 0;
  }
  return done;
}

#ifdef STATUS_REPORT_INTERVAL
static inline void report_idle()
{
  uart_begin();
  if (bt_advertising_enabled)
    uart_print_strz(":I " VMAJOR "." VMINOR "-");
  else
    uart_print_strz(":Ih " VMAJOR "." VMINOR "-");
  uart_print(MAX_FRAME);
  uart_print_strz("-" VARIANT);
  if (passkey_valid) {
    uart_print(' ');
    uart_write(passkey_b64, PASSKEY_B64_LEN);
  }
  uart_end();
}

static inline void report_connected()
{
  uart_begin();
  if (bt_advertising_enabled)
    uart_print_strz(":D");
  else
    uart_print_strz(":Dh");
  uart_end();
}
#endif

static void monitor_peers()
{
  for (unsigned i = 0; i < MAX_PEERS; ++i)
    if (peers[i] && !peers[i]->monitor())
      break;

#ifdef STATUS_REPORT_INTERVAL
  if (!is_congested) {
    uint32_t const now = millis();
    if (!last_status_ts || elapsed(last_status_ts, now) >= STATUS_REPORT_INTERVAL) {
      if (is_idle())
        report_idle();
      else if (is_connected())
        report_connected();
      last_status_ts = now;
    }
  }
#endif
}

#ifdef TELL_UPTIME
static void tell_uptime()
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
    if (!transmit_to_central(msg.c_str(), msg.length()))
      is_congested = true;
  }
}
#endif

static void receive_from_central(struct data_chunk const* chunk)
{
#ifdef EXT_FRAMES
  centr_xrx.receive(chunk);
#else
  uart_print_data(chunk->data, chunk->len, '<');
  free(chunk->data);
#endif
}

static bool cli_receive()
{
  size_t avail = DataSerial.available();
  if (cli_buff_data_sz + avail > CLI_BUFF_SZ) {
    avail = CLI_BUFF_SZ - cli_buff_data_sz;
  }
  if (!avail)
    return false;
  size_t const sz = DataSerial.read(cli_buff + cli_buff_data_sz, avail);
  BUG_ON(sz > avail);
  if (!sz)
    return false;
  cli_buff_data_sz += sz;
  return true;
}

static unsigned chk_errors()
{
  unsigned err_cnt =
      chk_error_cnt(&bt_unknown_data_src, "got data from unknown source")
    + chk_error_cnt(&rx_queue_full, "rx queue full")
    + chk_error_cnt(&write_err,     "write failed")
    + chk_error_cnt(&bt_notify_err, "notify failed")
    + chk_error_cnt(&parse_err,     "parse error")
    + chk_error_cnt(&lost_frames,   "serial frame lost")
    + chk_error_cnt(&bad_chunks,    "bad chunks dropped")
    + chk_error_cnt(&skip_chunks,   "chunks skipped")
    ;
  for (unsigned i = 0; i < MAX_PEERS; ++i)
    if (peers[i])
      err_cnt += peers[i]->chk_errors();
  return err_cnt;
}

//----- Main loop ----------------

void loop()
{
  bool const received = cli_receive();
  bool const was_congested = is_congested;
  if (received || is_congested)
    is_congested = !cli_process();

  bt_maybe_start_advertising();

#ifdef TELL_UPTIME
  if (bt_advertising_enabled)
    tell_uptime();
#endif

  struct data_chunk ch;
  while (xQueueReceive(rx_queue, &ch, 0)) {
    if (ch.data) {
      receive_from_central(&ch);
    } else {
      // Output stream start tag
      uart_begin();
#ifndef SIMPLE_LINK
      uart_print('<');
#endif
      uart_end();
#ifdef EXT_FRAMES
      centr_xrx.reset();
#endif
    }
  }

  monitor_peers();

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
