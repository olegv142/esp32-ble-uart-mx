/*
 * The BLE transmitter receiving data from serial port and updating its characteristic allowing
 * peer to subscribe to updates. 
 *
 * Its based on the ble_receiver example with the following additions:
 *  1. Reading data to transmit from serial port
 *  2. Using watchdog for better reliability
 *  3. Increased MTU
 *  4. Optionally adding suffix based on MAC to device name to make it distinguishable
 *
 * Tested on ESP32 C3 with SDK v.3.0
 * Use ../ble_receiver or ../ble_uart_rx for other side of the connection.
 *
 * Author: Oleg Volkov
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_mac.h>
#include <esp_task_wdt.h>

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
bool deviceConnected = false;
bool advertising = false;
uint32_t connectedTs;

String serial_buff;
uint32_t serial_ts;

#define WDT_TIMEOUT 20000 // msec

#define SERVICE_UUID           "FFE0"
#define CHARACTERISTIC_UUID_TX "FFE1"
#define DEV_NAME               "TestC3"
// Uncomment to add suffix based on MAC to device name to make it distinguishable
//#define DEV_NAME_SUFF_LEN      6

// Undefine to keep default power level
#define TX_PW_BOOST ESP_PWR_LVL_P21

// There is no flow control in USB serial port.
// The default buffer size is 256 bytes which may be not enough.
#define CDC_BUFFER_SZ 4096

#define CONNECTED_LED 8

// If defined send uptime every second instead of data from UART
//#define TEST

#ifndef TEST
// If USE_SEQ_TAG is defined every chunk of data transmitted (characteristic update) will carry sequence tag as the first symbol.
// It will get its value from 16 characters sequence 'a', 'b', .. 'p'. Next update will use next symbol. After 'p' the 'a' will
// be used again. The receiver may use sequence tag to control the order of delivery of updates detecting missed or reordered
// updates.
#define USE_SEQ_TAG
#endif

#ifdef USE_SEQ_TAG
#define NTAGS 16
#define FIRST_TAG 'a'
char next_tag = FIRST_TAG;
#else
#endif

#ifdef TEST
uint32_t last_uptime;
#endif


String dev_name(DEV_NAME);

static inline void serial_buff_reset()
{
#ifdef USE_SEQ_TAG
      serial_buff = ' ';
#else
      serial_buff = "";
#endif
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      connectedTs = millis();
      advertising = false;
      serial_buff_reset();
      digitalWrite(CONNECTED_LED, LOW);
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      connectedTs = millis();
      digitalWrite(CONNECTED_LED, HIGH);
    }
};

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

void esp_task_wdt_isr_user_handler(void)
{
  esp_restart();
}

static inline void watchdog_init()
{
  esp_task_wdt_config_t wdt_cfg = {.timeout_ms = WDT_TIMEOUT, .idle_core_mask = (1<<portNUM_PROCESSORS)-1, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt_cfg); // enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);             // add current thread to WDT watch
}

void setup()
{
  Serial.begin(115200);
  Serial.setTimeout(10);
#ifdef CDC_BUFFER_SZ
  Serial.setRxBufferSize(CDC_BUFFER_SZ);
#endif
  pinMode(CONNECTED_LED, OUTPUT);
  digitalWrite(CONNECTED_LED, HIGH);

  watchdog_init();
  // Create the BLE Device
  init_dev_name();
  BLEDevice::init(dev_name);
  BLEDevice::setMTU(247);

#ifdef TX_PW_BOOST
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, TX_PW_BOOST); 
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,     TX_PW_BOOST);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN,    TX_PW_BOOST);
#endif

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pTxCharacteristic = pService->createCharacteristic(
										CHARACTERISTIC_UUID_TX,
										BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
									);

  pTxCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData data;
  data.setName(dev_name);
  pAdvertising->setAdvertisementData(data);
  pAdvertising->setScanResponse(true);
  pAdvertising->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();
  advertising = true;
}

static void do_transmit(uint16_t max_chunk, bool all)
{
  uint8_t* pdata = (uint8_t*)serial_buff.c_str();
  unsigned data_len = serial_buff.length(), sent = 0;
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
    pTxCharacteristic->setValue(pdata - 1, chunk + 1);
#else
    pTxCharacteristic->setValue(pdata, chunk);
#endif
    pTxCharacteristic->notify();
    sent     += chunk;
    pdata    += chunk;
    data_len -= chunk;
    if (!all)
      break;
  }
  serial_buff = serial_buff.substring(sent);
}

static inline bool is_msg_terminator(char c)
{
  return c == '\n' || c == '\r';
}

static inline uint16_t max_tx_chunk()
{
  return BLEDevice::getMTU() - 3;
}

void loop()
{
#ifndef TEST
  String const received = Serial.readString();
  if (deviceConnected) {
    if (received.length()) {
      serial_buff += received;
      serial_ts = millis();
    }
    unsigned const data_len = serial_buff.length();
    if (data_len) {
      uint16_t const max_chunk = max_tx_chunk();
      bool const full_msg = is_msg_terminator(serial_buff[data_len-1]);
      if (full_msg || data_len >= max_chunk || millis() - serial_ts > 100)
        do_transmit(max_chunk, full_msg);
    }
  }
#else
  uint32_t const uptime = millis() / 1000;
  if (uptime != last_uptime) {
    last_uptime = uptime;
    serial_buff = String(uptime);
    do_transmit(max_tx_chunk(), true);
    Serial.println(uptime);
  }
#endif
  if (!deviceConnected && !advertising && millis() - connectedTs > 500) {
    BLEDevice::startAdvertising(); // restart advertising
    advertising = true;
  }
  esp_task_wdt_reset();
}
