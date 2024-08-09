/*
 * This is the dual mode BLE device example.
 * Its based on ble_uart_rx and ble_uart_tx examples.
 *
 * Tested on ESP32 C3 with SDK v.3.0
 * Use ../ble_transmitter or ../ble_uart_tx for other side of the connection.
 *
 * Author: Oleg Volkov
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLE2902.h>
#include <esp_mac.h>
#include <esp_task_wdt.h>
#include <driver/uart.h>

#define DEV_NAME               "TestC3"

// Uncomment to add suffix based on MAC to device name to make it distinguishable
#define DEV_NAME_SUFF_LEN      6

// If DEV_ADDR is defined the connection will be established without scan
// In such scenario the DEV_NAME is not used
// #define DEV_ADDR               "EC:DA:3B:BB:CE:02"

#define SERVICE_UUID           "FFE0"
#define CHARACTERISTIC_UUID_TX "FFE1"

#define LONG_UUID(uuid) ("0000" uuid "-0000-1000-8000-00805f9b34fb")

#define SCAN_TIME              5     // sec
#define CONNECT_TOUT           5000  // msec
#define WDT_TIMEOUT            20000 // msec

// If UART_TX_PIN is defined the data will be output to the hardware serial port
// Otherwise the USB virtual serial port will be used for that purpose
// #define UART_TX_PIN  7
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
#define UART_BEGIN "data: "
#define UART_END   "\n"
#endif

/*
 The BT stack is complex and not well tested bunch of software. Using it one can easily be trapped onto the state
 where there is no way out. The biggest problem is that connect routine may hung forever. Although the connect call
 has timeout parameter, it does not help. The call may complete on timeout without errors, but the connection will
 not actually be established. That's why we are using watchdog to detect connection timeout. Its unclear if soft
 reset by watchdog is equivalent to the power cycle or reset by pulling low EN pin. That's why there is an option
 to implement hard reset on connect timeout by hard wiring some output pin to EN input of the chip.
*/
// If defined use hard reset on connect timeout
#define RST_OUT_PIN 3

// Connected LED pin, active low
#define CONNECTED_LED 8

// Undefine to keep default power level
#define TX_PW_BOOST ESP_PWR_LVL_P21

// If defined self reset after the specified time since start (for testing)
// #define SELF_RESET_AFTER 150000 // msec

#define RSSI_REPORT_INTERVAL 5000

// If defined echo back all data received from central
// #define ECHO

// If USE_SEQ_TAG is defined every chunk of data transmitted (characteristic update) will carry sequence tag as the first symbol.
// It will get its value from 16 characters sequence 'a', 'b', .. 'p'. Next update will use next symbol. After 'p' the 'a' will
// be used again. The receiver may use sequence tag to control the order of delivery of updates detecting missed or reordered
// updates.
// #define USE_SEQ_TAG

#ifdef USE_SEQ_TAG
#define NTAGS 16
#define FIRST_TAG 'a'
char next_tag = FIRST_TAG;
#endif

BLEServer*           pServer;
BLECharacteristic*   pCharacteristic;
BLEScan*             pScan;
BLEAdvertisedDevice* peerDevice;
BLEClient*           peerClient;

bool     is_scanning;
bool     peer_connected;
uint32_t peer_disconn_ts;
uint32_t rssi_reported_ts;

bool     start_advertising = true;
uint32_t centr_disconn_ts;

String dev_name(DEV_NAME);
String tx_buff;

#ifndef ECHO
uint32_t last_uptime;
#endif

static void do_transmit(bool all);

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  // Called for each advertising BLE server.
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Device found: ");
    Serial.println(advertisedDevice.toString().c_str());
    if (advertisedDevice.getName() == DEV_NAME)
    {
      Serial.println("Peer device found");
      peerDevice = new BLEAdvertisedDevice(advertisedDevice);
    }
  }
};

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
      Serial.println("central connected");
      tx_buff_reset();
    };

    void onDisconnect(BLEServer* pServer) {
      Serial.println("central disconnected");
      centr_disconn_ts = millis();
      start_advertising = true;
    }
};

static void notifyConnected()
{
#if defined(UART_BEGIN) && defined(UART_END)
  DataSerial.print(UART_BEGIN);
  DataSerial.print(UART_END);
#endif
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) {
    Serial.println("peer connected");
    peer_connected = true;
    notifyConnected();
    digitalWrite(CONNECTED_LED, LOW);
  }
  void onDisconnect(BLEClient *pclient) {
    Serial.println("peer disconnected");
    peer_connected = false;
    peer_disconn_ts = millis();
    digitalWrite(CONNECTED_LED, HIGH);
  }
};

class MyCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    Serial.print("rx: ");
    Serial.println(rxValue);
#ifdef ECHO
    tx_buff += rxValue;
    do_transmit(true);
#endif
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

static void do_reset(const char* what)
{
  Serial.println(what);
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

#ifndef DEV_ADDR
  pScan = BLEDevice::getScan(); // create new scan
  pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pScan->setActiveScan(true);   // active scan uses more power, but get results faster
  pScan->setInterval(100);
  pScan->setWindow(100);  // less or equal setInterval value
#endif
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
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
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

  pinMode(CONNECTED_LED, OUTPUT);
  digitalWrite(CONNECTED_LED, HIGH);

#ifdef UART_TX_PIN
  DataSerial.begin(UART_BAUD_RATE, UART_MODE, UART_PIN_NO_CHANGE, UART_TX_PIN);
#ifdef UART_CTS_PIN
  uart_set_pin(DATA_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_CTS_PIN);  
  uart_set_hw_flow_ctrl(DATA_UART_NUM, UART_HW_FLOWCTRL_CTS, 0);
#endif
#endif
  tx_buff_reset();
}

void setup()
{
  hw_init();
  watchdog_init();
  bt_device_init();
  bt_device_start();
}

static inline void report_rssi()
{
  Serial.print("rssi: ");
  Serial.println(peerClient->getRssi());
}

static void peerNotifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
#ifdef UART_BEGIN
  DataSerial.print(UART_BEGIN);
#endif
  DataSerial.write(pData, length);
#ifdef UART_END
  DataSerial.print(UART_END);
#endif
}

static void connectToPeer(String const& addr)
{
  Serial.print("Connecting to ");
  Serial.println(addr);

  peerClient = BLEDevice::createClient();
  peerClient->setClientCallbacks(new MyClientCallback());

  peerClient->connect(addr);
  peerClient->setMTU(247);  // Request increased MTU from server (default is 23 otherwise)

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = peerClient->getService(SERVICE_UUID);
  // Reset itself on error to avoid dealing with de-initialization
  if (!pRemoteService) {
    do_reset("Failed to find our service UUID");
    return;
  }
  // Obtain a reference to the characteristic in the service of the remote BLE server.
  BLERemoteCharacteristic *pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID_TX);
  if (!pRemoteCharacteristic) {
    do_reset("Failed to find our characteristic UUID");
    return;
  }
  // Subscribe to updates
  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(peerNotifyCallback);
  } else {
    do_reset("Notification not supported by the server");
  }
}

#ifndef DEV_ADDR
static void scan_complete_cb(BLEScanResults res)
{
  Serial.print("Devices found: ");
  Serial.println(res.getCount());
  Serial.println("Scan done!");
  is_scanning = false;
}
#endif

static inline uint16_t max_tx_chunk()
{
  return BLEDevice::getMTU() - 3;
}

static void do_transmit(bool all)
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
    if (!all)
      break;
  }
  tx_buff = tx_buff.substring(sent);
}

void loop()
{
  uint32_t const now = millis();
#ifndef DEV_ADDR
  if (!peerDevice && !is_scanning) {
    pScan->clearResults();  // delete results fromBLEScan buffer to release memory
    Serial.println("Scanning...");
    pScan->start(SCAN_TIME, scan_complete_cb, true);
    is_scanning = true;
  }
  if (peerDevice && !is_scanning && !peer_connected) {
    connectToPeer(peerDevice->getAddress().toString());
    return;
  }
#else
  if (!peer_connected && now - peer_disconn_ts > 500) {
    connectToPeer(DEV_ADDR);
    return;
  }
#endif

  if (peer_connected && now - rssi_reported_ts > RSSI_REPORT_INTERVAL) {
    rssi_reported_ts = now;
    report_rssi();
  }

#ifndef ECHO
  uint32_t const uptime = millis() / 1000;
  if (uptime != last_uptime) {
    last_uptime = uptime;
    tx_buff += String(uptime);
    do_transmit(true);
  }
#endif

  if (start_advertising && millis() - centr_disconn_ts > 500) {
    Serial.println("start advertising");
    BLEDevice::startAdvertising(); // restart advertising
    start_advertising = false;
  }

#ifdef SELF_RESET_AFTER
  if (now > SELF_RESET_AFTER)
    do_reset("reset itself for testing");
#endif

  esp_task_wdt_reset();
}

