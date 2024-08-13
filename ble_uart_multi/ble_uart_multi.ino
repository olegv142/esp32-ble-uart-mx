/*
 This is the dual role BLE device capable of connecting to multiple peers. Its based on the ble_uart_dual example.
 It was designed as multipurpose BLE to serial adapter accepting commands from controlling host via serial link.
 The primary use case is gathering telemetry data from transmitters and providing communication link for other
 central for commands / responses. The controlling host uses the following protocol:

 Commands:
  '#C addr0 addr1 ..' - connect to peers with given addresses (up to 8)
  '#R'                - reset to idle state

 Status messages:
  ':I rev.vmaj.vmin' - idle, not connected
  ':Cn'      - connecting to the n-th peer
  ':D'       - all peers connected, data receiving

 Debug messages:
  '-message'

 Every in/out message on physical UART is started with '\1' end with '\0'. 
 In case USB VCP is used for communications there is no start symbol, end symbol is always '\n'

 Second symbol of out message is
  ':' for status messages
  '-' for debug messages
  '<' if message is received from connected central
  '0', '1' .. '7'  for data received from peer 0, 1, .. 7
 The first data stream message after peer connection / re-connection has no data payload. This is the stream start token.

 The second symbol for input message is
  '#' for commands
  '>' for message to be sent to connected central
  '0', '1' .. '7'  for data to be sent to peer 0, 1, .. 7

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

#define REVISION  "1"
#define VMAJOR    "1"
#define VMINOR    "0"

#define DEV_NAME  "Mx-"

// Uncomment to add suffix based on MAC to device name to make it distinguishable
#define DEV_NAME_SUFF_LEN  6

#define SERVICE_UUID           "FFE0"
#define CHARACTERISTIC_UUID_TX "FFE1"

#define WDT_TIMEOUT            20000 // msec
#define STATUS_REPORT_INTERVAL 1000  // msec

// If UART_TX_PIN is defined the data will be output to the hardware serial port
// Otherwise the USB virtual serial port will be used for that purpose
#define UART_TX_PIN  7
#define UART_RX_PIN  6
#define UART_BAUD_RATE 115200
#define UART_MODE SERIAL_8N1
// If defined the flow control on UART will be configured
#define UART_CTS_PIN 5

#ifdef UART_TX_PIN
#define DataSerial Serial1
#define DATA_UART_NUM UART_NUM_1
#define UART_BEGIN '\1'
#define UART_END   '\0'
#else
#define DataSerial Serial
#define UART_END   '\n'
#endif

// There is no flow control in USB serial port.
// The default buffer size is 256 bytes which may be not enough.
#define UART_BUFFER_SZ 4096

// Connected LED pin, active low
#define CONNECTED_LED 8

/*
 The BT stack is complex and not well tested bunch of software. Using it one can easily be trapped onto the state
 where there is no way out. The biggest problem is that connect routine may hung forever. Although the connect call
 has timeout parameter, it does not help. The call may complete on timeout without errors, but the connection will
 not actually be established. That's why we are using watchdog to detect connection timeout. Its unclear if soft
 reset by watchdog is equivalent to the power cycle or reset by pulling low EN pin. That's why there is an option
 to implement hard reset on connect timeout by hard wiring some output pin to EN input of the chip.
*/
// If defined use hard reset on connect timeout
// #define RST_OUT_PIN 3

// If defined reset itself on peer disconnection instead of reconnecting
#define RESET_ON_DISCONNECT

// Undefine to keep default power level
#define TX_PW_BOOST ESP_PWR_LVL_P21

// If TEST is defined it will connect on startup to the predefined set of peers
// and broadcast uptime every second to connected central
// #define TEST

#ifdef TEST
// Peer device address to connect to
//#define PEER_ADDR    "EC:DA:3B:BB:CE:02"
//#define PEER_ADDR1   "34:B7:DA:F6:44:B2"
#define PEER_ADDR2   "D8:3B:DA:13:0F:7A"
#define PEER_ADDR3   "34:B7:DA:FB:58:E2"
#endif

// If USE_SEQ_TAG is defined every chunk of data transmitted (characteristic update) will carry sequence tag as the first symbol.
// It will get its value from 16 characters sequence 'a', 'b', .. 'p'. Next update will use next symbol. After 'p' the 'a' will
// be used again. The receiver may use sequence tag to control the order of delivery of updates detecting missed or reordered
// updates.
// #define USE_SEQ_TAG

