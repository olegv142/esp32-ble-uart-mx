/*
 * The BLE receiver connecting to the device with particular name and subscribing
 * to the characteristic updates.
 *
 * Its based on the official Arduino examples with the following improvements:
 *  1. Increased the RF power for longer range
 *  2. Using watchdog to avoid hanging at connecting to device
 *  3. Automatic reconnect after disconnection
 *  4. LED to indicate connection status
 *
 * Tested on ESP32 C3 with SDK v.3.0
 * Use ../ble_transmitter or ../ble_uart_tx for other side of the connection.
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_task_wdt.h>

#undef  LED_BUILTIN
#define LED_BUILTIN 8

#define DEV_NAME               "TestC3"
#define SERVICE_UUID           "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID_TX "0000ffe1-0000-1000-8000-00805f9b34fb"
#define SCAN_TIME              5     // sec
#define CONNECT_TOUT           5000  // msec
#define WDT_TIMEOUT            20000 // msec

// Undefine to keep default power level
#define TX_PW_BOOST ESP_PWR_LVL_P21

// The remote service we wish to connect to.
static BLEUUID serviceUUID(SERVICE_UUID);
// The characteristic of the remote service we are interested in.
static BLEUUID charUUID(CHARACTERISTIC_UUID_TX);

static BLEScan *pBLEScan;
static BLEAdvertisedDevice *myDevice;
static BLEClient *pClient;
static bool is_scanning;
static bool is_connected;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  // Called for each advertising BLE server.
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Device found: ");
    Serial.println(advertisedDevice.toString().c_str());
    if (advertisedDevice.getName() == DEV_NAME)
    {
      Serial.println("Peer device found");
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
    }
  }
};

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
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  watchdog_init();
  BLEDevice::init("");

#ifdef TX_PW_BOOST
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, TX_PW_BOOST); 
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,     TX_PW_BOOST);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN,    TX_PW_BOOST);
#endif

  pBLEScan = BLEDevice::getScan(); // create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);   // active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(100);  // less or equal setInterval value
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) {
    is_connected = true;
    Serial.println("connected");
    digitalWrite(LED_BUILTIN, LOW);
  }
  void onDisconnect(BLEClient *pclient) {
    is_connected = false;
    Serial.println("disconnected");
    digitalWrite(LED_BUILTIN, HIGH);
  }
};

static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
  Serial.print("data[");
  Serial.print(length);
  Serial.print("]: ");
  Serial.write(pData, length);
  Serial.println();
}

void connectToServer()
{
  Serial.print("Connecting to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  if (!pClient) {
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
  } else
    pClient->disconnect();

  // Connect to the remove BLE Server.
  pClient->connect(myDevice);

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (!pRemoteService) {
    Serial.print("Failed to find our service UUID");
    pClient->disconnect();
    return;
  }
  // Obtain a reference to the characteristic in the service of the remote BLE server.
  BLERemoteCharacteristic *pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (!pRemoteCharacteristic) {
    Serial.print("Failed to find our characteristic UUID");
    pClient->disconnect();
    return;
  }
  // Subscribe to updates
  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(notifyCallback);
  } else {
    Serial.print("Notification not supported by the server");
    pClient->disconnect();
  }
}

void scan_complete_cb(BLEScanResults res)
{
  Serial.print("Devices found: ");
  Serial.println(res.getCount());
  Serial.println("Scan done!");
  is_scanning = false;
}

void loop()
{
  if (!myDevice && !is_scanning) {
    pBLEScan->clearResults();  // delete results fromBLEScan buffer to release memory
    Serial.println("Scanning...");
    pBLEScan->start(SCAN_TIME, scan_complete_cb, true);
    is_scanning = true;
  }
  if (myDevice && !is_scanning && !is_connected)
    connectToServer();

  esp_task_wdt_reset();
}

