/*
 * The BLE receiver connecting to the device with particular name and subscribing
 * to the characteristic updates. Outputs received data to the hardware serial port.
 * Its based on ../ble_receiver example with the following additions:
 *  1. Option to output received data to hardware UART port. If UART_TX_PIN is
 *     not defined data will be printed to USB serial port as other debug messages.
 *  2. Enclose data between the start marker '\1'
 *     and the end marker '\0'. So the receiving application will
 *     be able to split data stream onto packets provided that the data is textual.
 *     Undefine UART_BEGIN and UART_END to disable this feature.
 *  3. Sending empty start/end marker pair on connect as start of the stream marker.
 *  4. RSSI reporting
 *  5. Option to connect without scanning in case the target address is known.
 *     It should be defined as DEV_ADDR to enable such mode.
 *  6. Optional hard reset on watchdog timeout for better reliability.
 *  7. Increased MTU
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
#include <esp_task_wdt.h>
#include <driver/uart.h>

#define DEV_NAME               "TestC3"

// If DEV_ADDR is defined the connection will be established without scan
// In such scenario the DEV_NAME is not used
// #define DEV_ADDR               "EC:DA:3B:BB:CE:02"

#define SERVICE_UUID           "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID_TX "0000ffe1-0000-1000-8000-00805f9b34fb"
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

// Undefine to keep default power level
#define TX_PW_BOOST ESP_PWR_LVL_P21

#define CONNECTED_LED 8

// If defined the receiver will reset itself after being in connected state for the specified time (for testing)
// #define SELF_RESET_AFTER_CONNECTED 60000 // msec

#define RSSI_REPORT_INTERVAL 5000

// The remote service we wish to connect to.
static BLEUUID serviceUUID(SERVICE_UUID);
// The characteristic of the remote service we are interested in.
static BLEUUID charUUID(CHARACTERISTIC_UUID_TX);

BLEScan *pBLEScan;
BLEAdvertisedDevice *myDevice;
BLEClient *pClient;

bool is_scanning;
bool is_connected;
uint32_t connected_ts;
uint32_t rssi_reported_ts;

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

static void reset_self()
{
#ifdef RST_OUT_PIN
  pinMode(RST_OUT_PIN, OUTPUT);
  digitalWrite(RST_OUT_PIN, LOW);
#else
  esp_restart();
#endif
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

void setup()
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

  watchdog_init();
  BLEDevice::init("");

#ifdef TX_PW_BOOST
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, TX_PW_BOOST); 
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,     TX_PW_BOOST);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN,    TX_PW_BOOST);
#endif

#ifndef DEV_ADDR
  pBLEScan = BLEDevice::getScan(); // create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);   // active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(100);  // less or equal setInterval value
#endif
}

static void notifyConnected()
{
#if defined(UART_BEGIN) && defined(UART_END)
  DataSerial.print(UART_BEGIN);
  DataSerial.print(UART_END);
#endif
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) {
    Serial.println("connected");
    is_connected = true;
    connected_ts = millis();
    notifyConnected();
    digitalWrite(CONNECTED_LED, LOW);
  }
  void onDisconnect(BLEClient *pclient) {
    Serial.println("disconnected");
    is_connected = false;
    connected_ts = millis();
    digitalWrite(CONNECTED_LED, HIGH);
  }
};

static inline void report_rssi()
{
  Serial.print("rssi: ");
  Serial.println(pClient->getRssi());
}

static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
#ifdef UART_BEGIN
  DataSerial.print(UART_BEGIN);
#endif
  DataSerial.write(pData, length);
#ifdef UART_END
  DataSerial.print(UART_END);
#endif
}

static void do_reset(const char* what)
{
  Serial.println(what);
  delay(100); // give host a chance to read message
  reset_self();
}

static void connectToServer(String const& addr)
{
  Serial.print("Connecting to ");
  Serial.println(addr);

  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  pClient->connect(addr);
  pClient->setMTU(247);  // Request increased MTU from server (default is 23 otherwise)

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  // Reset itself on error to avoid dealing with de-initialization
  if (!pRemoteService) {
    do_reset("Failed to find our service UUID");
    return;
  }
  // Obtain a reference to the characteristic in the service of the remote BLE server.
  BLERemoteCharacteristic *pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (!pRemoteCharacteristic) {
    do_reset("Failed to find our characteristic UUID");
    return;
  }
  // Subscribe to updates
  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(notifyCallback);
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

void loop()
{
  uint32_t const now = millis();
#ifndef DEV_ADDR
  if (!myDevice && !is_scanning) {
    pBLEScan->clearResults();  // delete results fromBLEScan buffer to release memory
    Serial.println("Scanning...");
    pBLEScan->start(SCAN_TIME, scan_complete_cb, true);
    is_scanning = true;
  }
  if (myDevice && !is_scanning && !is_connected) {
    connectToServer(myDevice->getAddress().toString());
    return;
  }
#else
  if (!is_connected && now - connected_ts > 500) {
    connectToServer(DEV_ADDR);
    return;
  }
#endif

#ifdef SELF_RESET_AFTER_CONNECTED
  if (is_connected && now - connected_ts > SELF_RESET_AFTER_CONNECTED)
    do_reset("reset itself for testing");
#endif

  if (is_connected && now - rssi_reported_ts > RSSI_REPORT_INTERVAL) {
    rssi_reported_ts = now;
    report_rssi();
  }

  esp_task_wdt_reset();
}