#ifdef USE_SEQ_TAG
#define NTAGS 16
#define FIRST_TAG 'a'
static char next_tag = FIRST_TAG;
#endif

#define WRITE_RESPONSE false

// If defined echo all data received from peer back to it
// #define PEER_ECHO

#ifdef PEER_ECHO
#define PEER_ECHO_QUEUE 64
#endif

static BLEServer*         pServer;
static BLECharacteristic* pCharacteristic;

#define MAX_PEERS 8
class Peer;
static Peer*    peers[MAX_PEERS];
static unsigned npeers;
static int      connected_peers;

static bool     start_advertising = true;
static bool     centr_disconnected = true;
static uint32_t centr_disconn_ts;
static uint32_t last_status_ts;

static String   dev_name(DEV_NAME);
static String   tx_buff;
static String   rx_buff;

#ifdef TEST
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
    tx_buff_reset();
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
    tx_buff_reset();
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
    fatal("peer disconnected");
#endif
  }

  void report_connecting() {
    uart_begin();
    DataSerial.print(":C");
    DataSerial.print(m_idx);
    uart_end();
  }

  void notify_connected() {
    uart_begin();
    DataSerial.print(m_idx);
    uart_end();
  }

  void connect();

  void transmit(const char* str) {
    if (!m_writable) {
      fatal("Peer is not writable");
      return;
    }
    m_tx_buff += str;
    tx_flush();
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
    if (m_writable) {
      struct data_chunk ch = {.data = (uint8_t*)malloc(length), .len = length};
      memcpy(ch.data, pData, length);
      if (!xQueueSend(m_echo_queue, &ch, 0)) {
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
    if (m_echo_queue && xQueueReceive(m_echo_queue, &ch, 0)) {
      if (m_connected)
        m_remoteCharacteristic->writeValue(ch.data, ch.len, WRITE_RESPONSE);
      free(ch.data);
    }
#endif
    if (!m_connected && elapsed(m_disconn_ts, now) > 500) {
      connect();
      return true;
    }
    return false;
  }

  void tx_buff_reset()
  {
  #ifdef USE_SEQ_TAG
        m_tx_buff = ' ';
  #else
        m_tx_buff = "";
  #endif
  }

  void tx_flush();

  unsigned    m_idx;
  String      m_addr;
  bool        m_writable;
  bool        m_connected;
  uint32_t    m_disconn_ts;

  BLEClient*  m_Client;
  BLERemoteCharacteristic* m_remoteCharacteristic;
  String      m_tx_buff;

#ifdef PEER_ECHO
  QueueHandle_t m_echo_queue;
#endif
};

static void tx_flush();

static inline void tx_buff_reset()
{
#ifdef USE_SEQ_TAG
      tx_buff = ' ';
#else
      tx_buff = "";
#endif
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      uart_begin();
      DataSerial.print("-central connected, ");
      DataSerial.print(esp_get_free_heap_size());
      DataSerial.print(" heap bytes avail");
      uart_end();
      tx_buff_reset();
    };

    void onDisconnect(BLEServer* pServer) {
      uart_begin();
      DataSerial.print("-central disconnected");
      uart_end();
      centr_disconn_ts = millis();
      centr_disconnected = true;
      start_advertising = true;
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
#ifdef RST_OUT_PIN
  pinMode(RST_OUT_PIN, OUTPUT);
  digitalWrite(RST_OUT_PIN, LOW);
#else
  esp_restart();
#endif
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
  BLEDevice::setMTU(247);
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
  Serial.begin(115200);

#ifdef CONNECTED_LED
  pinMode(CONNECTED_LED, OUTPUT);
  digitalWrite(CONNECTED_LED, HIGH);
#endif

#ifdef UART_TX_PIN
  DataSerial.begin(UART_BAUD_RATE, UART_MODE, UART_RX_PIN, UART_TX_PIN);
#ifdef UART_CTS_PIN
  uart_set_pin(DATA_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_CTS_PIN);  
  uart_set_hw_flow_ctrl(DATA_UART_NUM, UART_HW_FLOWCTRL_CTS, 0);
#endif
#endif
#ifdef UART_BUFFER_SZ
  DataSerial.setRxBufferSize(UART_BUFFER_SZ);
#endif
  DataSerial.setTimeout(10);

  tx_buff_reset();
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
  m_Client->setMTU(247);  // Request increased MTU from server (default is 23 otherwise)

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

static inline uint16_t max_tx_chunk()
{
  return BLEDevice::getMTU() - 3;
}

static void tx_flush()
{
  uint16_t max_chunk = max_tx_chunk();
  uint8_t* pdata = (uint8_t*)tx_buff.c_str();
  unsigned data_len = tx_buff.length(), sent = 0;
#ifdef USE_SEQ_TAG
  max_chunk -= 1;
  data_len -= 1;
  pdata += 1;
#endif
  while (data_len) {
    unsigned const chunk = data_len > max_chunk ? max_chunk : data_len;
#ifdef USE_SEQ_TAG
    pdata[-1] = next_tag;
    if (++next_tag >= FIRST_TAG + NTAGS)
      next_tag = FIRST_TAG;
    pCharacteristic->setValue(pdata - 1, chunk + 1);
#else
    pCharacteristic->setValue(pdata, chunk);
#endif
    pCharacteristic->notify();
    sent     += chunk;
    pdata    += chunk;
    data_len -= chunk;
  }
  tx_buff = tx_buff.substring(sent);
}

void Peer::tx_flush()
{
  uint16_t max_chunk = max_tx_chunk();
  uint8_t* pdata = (uint8_t*)m_tx_buff.c_str();
  unsigned data_len = m_tx_buff.length(), sent = 0;
#ifdef USE_SEQ_TAG
  max_chunk -= 1;
  data_len -= 1;
  pdata += 1;
#endif
  while (data_len) {
    unsigned const chunk = data_len > max_chunk ? max_chunk : data_len;
#ifdef USE_SEQ_TAG
    pdata[-1] = next_tag;
    if (++next_tag >= FIRST_TAG + NTAGS)
      next_tag = FIRST_TAG;
    m_remoteCharacteristic->writeValue(pdata - 1, chunk + 1, WRITE_RESPONSE);
#else
    m_remoteCharacteristic->writeValue(pdata, chunk, WRITE_RESPONSE);
#endif
    pCharacteristic->notify();
    sent     += chunk;
    pdata    += chunk;
    data_len -= chunk;
  }
  m_tx_buff = m_tx_buff.substring(sent);
}

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

static void process_write(unsigned idx, const char* str)
{
  if (idx >= MAX_PEERS || !peers[idx]) {
    fatal("Bad peer index");
    return;
  }
  peers[idx]->transmit(str);
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
      fatal("unrecognized command");
  }
}

static void process_msg(const char* str)
{
  switch (str[0]) {
    case '#':
      process_cmd(str + 1);
      break;
    case '>':
      tx_buff += str + 1;
      tx_flush();
      break;
    default:
      process_write(str[0] - '0', str + 1);
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
    process_msg(begin);
  }
  rx_buff = rx_buff.substring(next - str);
}

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

static void monitor_peers()
{
  for (unsigned i = 0; i < MAX_PEERS; ++i)
    if (peers[i] && peers[i]->monitor())
      break;

  bool connected = false;
  uint32_t const now = millis();
  if (!connected_peers) {
    if (!last_status_ts || elapsed(last_status_ts, now) >= STATUS_REPORT_INTERVAL) {
      last_status_ts = now;
      report_idle();
    }
  } else if (connected_peers >= npeers) {
    connected = true;
    if (elapsed(last_status_ts, now) >= STATUS_REPORT_INTERVAL) {
      last_status_ts = now;
      report_connected();
    }
  }
#ifdef CONNECTED_LED
  digitalWrite(CONNECTED_LED, connected ? LOW : HIGH);
#endif
}

void loop()
{
  uint32_t const now = millis();

#ifndef TEST
  String const received = DataSerial.readString();
  if (received.length()) {
    rx_buff += received;
    rx_process();
  }
#endif

  if (start_advertising && elapsed(centr_disconn_ts, now) > 500) {
    uart_begin();
    DataSerial.print("-start advertising");
    uart_end();
    BLEDevice::startAdvertising(); // restart advertising
    start_advertising = false;
  }

#ifdef TEST
  uint32_t const uptime = now / 1000;
  if (uptime != last_uptime) {
    last_uptime = uptime;
    tx_buff += String(uptime);
    tx_flush();
  }
#endif

  monitor_peers();
  esp_task_wdt_reset();
}
