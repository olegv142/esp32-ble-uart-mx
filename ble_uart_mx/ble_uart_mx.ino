/*
 This is the dual role BLE device capable of connecting to multiple peripheral devices.
 It was designed as multipurpose BLE to serial adapter accepting commands from controlling host via serial link.
 The primary use case is gathering telemetry data from transmitters and providing communication link for other
 central for commands / responses. The controlling host uses the following protocol:

 Commands:
  '#C addr0 addr1 ..' - connect to peripherals with given addresses (up to 8)
  '#R'                - reset to idle state
 Commands will be disabled if AUTOCONNECT is defined

 Status messages:
  ':I rev.vmaj.vmin' - idle, not connected
  ':Cn'      - connecting to the n-th peripheral
  ':D'       - all peripherals connected, data receiving
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
#include <esp_system.h>
#include <driver/uart.h>
#include <string.h>
#include <malloc.h>
#include <freertos/queue.h>

#include "mx_config.h"

#ifdef BINARY_DATA_SUPPORT
#include "mx_encoding.h"
#endif

#ifdef EXT_FRAMES
#include "checksum.h"
#include "xframe.h"
#endif

#ifndef HIDDEN
static BLECharacteristic* pCharacteristic;
#endif

#ifndef MAX_PEERS
#define MAX_PEERS 8
#endif

class Peer;
static Peer*    peers[MAX_PEERS];
static unsigned npeers;
static int      connected_peers;
static int      connected_centrals;

static bool     start_advertising = true;
static uint32_t centr_disconn_ts;

#ifdef STATUS_REPORT_INTERVAL
static uint32_t last_status_ts;
#endif

static String   dev_name(DEV_NAME);
static String   cli_buff;

static QueueHandle_t rx_queue;
static unsigned      queue_full_cnt;
static unsigned      queue_full_last;
static bool          unknown_data_src;

#ifdef TELL_UPTIME
static uint32_t last_uptime;
#endif

static inline void uart_begin()
{
#ifdef UART_BEGIN
  DataSerial.print(UART_BEGIN);
#endif
}

static inline void uart_end()
{
  DataSerial.print(UART_END);
}

static inline bool is_idle()
{
  return !connected_peers;
}

static inline bool is_connected()
{
  return !is_idle() && connected_peers >= npeers;
}

static inline uint32_t elapsed(uint32_t from, uint32_t to)
{
  return to < from ? 0 : to - from;
}

static inline void debug_msg(const char* msg)
{
#ifndef NO_DEBUG
  uart_begin();
  DataSerial.print(msg);
  uart_end();
#endif
}

static void fatal(const char* what);

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
      DataSerial.print("-invalid chunk size from [");
      DataSerial.print(m_tag);
      DataSerial.print("]");
      uart_end();
#endif
      goto skip;
    }
    if (!(h & XH_FIRST)) {
      if (m_last_chunk < 0)
        goto skip_verbose;
      if (m_next_sn != (h & XH_SN_MASK))
        goto skip_verbose;
      if (m_last_chunk + 2 >= MAX_CHUNKS && !(h & XH_LAST))
        goto skip_verbose;
    }
    if (!chksum_validate(chunk->data, chunk->len - CHKSUM_SIZE, &chksum)) {
#ifndef NO_DEBUG
      uart_begin();
      DataSerial.print("-invalid checksum from [");
      DataSerial.print(m_tag);
      DataSerial.print("]");
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
      DataSerial.print("-skip chunk from [");
      DataSerial.print(m_tag);
      DataSerial.print("]");
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
    uint8_t enc_buff[MAX_ENCODED_CHUNK_LEN];
    uart_begin();
    DataSerial.print(m_tag);
    if (is_binary)
      DataSerial.print(ENCODED_DATA_START_TAG);
    for (int i = 0; i <= m_last_chunk; ++i) {
      size_t len = m_chunks[i].len - XHDR_SIZE - CHKSUM_SIZE;
      uint8_t * pchunk = m_chunks[i].data + XHDR_SIZE;
      if (is_binary) {
        len = encode(pchunk, len, enc_buff);
        pchunk = enc_buff;
      }
      DataSerial.write(pchunk, len);
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
static uint8_t        centr_tx_sn;
#endif

#ifdef BINARY_DATA_SUPPORT
static uint8_t        tx_buff[MAX_FRAME];
#endif

#ifndef EXT_FRAMES
static void uart_print_data(uint8_t const* data, size_t len, char tag)
{
  if (len > MAX_CHUNK) {
    fatal("Data size exceeds limit");
    return;
  }
  uint8_t const * out_data = data;
#ifdef BINARY_DATA_SUPPORT
  uint8_t enc_buff[1+MAX_ENCODED_CHUNK_LEN] = {ENCODED_DATA_START_TAG};
  if (is_data_binary(data, len)) {
    len = 1 + encode(data, len, enc_buff + 1);
    out_data = enc_buff;
  }
#endif
  uart_begin();
  DataSerial.print(tag);
  DataSerial.write(out_data, len);
  uart_end();
}
#endif

class Peer : public BLEClientCallbacks
{
public:
  Peer(unsigned idx, String const& addr)
    : m_tag('0' + idx)
    , m_addr(addr)
    , m_writable(false)
    , m_connected(false)
    , m_was_connected(false)
    , m_disconn_ts(0)
    , m_Client(nullptr)
    , m_remoteCharacteristic(nullptr)
    , m_tx_sn(0)
    , m_rx_queue(0)
    , m_queue_full_cnt(0)
    , m_queue_full_last(0)
#ifdef EXT_FRAMES
    , m_xrx('0' + idx)
#endif
  {
  }

  void onConnect(BLEClient *pclient) {
    // post connected event to receive queue
    struct data_chunk ch = {.data = nullptr, .len = 0};
    if (!xQueueSend(m_rx_queue, &ch, 0))
      ++m_queue_full_cnt;
    m_connected = true;
    ++connected_peers;
  }

  void onDisconnect(BLEClient *pclient) {
#ifdef RESET_ON_DISCONNECT
    fatal("Peer disconnected");
#endif
    m_disconn_ts = millis();
    m_connected = false;
    --connected_peers;
  }

  void report_connecting() {
#ifdef STATUS_REPORT_INTERVAL
    uart_begin();
    DataSerial.print(":C");
    DataSerial.print(m_tag);
    uart_end();
#endif
  }

  void notify_connected() {
    uart_begin();
    DataSerial.print(m_tag);
    uart_end();
  }

  void connect();

  void transmit(const char* data, size_t len)
  {
    if (!m_writable) {
      fatal("Peer is not writable");
      return;
    }
    uint8_t* tx_data = (uint8_t*)data;
  #ifdef BINARY_DATA_SUPPORT
    uint8_t binary = 0;
    if (len && (binary = (data[0] == ENCODED_DATA_START_TAG))) {
      if (len > 1 + MAX_ENCODED_FRAME_LEN) {
        fatal("Encoded data size exceeds limit");
        return;
      }
      len = decode(data + 1, len - 1, tx_data = tx_buff);
    }
  #endif
    if (!len) {
      fatal("No data to transmit");
      return;
    }
    if (len > MAX_FRAME) {
      fatal("Data size exceeds limit");
      return;
    }
  #ifdef EXT_FRAMES
    uint8_t first = 1, last;
    uint32_t chksum = CHKSUM_INI;
  #endif
    while (len) {
      size_t chunk = len;
      uint8_t* pdata = tx_data;
  #ifdef EXT_FRAMES
      if (!(last = (chunk <= MAX_CHUNK)))
        chunk = MAX_CHUNK;
      uint8_t chunk_buff[MAX_SIZE] = {mk_xframe_hdr(++m_tx_sn, binary, first, last)};
      chksum = chksum_up(chunk_buff[0], chksum);
      chksum = chksum_copy(tx_data, chunk, chunk_buff + 1, chksum);
      pdata = chunk_buff;
      first = 0;
  #endif
      m_remoteCharacteristic->writeValue(pdata, XHDR_SIZE + chunk + CHKSUM_SIZE);
      tx_data += chunk;
      len -= chunk;
      taskYIELD();
    }
  }

  bool notify_data(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length)
  {
    if (pBLERemoteCharacteristic != m_remoteCharacteristic)
      return false;

    struct data_chunk ch = {.data = (uint8_t*)malloc(length), .len = length};
    if (!ch.data)
      fatal("No memory");
    memcpy(ch.data, pData, length);
    if (!xQueueSend(m_rx_queue, &ch, 0)) {
      free(ch.data);
      ++m_queue_full_cnt;
    }
    return true;
  }

  void receive(struct data_chunk const* chunk)
  {
#ifdef ECHO
    if (m_writable && is_connected()) {
      m_remoteCharacteristic->writeValue(chunk->data, chunk->len);
      taskYIELD();
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
    if (m_connected != m_was_connected) {
      m_was_connected = m_connected;
#ifndef NO_DEBUG
      if (m_connected) {
        uart_begin();
        DataSerial.print("-peripheral [");
        DataSerial.print(m_tag);
        DataSerial.print("] ");
        DataSerial.print(m_addr);
        DataSerial.print(" connected");
        uart_end();
      } else {
        uart_begin();
        DataSerial.print("-peripheral [");
        DataSerial.print(m_tag);
        DataSerial.print("] ");
        DataSerial.print(m_addr);
        DataSerial.print(" disconnected");
        uart_end();
      }
#endif
    }
    if (m_queue_full_cnt != m_queue_full_last) {
#ifndef NO_DEBUG
      uart_begin();
      DataSerial.print("-rx queue [");
      DataSerial.print(m_tag);
      DataSerial.print("] full ");
      DataSerial.print(m_queue_full_cnt - m_queue_full_last);
      DataSerial.print(" times");
      uart_end();
#endif
      m_queue_full_last = m_queue_full_cnt;
    }
    if (!m_connected && elapsed(m_disconn_ts, millis()) > 500) {
      connect();
      return true;
    }
    return false;
  }

  char const  m_tag;
  String      m_addr;
  bool        m_writable;
  bool        m_connected;
  bool        m_was_connected;
  uint32_t    m_disconn_ts;

  BLEClient*  m_Client;
  BLERemoteCharacteristic* m_remoteCharacteristic;

  uint8_t       m_tx_sn;
  QueueHandle_t m_rx_queue;
  unsigned      m_queue_full_cnt;
  unsigned      m_queue_full_last;
#ifdef EXT_FRAMES
  XFrameReceiver m_xrx;
#endif
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      struct data_chunk ch = {.data = nullptr, .len = 0};
      if (!xQueueSend(rx_queue, &ch, 0))
        ++queue_full_cnt;
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
    uint8_t const * const pData = pCharacteristic->getData();
    struct data_chunk ch = {.data = (uint8_t*)malloc(length), .len = length};
    if (!ch.data)
      fatal("No memory");
    memcpy(ch.data, pData, length);
    if (!xQueueSend(rx_queue, &ch, 0)) {
      free(ch.data);
      ++queue_full_cnt;
    }
  }
};

static void reset_self()
{
  esp_restart();
}

static void fatal(const char* what)
{
#ifndef NO_DEBUG
  uart_begin();
  DataSerial.print("-fatal: ");
  DataSerial.print(what);
  uart_end();
  delay(100); // give host a chance to read message
#endif
  reset_self();
}

void esp_task_wdt_isr_user_handler(void)
{
  reset_self();
}

static inline void watchdog_init()
{
  esp_task_wdt_config_t wdt_cfg = {.timeout_ms = WDT_TIMEOUT, .idle_core_mask = (1<<portNUM_PROCESSORS)-1, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt_cfg); // enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);             // add current thread to WDT watch
}

#ifndef PASSIVE_ONLY
static void add_peer(unsigned idx, String const& addr)
{
  if (idx >= MAX_PEERS) {
    debug_msg("-bad peripheral index");
    return;
  }
  if (peers[idx]) {
    debug_msg("-peer already exist");
    return;
  }
  peers[idx] = new Peer(idx, addr);
  ++npeers;
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

static void bt_device_init()
{
  // Create the BLE Device
  init_dev_name();
  BLEDevice::init(dev_name);
  BLEDevice::setMTU(MAX_SIZE+3);
  setup_tx_power();
  rx_queue = xQueueCreate(RX_QUEUE, sizeof(struct data_chunk));
  if (!rx_queue)
    fatal("No memory");
}

static void bt_device_start()
{
#ifndef HIDDEN
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
#endif
}

static void hw_init()
{
#ifdef CONNECTED_LED
  pinMode(CONNECTED_LED, OUTPUT);
  digitalWrite(CONNECTED_LED, HIGH);
#endif

#ifdef UART_BUFFER_SZ
  DataSerial.setRxBufferSize(UART_BUFFER_SZ);
#endif
#ifdef HW_UART
  DataSerial.begin(UART_BAUD_RATE, UART_MODE, UART_RX_PIN, UART_TX_PIN);
#if defined(UART_CTS_PIN) && defined(UART_RTS_PIN)
  uart_set_pin(DATA_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_RTS_PIN, UART_CTS_PIN);  
  uart_set_hw_flow_ctrl(DATA_UART_NUM, UART_HW_FLOWCTRL_CTS_RTS, UART_FIFO_LEN/2);
#else
#ifdef UART_CTS_PIN
  uart_set_pin(DATA_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_CTS_PIN);  
  uart_set_hw_flow_ctrl(DATA_UART_NUM, UART_HW_FLOWCTRL_CTS, UART_FIFO_LEN/2);
#endif
#ifdef UART_RTS_PIN
  uart_set_pin(DATA_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_RTS_PIN, UART_PIN_NO_CHANGE);
  uart_set_hw_flow_ctrl(DATA_UART_NUM, UART_HW_FLOWCTRL_RTS, UART_FIFO_LEN/2);
#endif
#endif
#endif
  DataSerial.setTimeout(10);

  Serial.begin(UART_BAUD_RATE);
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
  uart_begin();
  DataSerial.print("-connecting to ");
  DataSerial.print(m_addr);
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

  uint32_t const start = millis();
  m_Client->connect(m_addr);
  m_Client->setMTU(MAX_SIZE+3);  // Request increased MTU from server (default is 23 otherwise)

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = m_Client->getService(SERVICE_UUID);
  // Reset itself on error to avoid dealing with de-initialization
  if (!pRemoteService) {
    fatal("Failed to find our service UUID");
    return;
  }
  // Obtain a reference to the characteristic in the service of the remote BLE server.
  m_remoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID_TX);
  if (!m_remoteCharacteristic) {
    fatal("Failed to find our characteristic UUID");
    return;
  }
  // Subscribe to updates
  if (m_remoteCharacteristic->canNotify()) {
    m_remoteCharacteristic->registerForNotify(peerNotifyCallback);
  } else {
    fatal("Notification not supported by the server");
  }
  m_writable = m_remoteCharacteristic->canWrite();

#ifndef NO_DEBUG
  uart_begin();
  DataSerial.print("-connected to ");
  DataSerial.print(m_addr);
  DataSerial.print(" in ");
  DataSerial.print(millis() - start);
  DataSerial.print(" msec");
  if (m_writable)
    DataSerial.print(", writable");
#ifdef RESET_ON_DISCONNECT
  DataSerial.print(", rssi=");
  DataSerial.print(m_Client->getRssi());
#endif
  uart_end();
#endif
}

#ifndef HIDDEN
static void transmit_to_central(const char* data, size_t len)
{
  uint8_t* tx_data = (uint8_t*)data;
#ifdef BINARY_DATA_SUPPORT
  uint8_t binary = 0;
  if (len && (binary = (data[0] == ENCODED_DATA_START_TAG))) {
    if (len > 1 + MAX_ENCODED_FRAME_LEN) {
      fatal("Encoded data size exceeds limit");
      return;
    }
    len = decode(data + 1, len - 1, tx_data = tx_buff);
  }
#endif
  if (!len) {
    fatal("No data to transmit");
    return;
  }
  if (len > MAX_FRAME) {
    fatal("Data size exceeds limit");
    return;
  }
#ifdef EXT_FRAMES
  uint8_t first = 1, last;
  uint32_t chksum = CHKSUM_INI;
#endif
  while (len) {
    size_t chunk = len;
    uint8_t* pdata = tx_data;
#ifdef EXT_FRAMES
    if (!(last = (chunk <= MAX_CHUNK)))
      chunk = MAX_CHUNK;
    uint8_t chunk_buff[MAX_SIZE] = {mk_xframe_hdr(++centr_tx_sn, binary, first, last)};
    chksum = chksum_up(chunk_buff[0], chksum);
    chksum = chksum_copy(tx_data, chunk, chunk_buff + 1, chksum);
    pdata = chunk_buff;
    first = 0;
#endif
    pCharacteristic->setValue(pdata, XHDR_SIZE + chunk + CHKSUM_SIZE);
    pCharacteristic->notify();
    tx_data += chunk;
    len -= chunk;
    taskYIELD();
  }
}
#endif

static void transmit_to_peer(unsigned idx, const char* str, size_t len)
{
  if (idx >= MAX_PEERS || !peers[idx]) {
    debug_msg("-bad peripheral index");
    return;
  }
  peers[idx]->transmit(str, len);
}

#ifndef AUTOCONNECT
static void cmd_connect(const char* param)
{
  String params(param);
  const char* ptr = param;
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

static void process_cmd(const char* cmd)
{
  switch (cmd[0]) {
    case 'R':
      reset_self();
      break;
#ifndef AUTOCONNECT
    case 'C':
      cmd_connect(cmd + 1);
      break;
#endif
    default:
      fatal("Unrecognized command");
  }
}

static void process_msg(const char* str, size_t len)
{
  if (!len) {
    fatal("Invalid message");
    return;
  }
  switch (str[0]) {
    case '#':
      process_cmd(str + 1);
      break;
#ifndef HIDDEN
    case '>':
      transmit_to_central(str + 1, len - 1);
      break;
#endif
    default:
      transmit_to_peer(str[0] - '0', str + 1, len - 1);
  }
}

static void cli_process()
{
  const char* str = cli_buff.c_str();
  const char* next = str;
  for (;;) {
#ifdef UART_BEGIN
    const char* begin = strchr(next, UART_BEGIN);
    if (!begin)
      break;
    begin += 1;
#else
    const char* begin = next;
#endif
    char* tail = strchr(begin, UART_END);
    if (!tail)
      break;
    *tail = '\0';
    next = tail + 1;
    process_msg(begin, tail - begin);
  }
  cli_buff = cli_buff.substring(next - str);
}

#ifdef STATUS_REPORT_INTERVAL
static inline void report_idle()
{
  uart_begin();
  DataSerial.print(":I " VMAJOR "." VMINOR "-");
  DataSerial.print(MAX_FRAME);
  DataSerial.print("-" VARIANT);
  uart_end();
}

static inline void report_connected()
{
  uart_begin();
  DataSerial.print(":D");  
  uart_end();
}
#endif

static void monitor_peers()
{
  for (unsigned i = 0; i < MAX_PEERS; ++i)
    if (peers[i] && peers[i]->monitor())
      break;

#ifdef STATUS_REPORT_INTERVAL
  uint32_t const now = millis();
  if (is_idle()) {
    if (!last_status_ts || elapsed(last_status_ts, now) >= STATUS_REPORT_INTERVAL) {
      last_status_ts = now;
      report_idle();
    }
  } else if (is_connected()) {
    if (elapsed(last_status_ts, now) >= STATUS_REPORT_INTERVAL) {
      last_status_ts = now;
      report_connected();
    }
  }
#endif
#ifdef CONNECTED_LED
  bool const conn_led = (is_idle() && connected_centrals) || is_connected();
  digitalWrite(CONNECTED_LED, conn_led ? LOW : HIGH);
#endif
}

void receive_from_central(struct data_chunk const* chunk)
{
#ifdef ECHO
  pCharacteristic->setValue(chunk->data, chunk->len);
  pCharacteristic->notify();
  taskYIELD();
#endif
#ifdef EXT_FRAMES
  centr_xrx.receive(chunk);
#else
  uart_print_data(chunk->data, chunk->len, '<');
  free(chunk->data);
#endif
}

void loop()
{
  String const received = DataSerial.readString();
  if (received.length()) {
    cli_buff += received;
    cli_process();
  }

#ifndef HIDDEN
  if (start_advertising && elapsed(centr_disconn_ts, millis()) > 500) {
    debug_msg("-start advertising");
    BLEDevice::startAdvertising(); // restart advertising
    start_advertising = false;
  }
#endif

#ifdef TELL_UPTIME
  uint32_t const uptime = millis() / 1000;
  if (uptime != last_uptime) {
    last_uptime = uptime;
    String msg(uptime);
    transmit_to_central(msg.c_str(), msg.length());
  }
#endif

  struct data_chunk ch;
  while (xQueueReceive(rx_queue, &ch, 0)) {
    if (ch.data) {
      receive_from_central(&ch);
    } else {
      // Output stream start tag
      uart_begin();
      DataSerial.print('<');
      uart_end();
#ifdef EXT_FRAMES
      centr_xrx.reset();
#endif
    }
  }

  if (queue_full_cnt != queue_full_last) {
#ifndef NO_DEBUG
    uart_begin();
    DataSerial.print("-rx queue full ");
    DataSerial.print(queue_full_cnt - queue_full_last);
    DataSerial.print(" times");
    uart_end();
#endif
    queue_full_last = queue_full_cnt;
  }

  if (unknown_data_src) {
    unknown_data_src = false;
    debug_msg("-got data from unknown source");
  }

  monitor_peers();
  esp_task_wdt_reset();
}
