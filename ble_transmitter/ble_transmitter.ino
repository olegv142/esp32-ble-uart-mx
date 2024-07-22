/*
 * The BLE transmitter periodically updating its characteristic allowing
 * peer to subscribe to updates. 
 *
 * Its based on the official Arduino examples with the following improvements:
 *  1. Increased the RF power for longer range
 *  2. Adding more data to advertising so it can be correctly discovered
 *  3. LED to indicate connection status
 *
 * Tested on ESP32 C3 with SDK v.3.0
 * Use ../ble_receiver for other side of the connection.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
bool deviceConnected = false;
bool advertising = false;
uint32_t connectedTs;
uint8_t txValue = '0';

#define DEV_NAME               "TestC3"
#define SERVICE_UUID           "FFE0"
#define CHARACTERISTIC_UUID_TX "FFE1"

#undef  LED_BUILTIN
#define LED_BUILTIN 8

// Undefine to keep default power level
#define TX_PW_BOOST ESP_PWR_LVL_P21

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    connectedTs = millis();
    advertising = false;
    digitalWrite(LED_BUILTIN, LOW);
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    connectedTs = millis();
    digitalWrite(LED_BUILTIN, HIGH);
  }
};

void setup()
{
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Create the BLE Device
  BLEDevice::init(DEV_NAME);

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
  data.setName(DEV_NAME);
  pAdvertising->setAdvertisementData(data);
  pAdvertising->setScanResponse(true);
  pAdvertising->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();
  advertising = true;
}

void loop()
{
  if (deviceConnected) {
    if (++txValue > '9')
      txValue = '0';
    Serial.println((char)txValue);
    pTxCharacteristic->setValue(&txValue, 1);
    pTxCharacteristic->notify();
    delay(1000);
  }

  if (!deviceConnected && !advertising && millis() - connectedTs > 500) {
    BLEDevice::startAdvertising(); // restart advertising
    advertising = true;
  }
}
