/*
 This is the dual role BLE device capable of connecting to multiple peripheral devices.
 It was designed as multipurpose BLE to serial adapter accepting commands from controlling host via serial link.
 The primary use case is gathering telemetry data from transmitters and providing communication link for other
 central for commands / responses. The controlling host uses the following protocol:

 Commands:
  '#C addr0 addr1 ..' - connect to peripherals with given addresses (up to 8)
  '#A'                - start advertising if was hidden
  '#R'                - reset to idle state
 Connect command will be disabled if AUTOCONNECT is defined

 Status messages:
  ':I[h] vmaj.vmin-maxframe-variant' - idle, not connected, 'h' if hidden
  ':Cn'      - connecting to the n-th peripheral
  ':D[h]'    - all peripherals connected, data receiving, 'h' if hidden
  Status messages will be disabled if STATUS_REPORT_INTERVAL is undefined

 Debug messages:
  '-message'

 Every in/out message on physical UART is started with '\1' end with '\0'. 
 In case USB VCP is used for communications there is no start symbol, end symbol is always '\n'

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
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLE2902.h>
#include <esp_mac.h>
#include <esp_task_wdt.h>
#include <esp_gatt_defs.h>
#include <esp_gattc_api.h>
#include <string.h>
#include <malloc.h>
#include <freertos/queue.h>
#include <esp_cpu.h>
#include <Esp.h>

#include "mx_config.h"
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

static BLECharacteristic* pCharacteristic;
static uint32_t           last_characteristic_error;

class Peer;
static Peer*    peers[MAX_PEERS];
static unsigned npeers;
static int      connected_peers;
static int      connected_centrals;

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

static inline c_status_t get_connect_status()
{
  if (!npeers)
    return !connected_centrals ? c_idle : c_passive;
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

#ifndef HIDDEN
static bool     advertising_enabled = true;
#else
static bool     advertising_enabled = false;
#endif
static bool     start_advertising = true;
static uint32_t centr_disconn_ts;

#ifdef STATUS_REPORT_INTERVAL
static uint32_t last_status_ts;
#endif

static String   dev_name(DEV_NAME);
static String   dev_addr;

#define CLI_BUFF_SZ UART_RX_BUFFER_SZ
static uint8_t   cli_buff[CLI_BUFF_SZ];
static size_t    cli_buff_data_sz;

#ifdef STREAM_TAGS
static uint8_t last_tx_tag = STREAM_TAG_FIRST - 1;
static uint8_t last_rx_tag;
static unsigned tx_msg_sz;

static inline bool is_stream_tag(uint8_t c)
{
  return c >= STREAM_TAG_FIRST && c < STREAM_TAG_FIRST + STREAM_TAGS_MOD;
}
#endif

static QueueHandle_t rx_queue;

struct err_count {
  unsigned cnt;
  unsigned last;
  err_count() : cnt(0), last(0) {}
};

static struct err_count rx_queue_full;
static struct err_count write_err;
static struct err_count notify_err;
static struct err_count parse_err;
static struct err_count lost_frames;

static bool   is_congested;
static bool   unknown_data_src;

static inline void uart_begin()
{
#ifdef UART_BEGIN
  DataSerial.print(UART_BEGIN);
#endif
#ifdef STREAM_TAGS
  uint8_t next_tag = last_tx_tag + 1;
  if (next_tag >= STREAM_TAG_FIRST + STREAM_TAGS_MOD)
    next_tag = STREAM_TAG_FIRST;
  DataSerial.print((char)next_tag);
  last_tx_tag = next_tag;
  tx_msg_sz = 0;
#endif
}

static inline void uart_end()
{
#ifdef STREAM_TAGS
  DataSerial.print((char)(STREAM_TAG_FIRST + (last_tx_tag - STREAM_TAG_FIRST + tx_msg_sz) % STREAM_TAGS_MOD));
#endif
  DataSerial.print(UART_END);
}

static inline void uart_write(const char* data, size_t sz)
{
  DataSerial.write(data, sz);
#ifdef STREAM_TAGS
  tx_msg_sz += sz;
#endif
}

static inline void uart_print(char c)
{
  uart_write(&c, 1);
}

static void uart_print(const char* str)
{
  uart_write(str, strlen(str));
}

static void uart_print(String const& str)
{
  uart_write(str.c_str(), str.length());
}

static void uart_print(int val)
{
  String s(val);
  uart_write(s.c_str(), s.length());
}

static void uart_print(unsigned val)
{
  String s(val);
  uart_write(s.c_str(), s.length());
}

static void uart_print(unsigned long val)
{
  String s(val);
  uart_write(s.c_str(), s.length());
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

static inline uint32_t elapsed(uint32_t from, uint32_t to)
{
  return to < from ? 0 : to - from;
}

static inline void debug_msg(const char* msg)
{
#ifndef NO_DEBUG
  uart_begin();
  uart_print(msg);
  uart_end();
#endif
}

static unsigned chk_error_cnt(struct err_count* e, const char* msg)
{
  if (e->cnt != e->last) {
#ifndef NO_DEBUG
    uart_begin();
    uart_print(msg);
    uart_print(e->cnt - e->last);
    uart_print(" times");
    uart_end();
#endif
    unsigned const err_cnt = e->cnt - e->last;
    e->last = e->cnt;
    return err_cnt;
  }
  return 0;
}

static unsigned chk_error_cnt2(struct err_count* e, const char* pref, char tag, const char* suff)
{
  if (e->cnt != e->last) {
#ifndef NO_DEBUG
    uart_begin();
    uart_print(pref);
    uart_print(tag);
    uart_print(suff);
    uart_print(e->cnt - e->last);
    uart_print(" times");
    uart_end();
#endif
    unsigned const err_cnt = e->cnt - e->last;
    e->last = e->cnt;
    return err_cnt;
  }
  return 0;
}

static unsigned chk_error_flag(bool* flag, const char* msg)
{
  if (*flag) {
    debug_msg(msg);
    *flag = false;
    return 1;
  }
  return 0;
}

struct data_chunk {
  uint8_t* data;
  size_t len;
};

#ifdef EXT_FRAMES
class XFrameReceiver {
public:
  XFrameReceiver(char tag)
    : m_tag(tag), m_next_sn(0), m_last_chunk(-1) {}

  void receive(struct data_chunk const* chunk)
  {
    uint8_t const h = chunk->data[0];
    uint32_t chksum = h & XH_FIRST ? CHKSUM_INI : m_last_chksum;
    if (chunk->len <= XHDR_SIZE + CHKSUM_SIZE || chunk->len > MAX_SIZE) {
#ifndef NO_DEBUG
      uart_begin();
      uart_print("-invalid chunk size from [");
      uart_print(m_tag);
      uart_print("]");
      uart_end();
#endif
      goto skip;
    }
    if (!(h & XH_FIRST)) {
      if (m_last_chunk < 0)
        goto skip_verbose;
      if (m_next_sn != (h & XH_SN_MASK))
        goto skip_verbose;
      if (m_last_chunk + 1 >= MAX_CHUNKS)
        goto skip_verbose;
    }
    if (!chksum_validate(chunk->data, chunk->len - CHKSUM_SIZE, &chksum)) {
#ifndef NO_DEBUG
      uart_begin();
      uart_print("-invalid checksum from [");
      uart_print(m_tag);
      uart_print("]");
      uart_end();
#endif
      goto skip;
    }
    if (h & XH_FIRST)
      reset();
    m_chunks[++m_last_chunk] = *chunk;
    m_next_sn = (h + 1) & XH_SN_MASK;
    m_last_chksum = chksum;
    if (h & XH_LAST)
      flush();
    return;
  skip_verbose:
#ifdef VERBOSE_DEBUG
    uart_begin();
    uart_print("-skip chunk from [");
    uart_print(m_tag);
    uart_print("]");
    uart_end();
#endif
  skip:
    free(chunk->data);
  }

  void reset()
  {
    for (int i = 0; i <= m_last_chunk; ++i)
      free(m_chunks[i].data);
    m_last_chunk = -1;
  }

private:
  void flush()
  {
    uint8_t const is_binary = m_chunks[0].data[0] & XH_BINARY;
    char enc_buff[MAX_ENCODED_CHUNK_LEN];
    uart_begin();
#ifndef SIMPLE_LINK
    uart_print(m_tag);
#endif
    if (is_binary)
      uart_print(ENCODED_DATA_START_TAG);
    for (int i = 0; i <= m_last_chunk; ++i) {
      size_t len = m_chunks[i].len - XHDR_SIZE - CHKSUM_SIZE;
      const uint8_t* const pchunk = m_chunks[i].data + XHDR_SIZE;
      const char* out_data = (const char*)pchunk;
      if (is_binary) {
        len = encode(pchunk, len, enc_buff);
        out_data = enc_buff;
      }
      uart_write(out_data, len);
    }
    uart_end();
    reset();
  }

  char const m_tag;
  uint8_t    m_next_sn;
  int        m_last_chunk;
  uint32_t   m_last_chksum;
  struct data_chunk m_chunks[MAX_CHUNKS];
};

static XFrameReceiver centr_xrx('<');
#endif

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
  static uint8_t tx_buff[MAX_FRAME];
  uint8_t binary = 0;
  if (len && (binary = (data[0] == ENCODED_DATA_START_TAG))) {
    if (len > 1 + MAX_ENCODED_FRAME_LEN) {
      // All such errors may be due to uart buffer overflow while not using RTS
      // flow control. So just print debug message and return true.
      // Note that returning false means BLE stack congestion.
      debug_msg("-encoded data size exceeds limit");
      return true;
    }
    if ((len % 4) != 1) {
      debug_msg("-invalid encoded data size");
      return true;
    }
    len = decode(data + 1, len - 1, tx_data = tx_buff);
  }
#endif

  if (!len) {
    debug_msg("-bad data to transmit");
    return true;
  }
  if (len > MAX_FRAME) {
    debug_msg("-data size exceeds limit");
    return true;
  }

#ifdef EXT_FRAMES
  static uint8_t last_frame_sn;
  static uint8_t last_chunk_sn;
  uint8_t tx_sn = last_frame_sn;
  bool incomplete = (last_chunk_sn != last_frame_sn);
  uint32_t chksum = CHKSUM_INI;
  uint8_t first = 1, last;
#endif
  while (len) {
    size_t chunk = len;
    uint8_t* pdata = tx_data;
#ifdef EXT_FRAMES
    if (!(last = (chunk <= MAX_CHUNK)))
      chunk = MAX_CHUNK;
    uint8_t const chunk_hdr = mk_xframe_hdr(++tx_sn, binary, first, last);
    chksum = chksum_up(chunk_hdr, chksum);
    if (!incomplete) {
      uint8_t* const chunk_buff = get_chunk(XHDR_SIZE + chunk + CHKSUM_SIZE, ctx);
      if (!chunk_buff)
        return false;
      chunk_buff[0] = chunk_hdr;
      chksum = chksum_copy(tx_data, chunk, chunk_buff + 1, chksum);
      pdata = chunk_buff;
    } else {
      chksum = chksum_update(tx_data, chunk, chksum);
      pdata = nullptr;
    }
    first = 0;
#else
    if (get_chunk) {
      uint8_t* const chunk_buff = get_chunk(chunk, ctx);
      if (!chunk_buff)
        return false;
      memcpy(chunk_buff, pdata, chunk);
      pdata = chunk_buff;
    }
#endif
    if (pdata && !tx_chunk(pdata, XHDR_SIZE + chunk + CHKSUM_SIZE, ctx))
      return false;
    tx_data += chunk;
    len -= chunk;
#ifdef EXT_FRAMES
    if (!incomplete)
      last_chunk_sn = tx_sn;
    else if (last_chunk_sn == tx_sn)
      incomplete = false;
  }
  last_frame_sn = tx_sn;
#else
  }
#endif
  return true;
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

static bool remote_write_(BLERemoteCharacteristic* ch, uint8_t* data, size_t len, bool with_response)
{
  BLEClient * const clnt = ch->getRemoteService()->getClient();
  return ESP_OK == esp_ble_gattc_write_char(
    clnt->getGattcIf(), clnt->getConnId(), ch->getHandle(), len, data,
    with_response ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
    ESP_GATT_AUTH_REQ_NONE
  );
}

class Peer : public BLEClientCallbacks
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
    uart_print(":C");
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
  }

  void connect();
  void subscribe();

  bool remote_write(uint8_t* data, size_t len)
  {
    xSemaphoreTake(m_wr_sem, portMAX_DELAY);
    if (!m_connected) {
      xSemaphoreGive(m_wr_sem);
      return true;
    }
    if (!remote_write_(m_remoteCharacteristic, data, len, true))
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

  bool notify_data(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length)
  {
    if (pBLERemoteCharacteristic != m_remoteCharacteristic)
      return false;

    struct data_chunk ch = {.data = (uint8_t*)malloc(length), .len = length};
    if (!ch.data)
      reset_self();
    memcpy(ch.data, pData, length);
    if (!xQueueSend(m_rx_queue, &ch, 0)) {
      free(ch.data);
      ++m_rx_queue_full.cnt;
      is_congested = true;
    }
    return true;
  }

  void receive(struct data_chunk const* chunk)
  {
#ifdef ECHO
    if (m_writable && is_connected())
      if (!remote_write_(m_remoteCharacteristic, chunk->data, chunk->len, false)) {
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
      String msg("-peripheral [");
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
    return chk_error_cnt2(&m_rx_queue_full, "-rx queue [", m_tag, "] full ")
         + chk_error_cnt2(&m_tx_queue_full, "-tx queue [", m_tag, "] full ");
  }

  void write_worker();

  static void write_worker_(void* ctx) {
    ((Peer*)ctx)->write_worker();
  }

  Peer(unsigned idx, String const& addr)
    : m_tag('0' + idx)
    , m_addr(addr)
    , m_writable(false)
    , m_connected(false)
    , m_subscribed(false)
    , m_was_connected(false)
    , m_Client(nullptr)
    , m_remoteCharacteristic(nullptr)
    , m_wr_task(nullptr)
    , m_wr_queue(xRingbufferCreateNoSplit(MAX_SIZE, TX_QUEUE * MAX_CHUNKS))
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
  String      m_addr;
  bool        m_writable;
  bool        m_connected;
  bool        m_subscribed;
  bool        m_was_connected;

  BLEClient*  m_Client;
  BLERemoteCharacteristic* m_remoteCharacteristic;
  TaskHandle_t             m_wr_task;
  RingbufHandle_t          m_wr_queue;
  SemaphoreHandle_t        m_wr_sem;
  QueueHandle_t            m_rx_queue;
  struct err_count         m_rx_queue_full;
  struct err_count         m_tx_queue_full;
#ifdef EXT_FRAMES
  XFrameReceiver m_xrx;
#endif
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      struct data_chunk ch = {.data = nullptr, .len = 0};
      if (!xQueueSend(rx_queue, &ch, 0)) {
        ++rx_queue_full.cnt;
        is_congested = true;
      }
      ++connected_centrals;
    };

    void onDisconnect(BLEServer* pServer) {
      centr_disconn_ts = millis();
      start_advertising = true;
      --connected_centrals;
    }
};

class MyCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    size_t const length = pCharacteristic->getLength();
    uint8_t* const pData = pCharacteristic->getData();
    struct data_chunk ch = {.data = (uint8_t*)malloc(length), .len = length};
    if (!ch.data)
      reset_self();
    memcpy(ch.data, pData, length);
    if (!xQueueSend(rx_queue, &ch, 0)) {
      free(ch.data);
      ++rx_queue_full.cnt;
      is_congested = true;
    }
#ifdef ECHO
    pCharacteristic->setValue(pData, length);
    pCharacteristic->notify();
#endif
  }
  void onStatus(BLECharacteristic * ch, Status s, uint32_t code) {
    if (ch == pCharacteristic && s == Status::ERROR_GATT)
      last_characteristic_error = code;
  }
};

static void reset_self()
{
  esp_restart();
}

void fatal(const char* what)
{
#ifndef NO_DEBUG
  uart_begin();
  uart_print("-fatal: ");
  uart_print(what);
  uart_end();
#endif
#ifdef HW_UART // Duplicate msg to other uart
  Serial.print("-fatal: ");
  Serial.println(what);
#endif
  delay(100); // give host a chance to read message
  reset_self();
}

void esp_task_wdt_isr_user_handler(void)
{
  reset_self();
}

static inline void watchdog_init()
{
  esp_task_wdt_config_t wdt_cfg = {.timeout_ms = WDT_TIMEOUT, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt_cfg); // enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);             // add current thread to WDT watch
}

#ifndef PASSIVE_ONLY
static void add_peer(unsigned idx, String const& addr)
{
  if (idx >= MAX_PEERS)
    fatal("Bad peripheral index");
  if (peers[idx])
    fatal("Peer already exist");
  peers[idx] = new Peer(idx, addr);
  ++npeers;
#ifndef NO_DEBUG
  uart_begin();
  uart_print("-connection [");
  uart_print(idx);
  uart_print("] initialized, ");
  uart_print(ESP.getFreeHeap());
  uart_print(" bytes free");
  uart_end();
#endif
}
#endif

static inline char hex_digit(uint8_t v)
{
    return v < 10 ? '0' + v : 'A' + v - 10;
}

static inline char byte_signature(uint8_t v)
{
    return hex_digit((v & 0xf) ^ (v >> 4));
}

static void init_dev_name()
{
#ifdef DEV_NAME_SUFF_LEN
  uint8_t mac[8] = {0};
  if (ESP_OK == esp_efuse_mac_get_default(mac)) {
    for (int i = 0; i < DEV_NAME_SUFF_LEN && i < ESP_BD_ADDR_LEN; ++i)
      dev_name += byte_signature(mac[i]);
  }
#endif
}

static void setup_tx_power()
{
#ifdef TX_PW_BOOST
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, TX_PW_BOOST); 
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,     TX_PW_BOOST);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN,    TX_PW_BOOST);
#endif
}

static void bt_gattc_event_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
  for (unsigned i = 0; i < MAX_PEERS; ++i)
    if (peers[i] && peers[i]->on_gattc_evt(event, gattc_if, param))
      return;
}

static void bt_device_init()
{
  // Create the BLE Device
  init_dev_name();
  BLEDevice::init(dev_name);
  BLEDevice::setCustomGattcHandler(bt_gattc_event_cb);
  BLEDevice::setMTU(MAX_SIZE+3);
  setup_tx_power();
  rx_queue = xQueueCreate(RX_QUEUE, sizeof(struct data_chunk));
  if (!rx_queue)
    fatal("No memory");
}

static void bt_device_start()
{
  // Create the BLE Server
  BLEServer* pServer = BLEDevice::createServer();

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
    | BLECharacteristic::PROPERTY_READ
#ifdef WRITABLE
    | BLECharacteristic::PROPERTY_WRITE
    | BLECharacteristic::PROPERTY_WRITE_NR
#endif
  );

  pCharacteristic->setAccessPermissions(
    ESP_GATT_PERM_READ
#ifdef WRITABLE
    | ESP_GATT_PERM_WRITE
#endif
  );

  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCharCallbacks());
  pServer->setCallbacks(new MyServerCallbacks());

  // Start the service
  pService->start();
  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData data;
  data.setName(dev_name);
  pAdvertising->setAdvertisementData(data);
  pAdvertising->setScanResponse(true);
  pAdvertising->addServiceUUID(SERVICE_UUID);

  dev_addr = BLEDevice::getAddress().toString();

#ifndef NO_DEBUG
  uart_begin();
  uart_print("-BT device ");
  uart_print(dev_name);
  uart_print(" at ");
  uart_print(dev_addr);
  uart_print(" on ");
  uart_print(ESP.getChipModel());
  uart_print(" ");
  uart_print(getCpuFrequencyMhz());
  uart_print("MHz ");
  uart_print(ESP.getFreeHeap());
  uart_print(" bytes free");
  uart_end();
#endif
}

#ifdef NEO_PIXEL_PIN
#define NPX_LED_BITS (3*8)
static rmt_data_t neopix_led[cx_status_cnt][NPX_LED_BITS];
static cx_status_t neopix_conn_status;

static inline void neopix_conn_set(cx_status_t sta)
{
  neopix_led_write(NEO_PIXEL_PIN, neopix_led[sta]);
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
    debug_msg("-neopixel pin init failed");
    return;
  }
  neopix_conn_set(cx_idle);
}

static inline void neopix_conn_up(cx_status_t sta)
{
  if (neopix_conn_status != sta)
    neopix_conn_set(sta);
}
#endif

static void hw_init()
{
  DataSerial.setRxBufferSize(UART_RX_BUFFER_SZ);
  DataSerial.setTxBufferSize(UART_TX_BUFFER_SZ);
#ifdef HW_UART
  DataSerial.begin(UART_BAUD_RATE, UART_MODE, UART_RX_PIN, UART_TX_PIN);
#if defined(UART_CTS_PIN) && defined(UART_RTS_PIN)
  DataSerial.setPins(UART_RX_PIN, UART_TX_PIN, UART_CTS_PIN, UART_RTS_PIN);
  DataSerial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS_RTS);
#else
#ifdef UART_CTS_PIN
  DataSerial.setPins(UART_RX_PIN, UART_TX_PIN, UART_CTS_PIN, -1);
  DataSerial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS);
#endif
#ifdef UART_RTS_PIN
  DataSerial.setPins(UART_RX_PIN, UART_TX_PIN, -1, UART_RTS_PIN);
  DataSerial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_RTS);
#endif
#endif
#endif
  DataSerial.setTimeout(UART_TIMEOUT);

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
  bt_device_init();
  bt_device_start();
}

static void peerNotifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
  for (unsigned i = 0; i < MAX_PEERS; ++i)
    if (peers[i] && peers[i]->notify_data(pBLERemoteCharacteristic, pData, length))
      return;

  unknown_data_src = true;
}

void Peer::connect()
{
  report_connecting();

#ifndef NO_DEBUG
  uint32_t const start = millis();
  uart_begin();
  uart_print("-connecting to ");
  uart_print(m_addr);
  uart_end();
#endif

  if (!m_Client) {
    m_Client = BLEDevice::createClient();
    m_Client->setClientCallbacks(this);
  }
  if (!m_rx_queue) {
    m_rx_queue = xQueueCreate(RX_QUEUE, sizeof(struct data_chunk));
    if (!m_rx_queue)
      fatal("No memory");
  }

  m_Client->connect(m_addr);
  m_Client->setMTU(MAX_SIZE+3);  // Request increased MTU from server (default is 23 otherwise)

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = m_Client->getService(SERVICE_UUID);
  // Reset itself on error to avoid dealing with de-initialization
  if (!pRemoteService)
    fatal("Failed to find our service UUID");
  // Obtain a reference to the characteristic in the service of the remote BLE server.
  m_remoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID_TX);
  if (!m_remoteCharacteristic)
    fatal("Failed to find our characteristic UUID");
  if (!m_remoteCharacteristic->canNotify())
    fatal("Notification not supported by the server");
  m_writable = m_remoteCharacteristic->canWrite();

#ifndef NO_DEBUG
  uart_begin();
  uart_print("-connected to ");
  uart_print(m_addr);
  uart_print(" in ");
  uart_print(millis() - start);
  uart_print(" msec");
  if (m_writable)
    uart_print(", writable");
  else
    uart_print(", readonly");
  uart_print(", rssi=");
  uart_print(m_Client->getRssi());
  uart_end();
#endif
}

void Peer::subscribe()
{
#ifndef NO_DEBUG
  uint32_t const start = millis();
  uart_begin();
  uart_print("-subscribing to ");
  uart_print(m_addr);
  uart_end();
#endif

  // Subscribe to updates
  m_remoteCharacteristic->registerForNotify(peerNotifyCallback);
  m_subscribed = true;

#ifndef NO_DEBUG
  uart_begin();
  uart_print("-subscribed to ");
  uart_print(m_addr);
  uart_print(" in ");
  uart_print(millis() - start);
  uart_print(" msec, ");
  uart_print(ESP.getFreeHeap());
  uart_print(" bytes free");
  uart_end();
#endif
}

void Peer::write_worker()
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

static bool transmit_chunk_to_central(uint8_t* pdata, size_t sz, void* ctx)
{
  last_characteristic_error = 0;
  pCharacteristic->setValue(pdata, sz);
  pCharacteristic->notify();
  if (last_characteristic_error) {
    ++notify_err.cnt;
    return false;
  }
  taskYIELD();
  return true;
}

static bool transmit_to_central(const char* data, size_t len)
{
  if (!advertising_enabled) {
    debug_msg("-can't transmit while hidden");
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
    debug_msg("-bad peripheral index");
    return true;
  }
  return peers[idx]->transmit(str, len);
}

#ifndef AUTOCONNECT
static void cmd_connect(const char* param)
{
  String params(param);
  const char* ptr = param;
  if (npeers) {
    debug_msg("-already connected");
    return;
  }
  while (*ptr)
  {
    while (isspace(*ptr))
      ++ptr;
    const char* begin = ptr;
    while (*ptr && !isspace(*ptr))
      ++ptr;
    if (ptr != begin)
      add_peer(npeers, params.substring(begin - param, ptr - param));
  }
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
      cmd_connect(cmd + 1);
      break;
#endif
#if defined(HIDDEN) && !defined(CENTRAL_ONLY)
    case 'A':
      if (len != 1) {
        ++parse_err.cnt;
        return;
      }
      advertising_enabled = true;
      break;
#endif
    default:
      debug_msg("-unrecognized command");
      ++parse_err.cnt;
  }
}

#ifdef STREAM_TAGS
static inline bool chk_stream_tags(uint8_t topen, uint8_t tclose, size_t len)
{
  if (len <= 2) {
    ++parse_err.cnt;
    return false;
  }
  if (last_rx_tag) {
    uint8_t next_tag = last_rx_tag + 1;
    if (next_tag >= STREAM_TAG_FIRST + STREAM_TAGS_MOD)
      next_tag = STREAM_TAG_FIRST;
    if (topen != next_tag) {
      unsigned const lost = topen > next_tag ? topen - next_tag : topen + STREAM_TAGS_MOD - next_tag;
      lost_frames.cnt += lost;
    }
  }
  last_rx_tag = topen;
  if (tclose != STREAM_TAG_FIRST + (topen - STREAM_TAG_FIRST + len - 2) % STREAM_TAGS_MOD) {
    ++parse_err.cnt;
    return false;
  }
  return true;
}
#endif

static bool process_msg(const char* str, size_t len)
{
  if (!len) {
    ++parse_err.cnt;
    return true;
  }
#ifdef STREAM_TAGS
#ifdef SIMPLE_LINK
  if (!is_stream_tag(str[0])) {
    ++parse_err.cnt;
    return true;
  }
#else
  if (is_stream_tag(str[0]))
#endif
  {
    if (!chk_stream_tags(str[0], str[len-1], len))
      return true;
    str += 1;
    len -= 2;
  }
#endif
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

static bool cli_process()
{
  size_t const len = cli_buff_data_sz;
  const char  *str = (const char*)cli_buff;
  const char  *next = str, *end = str + len;
  bool done = true;

#ifdef UART_BEGIN
  const char* begin = (const char*)memchr(str, UART_BEGIN, len);
  if (begin && begin != str)
    ++parse_err.cnt;
#else
  const char* begin = str;
#endif
  while (begin && begin < end)
  {
    char* const tail = (char*)memchr(begin, UART_END, len - (begin - str));
    if (!tail)
      break;
    BUG_ON(tail >= end);
#ifdef UART_BEGIN
    begin += 1;
    char* next_begin;
    for (;;) {
      next_begin = (char*)memchr(begin, UART_BEGIN, len - (begin - str));
      if (next_begin && next_begin < tail) {
        begin = next_begin + 1;
        ++parse_err.cnt;
      } else
        break;
    }
#endif
    *tail = '\0';
    if (!process_msg(begin, tail - begin)) {
      done = false;
      break;
    }
    next = tail + 1;
#ifdef UART_BEGIN
    begin = next_begin;
    if (begin && begin != next)
      ++parse_err.cnt;
#else
    begin = next;
#endif
  }
  memmove(cli_buff, next, cli_buff_data_sz = end - next);
  return done;
}

#ifdef STATUS_REPORT_INTERVAL
static inline void report_idle()
{
  uart_begin();
  if (advertising_enabled)
    uart_print(":I " VMAJOR "." VMINOR "-");
  else
    uart_print(":Ih " VMAJOR "." VMINOR "-");
  uart_print(MAX_FRAME);
  uart_print("-" VARIANT);
  uart_end();
}

static inline void report_connected()
{
  uart_begin();
  if (advertising_enabled)
    uart_print(":D");
  else
    uart_print(":Dh");
  uart_end();
}
#endif

static void monitor_peers()
{
  for (unsigned i = 0; i < MAX_PEERS; ++i)
    if (peers[i] && !peers[i]->monitor())
      break;

#ifdef STATUS_REPORT_INTERVAL
  uint32_t const now = millis();
  if (is_idle()) {
    if (!last_status_ts || elapsed(last_status_ts, now) >= STATUS_REPORT_INTERVAL) {
      last_status_ts = now;
      report_idle();
    }
  } else if (is_connected() && !is_congested) {
    if (elapsed(last_status_ts, now) >= STATUS_REPORT_INTERVAL) {
      last_status_ts = now;
      report_connected();
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
    msg += dev_addr;
    msg += "#";
    msg += dev_name;
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

static void show_conn_status(bool congested)
{
#ifdef NEO_PIXEL_PIN
  neopix_conn_up(get_connect_status_ex(congested));
#elif defined(CONNECTED_LED)
  digitalWrite(CONNECTED_LED, get_connected_indicator() ? CONNECTED_LED_LVL : !(CONNECTED_LED_LVL));
#endif
}

static unsigned chk_errors()
{
  unsigned err_cnt = chk_error_flag(&unknown_data_src, "-got data from unknown source")
    + chk_error_cnt(&rx_queue_full, "-rx queue full ")
    + chk_error_cnt(&write_err,     "-write failed ")
    + chk_error_cnt(&notify_err,    "-notify failed ")
    + chk_error_cnt(&parse_err,     "-parse error ")
    + chk_error_cnt(&lost_frames,   "-serial frame lost ")
    ;
  for (unsigned i = 0; i < MAX_PEERS; ++i)
    if (peers[i])
      err_cnt += peers[i]->chk_errors();
  return err_cnt;
}

void loop()
{
  bool const received = cli_receive();
  bool const was_congested = is_congested;
  if (received || is_congested)
    is_congested = !cli_process();

  if (advertising_enabled && start_advertising && elapsed(centr_disconn_ts, millis()) > 100) {
    debug_msg("-start advertising");
    BLEDevice::startAdvertising(); // restart advertising
    start_advertising = false;
  }

#ifdef TELL_UPTIME
  if (advertising_enabled)
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

  unsigned const err_cnt = chk_errors();
  show_conn_status(was_congested || is_congested || err_cnt);

  if (!is_congested)
    esp_task_wdt_reset();
#ifdef UART_THROTTLE
  else
    delay(CONGESTION_DELAY);
#endif
}
