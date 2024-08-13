/*
 This is the dual role BLE device capable of connecting to multiple peripheral devices.
 It was designed as multipurpose BLE to serial adapter accepting commands from controlling host via serial link.
 The primary use case is gathering telemetry data from transmitters and providing communication link for other
 central for commands / responses. The controlling host uses the following protocol:

 Commands:
  '#C addr0 addr1 ..' - connect to peers with given addresses (up to 8)
  '#R'                - reset to idle state
 Commands will be disabled if AUTOCONNECT is defined

 Status messages:
  ':I rev.vmaj.vmin' - idle, not connected
  ':Cn'      - connecting to the n-th peer
  ':D'       - all peers connected, data receiving
  Status messages will be disabled if STATUS_REPORT_INTERVAL is undefined

 Debug messages:
  '-message'

 Every in/out message on physical UART is started with '\1' end with '\0'. 
 In case USB VCP is used for communications there is no start symbol, end symbol is always '\n'

 Second symbol of out message is
  ':' for status messages
  '-' for debug messages
  '<' if message is received from connected central
  '0', '1' .. '7'  for data received from peer 0, 1, .. 7
 The first data stream message after connection / re-connection has no data payload. This is the stream start token.

 The second symbol for input message is
  '#' for commands
  '>' for message to be sent to connected central
  '0', '1' .. '7'  for data to be sent to peer 0, 1, .. 7

 The maximum size of data in single message is MAX_CHUNK = 512.
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

// Version info printed as part of idle status message
#define REVISION  "1"
#define VMAJOR    "1"
#define VMINOR    "0"

// Device name (may be followed by unique suffix)
#define DEV_NAME  "Mx-"

// If defined the unique suffix based on MAC is added to device name to make it distinguishable
#define DEV_NAME_SUFF_LEN  6

// Connected LED pin, active low
#define CONNECTED_LED 8

#define SERVICE_UUID           "FFE0"
#define CHARACTERISTIC_UUID_TX "FFE1"

// Watchdog timeout. It will restart esp32 if some operation will hung.
#define WDT_TIMEOUT            20000 // msec

// If defined the status messages will be output periodically
#define STATUS_REPORT_INTERVAL 1000  // msec

// If UART_TX_PIN is defined the hardware serial port will be used for communications.
// Otherwise the USB virtual serial port will be utilized.
#define UART_TX_PIN  7
#define UART_RX_PIN  6
#define UART_BAUD_RATE 115200
#define UART_MODE SERIAL_8N1
// If defined the flow control on UART will be configured
// CTS prevents overflow of the host receiving buffer. Use it when
// you have USB serial adapter at the host side. They typically have
// buffer capacity of only 128 bytes.
#define UART_CTS_PIN 5
// RTS prevents overflow of the esp32 receiving buffer.
// #define UART_RTS_PIN 4

#ifdef UART_TX_PIN
// Using hardware UART
#define DataSerial Serial1
#define DATA_UART_NUM UART_NUM_1
#define UART_BEGIN '\1'
#define UART_END   '\0'
#else
// Using USB CDC
#define DataSerial Serial
#define UART_END   '\n'
#endif

// There is no flow control in USB serial port.
// The default buffer size is 256 bytes which may be not enough.
#define UART_BUFFER_SZ 4096

// If defined reset itself on peer disconnection instead of reconnecting
#define RESET_ON_DISCONNECT

// Undefine to keep default power level
#define TX_PW_BOOST ESP_PWR_LVL_P21

// If AUTOCONNECT is defined it will connect on startup to the predefined set of peers
// #define AUTOCONNECT

#ifdef AUTOCONNECT
// Peer device address to connect to
//#define PEER_ADDR    "EC:DA:3B:BB:CE:02"
//#define PEER_ADDR1   "34:B7:DA:F6:44:B2"
#define PEER_ADDR2   "D8:3B:DA:13:0F:7A"
#define PEER_ADDR3   "34:B7:DA:FB:58:E2"
#endif

// Broadcast uptime every second to connected central (for testing)
// #define TELL_UPTIME

// The maximum size of the message data.
// Larger amount of data should be split onto chunks before sending
// them to the adapter.
#define MAX_CHUNK 512

// If defined echo all data received from peer back to it (for testing)
#define PEER_ECHO

#ifdef PEER_ECHO
#define PEER_ECHO_QUEUE 16
#endif

static BLEServer*         pServer;
static BLECharacteristic* pCharacteristic;

#define MAX_PEERS 8
class Peer;
static Peer*    peers[MAX_PEERS];
static unsigned npeers;
static int      connected_peers;
static int      connected_centrals;

static bool     start_advertising = true;
static bool     centr_disconnected = true;
static uint32_t centr_disconn_ts;

#ifdef STATUS_REPORT_INTERVAL
static uint32_t last_status_ts;
#endif

static String   dev_name(DEV_NAME);
static String   rx_buff;

#ifdef TELL_UPTIME
static uint32_t last_uptime;
#endif

static inline void uart_begin()
{
#ifdef UART_BEGIN
  DataSerial.print(UART_BEGIN);
#endif
}

static inline bool is_idle()
{
  return !connected_peers;
}

static inline bool is_connected()
{
  return !is_idle() && connected_peers >= npeers;
}

static inline void uart_end()
{
  DataSerial.print(UART_END);
}

static inline uint32_t elapsed(uint32_t from, uint32_t to)
{
  return to < from ? 0 : to - from;
}

static void fatal(const char* what);

#ifdef PEER_ECHO
struct data_chunk {
  uint8_t* data;
  size_t len;
};
#endif

class Peer : public BLEClientCallbacks
{
public:
  Peer(unsigned idx, String const& addr)
    : m_idx(idx)
    , m_addr(addr)
    , m_writable(false)
    , m_connected(false)
    , m_disconn_ts(0)
    , m_Client(nullptr)
    , m_remoteCharacteristic(nullptr)
#ifdef PEER_ECHO
    , m_echo_queue(0)
#endif
  {
  }

  void onConnect(BLEClient *pclient) {
    uart_begin();
    DataSerial.print("-peer [");
    DataSerial.print(m_idx);
    DataSerial.print("] ");
    DataSerial.print(m_addr);
    DataSerial.print(" connected");
    uart_end();
    m_connected = true;
    notify_connected();
    connected_peers++;
  }

  void onDisconnect(BLEClient *pclient) {
    uart_begin();
    DataSerial.print("-peer [");
    DataSerial.print(m_idx);
    DataSerial.print("] ");
    DataSerial.print(m_addr);
    DataSerial.print(" disconnected");
    uart_end();
    m_connected = false;
    m_disconn_ts = millis();
    --connected_peers;
#ifdef RESET_ON_DISCONNECT
    fatal("Peer disconnected");
#endif
  }

  void report_connecting() {
#ifdef STATUS_REPORT_INTERVAL
    uart_begin();
    DataSerial.print(":C");
    DataSerial.print(m_idx);
    uart_end();
#endif
  }

  void notify_connected() {
    uart_begin();
    DataSerial.print(m_idx);
    uart_end();
  }

  void connect();

  void transmit(const char* data, size_t len) {
    if (!m_writable) {
      fatal("Peer is not writable");
      return;
    }
    if (len > MAX_CHUNK) {
      fatal("Data size exceeds limit");
      return;
    }
    m_remoteCharacteristic->writeValue((uint8_t*)data, len);
    taskYIELD();
  }

  bool notify_data(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length)
  {
    if (pBLERemoteCharacteristic != m_remoteCharacteristic)
      return false;

    uart_begin();
    DataSerial.print(m_idx);
    DataSerial.write(pData, length);
    uart_end();

#ifdef PEER_ECHO
    if (m_writable && is_connected()) {
      struct data_chunk ch = {.data = (uint8_t*)malloc(length), .len = length};
      if (!ch.data)
        fatal("No memory");
      memcpy(ch.data, pData, length);
      if (!xQueueSend(m_echo_queue, &ch, 0)) {
        free(ch.data);
        uart_begin();
        DataSerial.print("-echo queue full");
        uart_end();
      }
    }
#endif
    return true;
  }

  bool monitor()
  {
    uint32_t const now = millis();
#ifdef PEER_ECHO
    struct data_chunk ch;
    while (m_echo_queue && xQueueReceive(m_echo_queue, &ch, 0)) {
      if (m_connected) {
        m_remoteCharacteristic->writeValue(ch.data, ch.len);
        taskYIELD();
      }
      free(ch.data);
    }
#endif
    if (!m_connected && elapsed(m_disconn_ts, now) > 500) {
      connect();
      return true;
    }
    return false;
  }

  unsigned    m_idx;
  String      m_addr;
  bool        m_writable;
  bool        m_connected;
  uint32_t    m_disconn_ts;

  BLEClient*  m_Client;
  BLERemoteCharacteristic* m_remoteCharacteristic;

#ifdef PEER_ECHO
  QueueHandle_t m_echo_queue;
#endif
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      uart_begin();
      DataSerial.print("-central connected, ");
      DataSerial.print(esp_get_free_heap_size());
      DataSerial.print(" heap bytes avail");
      uart_end();
      ++connected_centrals;
    };

    void onDisconnect(BLEServer* pServer) {
      uart_begin();
      DataSerial.print("-central disconnected");
      uart_end();
      centr_disconn_ts = millis();
      centr_disconnected = true;
      start_advertising = true;
      --connected_centrals;
    }
};

class MyCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    if (centr_disconnected) {
      centr_disconnected = false;
      // Output stream start tag
      uart_begin();
      DataSerial.print('<');
      uart_end();
    }
    uart_begin();
    DataSerial.print('<');
    DataSerial.print(pCharacteristic->getValue());
    uart_end();
  }
};

static void reset_self()
{
  esp_restart();
}

static void fatal(const char* what)
{
  uart_begin();
  DataSerial.print("-fatal: ");
  DataSerial.print(what);
  uart_end();
  delay(100); // give host a chance to read message
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

static void add_peer(unsigned idx, String const& addr)
{
  if (idx >= MAX_PEERS) {
    fatal("Bad peer index");
    return;
  }
  if (peers[idx]) {
    fatal("Peer already exist");
    return;
  }
  peers[idx] = new Peer(idx, addr);
  ++npeers;
}

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
  BLEDevice::setMTU(MAX_CHUNK+3);
  setup_tx_power();
}

static void bt_device_start()
{
  // Create the BLE Server
  pServer = BLEDevice::createServer();

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_READ   |
    BLECharacteristic::PROPERTY_WRITE  |
    BLECharacteristic::PROPERTY_WRITE_NR
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
#ifdef UART_TX_PIN
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

  Serial.begin(115200);
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

  uart_begin();
  DataSerial.print("-data from unknown source");
  uart_end();
}

void Peer::connect()
{
  report_connecting();

  uint32_t const start = millis();

  uart_begin();
  DataSerial.print("-connecting to ");
  DataSerial.print(m_addr);
  uart_end();

  if (!m_Client) {
    m_Client = BLEDevice::createClient();
    m_Client->setClientCallbacks(this);
  }

  m_Client->connect(m_addr);
  m_Client->setMTU(MAX_CHUNK+3);  // Request increased MTU from server (default is 23 otherwise)

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
#ifdef PEER_ECHO
  if (m_writable)
    m_echo_queue = xQueueCreate(PEER_ECHO_QUEUE, sizeof(struct data_chunk));
#endif

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
}

static void transmit(const char* data, size_t len)
{
  if (len > MAX_CHUNK) {
    fatal("Data size exceeds limit");
    return;
  }
  pCharacteristic->setValue((uint8_t*)data, len);
  pCharacteristic->notify();
  taskYIELD();
}

static void process_write(unsigned idx, const char* str, size_t len)
{
  if (idx >= MAX_PEERS || !peers[idx]) {
    fatal("Bad peer index");
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

static void process_cmd(const char* cmd)
{
  switch (cmd[0]) {
    case 'R':
      reset_self();
      break;
    case 'C':
      cmd_connect(cmd + 1);
      break;
    default:
      fatal("Unrecognized command");
  }
}
#endif

static void process_msg(const char* str, size_t len)
{
  if (!len) {
    fatal("Invalid message");
    return;
  }
  switch (str[0]) {
#ifndef AUTOCONNECT
    case '#':
      process_cmd(str + 1);
      break;
#endif
    case '>':
      transmit(str + 1, len - 1);
      break;
    default:
      process_write(str[0] - '0', str + 1, len - 1);
  }
}

static void rx_process()
{
  const char* str = rx_buff.c_str();
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
  rx_buff = rx_buff.substring(next - str);
}

#ifdef STATUS_REPORT_INTERVAL
static inline void report_idle()
{
  uart_begin();
  DataSerial.print(":I " REVISION "." VMAJOR "." VMINOR);
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

void loop()
{
  uint32_t const now = millis();

  String const received = DataSerial.readString();
  if (received.length()) {
    rx_buff += received;
    rx_process();
  }

  if (start_advertising && elapsed(centr_disconn_ts, now) > 500) {
    uart_begin();
    DataSerial.print("-start advertising");
    uart_end();
    BLEDevice::startAdvertising(); // restart advertising
    start_advertising = false;
  }

#ifdef TELL_UPTIME
  uint32_t const uptime = now / 1000;
  if (uptime != last_uptime) {
    last_uptime = uptime;
    String msg(uptime);
    transmit(msg.c_str(), msg.length());
  }
#endif

  monitor_peers();
  esp_task_wdt_reset();
}
