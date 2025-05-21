#include "bt_remote.h"
#include "mx_config.h"
#include "mx_uart.h"

static RemoteClient* remote_clients[MAX_PEERS];
static unsigned remote_clients_cnt;

struct err_count bt_unknown_data_src;

void remote_client_notify(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
  for (unsigned i = 0; i < remote_clients_cnt; ++i)
    if (remote_clients[i]->notify_remote_data(pBLERemoteCharacteristic, pData, length))
      return;

  ++bt_unknown_data_src.cnt;
}

RemoteClient::RemoteClient(String const& addr)
  : m_addr(addr)
  , m_Client(nullptr)
  , m_writable(false)
  , m_subscribed(false)
  , m_remoteTx(nullptr)
  , m_remoteRx(nullptr)
{
  BUG_ON(remote_clients_cnt >= MAX_PEERS);
  remote_clients[remote_clients_cnt++] = this;
}

bool RemoteClient::notify_remote_data(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *data, size_t len)
{
    if (pBLERemoteCharacteristic != m_remoteTx)
      return false;
    notify_data(data, len);
    return true;
}

void RemoteClient::connect(void)
{
#ifndef NO_DEBUG
  uint32_t const start = millis();
  uart_debug_begin();
  uart_print_strz("connecting to ");
  uart_print(m_addr);
  uart_end();
#endif

  if (!m_Client) {
    m_Client = BLEDevice::createClient();
    m_Client->setClientCallbacks(this);
  }

  m_Client->connect(m_addr);
  m_Client->setMTU(MAX_SIZE+3);  // Request increased MTU from server (default is 23 otherwise)

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = m_Client->getService(SERVICE_UUID);
  // Reset itself on error to avoid dealing with de-initialization
  if (!pRemoteService)
    fatal("Failed to find our service UUID");
  // Obtain a reference to the characteristic in the service of the remote BLE server.
  m_remoteTx = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID_TX);
  if (!m_remoteTx)
    fatal("Failed to find our characteristic UUID");
  if (!m_remoteTx->canNotify())
    fatal("Notification not supported by the server");
#ifndef DUAL_CHAR
  m_remoteRx = m_remoteTx;
#else
  m_remoteRx = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID_RX);
  if (!m_remoteRx)
    fatal("Failed to find our characteristic UUID");
#endif
  m_writable = m_remoteRx->canWrite();

#ifndef NO_DEBUG
  uart_debug_begin();
  uart_print_strz("connected to ");
  uart_print(m_addr);
  uart_print_strz(" in ");
  uart_print(millis() - start);
  uart_print_strz(" msec");
  if (m_writable)
    uart_print_strz(", writable");
  else
    uart_print_strz(", readonly");
  uart_print_strz(", rssi=");
  uart_print(m_Client->getRssi());
  uart_end();
#endif
}

void RemoteClient::subscribe(void)
{
#ifndef NO_DEBUG
  uint32_t const start = millis();
  uart_debug_begin();
  uart_print_strz("subscribing to ");
  uart_print(m_addr);
  uart_end();
#endif

  // Subscribe to updates
  m_remoteTx->registerForNotify(remote_client_notify);
  m_subscribed = true;

#ifndef NO_DEBUG
  uart_debug_begin();
  uart_print_strz("subscribed to ");
  uart_print(m_addr);
  uart_print_strz(" in ");
  uart_print(millis() - start);
  uart_print_strz(" msec, ");
  uart_print(ESP.getFreeHeap());
  uart_print_strz(" bytes free");
  uart_end();
#endif
}

bool RemoteClient::send_data(uint8_t* data, size_t len, bool with_response)
{
  BLEClient * const clnt = m_remoteRx->getRemoteService()->getClient();
  return ESP_OK == esp_ble_gattc_write_char(
    clnt->getGattcIf(), clnt->getConnId(), m_remoteRx->getHandle(), len, data,
    with_response ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
    ESP_GATT_AUTH_REQ_NONE
  );
}

