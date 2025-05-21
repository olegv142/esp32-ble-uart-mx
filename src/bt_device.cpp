#include "bt_device.h"
#include "mx_uart.h"
#include "watchdog.h"
#include "debug.h"
#include "util.h"

#include <BLEDevice.h>
#include <BLEAdvertisedDevice.h>
#include <BLE2902.h>
#include <esp_mac.h>

String   bt_dev_name(DEV_NAME);
String   bt_dev_addr;

static bt_rx_cb_t bt_rx_cb;

BLECharacteristic* bt_char_tx; // peripheral transmit there
BLECharacteristic* bt_char_rx; // peripheral receive there

struct err_count bt_notify_err;

int bt_connected_centrals;

#ifndef HIDDEN
bool bt_advertising_enabled = true;
#else
bool bt_advertising_enabled = false;
#endif

#ifdef WRITABLE
static bool writable = true;
#else
static bool writable = false;
#endif

static bool     start_advertising = true;
static uint32_t centr_disconn_ts;
static uint32_t last_char_error;

static inline char byte_signature(uint8_t v)
{
    return hex_digit((v & 0xf) ^ (v >> 4));
}

static void init_dev_name(void)
{
#ifdef DEV_NAME_SUFF_LEN
  uint8_t mac[8] = {0};
  if (ESP_OK == esp_efuse_mac_get_default(mac)) {
    for (int i = 0; i < DEV_NAME_SUFF_LEN && i < ESP_BD_ADDR_LEN; ++i)
      bt_dev_name += byte_signature(mac[i]);
  }
#endif
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      if (bt_advertising_enabled) {
        // For some unknown reason the spurious call of this
        // function are possible on connect to peripheral device
        if (bt_rx_cb) {
          struct data_chunk ch = {.data = nullptr, .len = 0};
          bt_rx_cb(&ch);
        }
      }
      ++bt_connected_centrals;
    };

    void onDisconnect(BLEServer* pServer) {
      centr_disconn_ts = millis();
      start_advertising = true;
      --bt_connected_centrals;
    }
};

static inline void transmit_to_central(uint8_t* pdata, size_t sz)
{
  bt_char_tx->setValue(pdata, sz);
  bt_char_tx->notify();
}

class MyCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    if (pCharacteristic != bt_char_rx)
      return;
    size_t const length  = bt_char_rx->getLength();
    uint8_t* const pData = bt_char_rx->getData();
    if (bt_rx_cb) {
      struct data_chunk ch = {.data = (uint8_t*)malloc(length), .len = length};
      if (!ch.data)
        fatal("No memory");
      memcpy(ch.data, pData, length);
      bt_rx_cb(&ch);
    }
#ifdef ECHO
    transmit_to_central(pData, length);
#endif
  }
  void onStatus(BLECharacteristic * ch, Status s, uint32_t code) {
    if (ch == bt_char_tx && s == Status::ERROR_GATT)
      last_char_error = code;
  }
};

static void setup_tx_power(void)
{
#ifdef TX_PW_BOOST
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, TX_PW_BOOST); 
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,     TX_PW_BOOST);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN,    TX_PW_BOOST);
#endif
}

void bt_device_init(bt_rx_cb_t rx_cb)
{
  // Create the BLE Device
  bt_rx_cb = rx_cb;
  if (!rx_cb)
    writable = false;
  init_dev_name();
  BLEDevice::init(bt_dev_name);
  BLEDevice::setMTU(MAX_SIZE+3);
  setup_tx_power();
}

void bt_device_start(void)
{
  // Create the BLE Server
  BLEServer* pServer = BLEDevice::createServer();

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  const uint32_t prop_write = writable ? BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR : 0;
  const uint32_t perm_write = writable ? ESP_GATT_PERM_WRITE : 0;

  // Create a BLE Characteristic
  bt_char_tx = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
#ifndef DUAL_CHAR
    | prop_write
#endif
  );

  bt_char_tx->setAccessPermissions(
    ESP_GATT_PERM_READ
#ifndef DUAL_CHAR
    | perm_write
#endif
  );
  bt_char_tx->addDescriptor(new BLE2902());
  bt_char_tx->setCallbacks(new MyCharCallbacks());

#ifndef DUAL_CHAR
  bt_char_rx = bt_char_tx;
#else
  bt_char_rx = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_READ | prop_write
  );
  bt_char_rx->setAccessPermissions(
    ESP_GATT_PERM_READ | perm_write
  );
  bt_char_rx->addDescriptor(new BLE2902());
  bt_char_rx->setCallbacks(new MyCharCallbacks());
#endif

  pServer->setCallbacks(new MyServerCallbacks());

  // Start the service
  pService->start();
  // Initialize advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData data;
  data.setName(bt_dev_name);
  pAdvertising->setAdvertisementData(data);
  pAdvertising->setScanResponse(true);
  pAdvertising->addServiceUUID(SERVICE_UUID);

  bt_dev_addr = BLEDevice::getAddress().toString();

#ifndef NO_DEBUG
  uart_debug_begin();
  uart_print_strz("BT device ");
  uart_print(bt_dev_name);
  uart_print_strz(" at ");
  uart_print(bt_dev_addr);
  uart_print_strz(" on ");
  uart_print(ESP.getChipModel());
  uart_print_strz(" ");
  uart_print(getCpuFrequencyMhz());
  uart_print_strz("MHz ");
  uart_print(ESP.getFreeHeap());
  uart_print_strz(" bytes free");
  uart_end();
#endif
}

void bt_maybe_start_advertising(void)
{
  if (bt_advertising_enabled && start_advertising && elapsed_since(centr_disconn_ts) > 100) {
    debug_strz("start advertising");
    BLEDevice::startAdvertising(); // restart advertising
    start_advertising = false;
  }
}

bool bt_transmit_to_central(uint8_t* pdata, size_t sz)
{
  last_char_error = 0;
  transmit_to_central(pdata, sz);
  if (last_char_error) {
    ++bt_notify_err.cnt;
    return false;
  }
  return true;
}
