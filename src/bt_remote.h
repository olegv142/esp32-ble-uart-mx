#pragma once

/*
 * Remote peripheral connection base class
 */

#include "debug.h"
#include "mx_types.h"
#include <BLEDevice.h>

class RemoteClient : public BLEClientCallbacks {
public:
  RemoteClient(String const& addr);

  void connect(void);
  void subscribe(void);

private:
friend void remote_client_notify(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *data, size_t len, bool isNotify);
  bool notify_remote_data(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *data, size_t len);

protected:
  bool send_data(uint8_t* data, size_t len, bool with_response);
  virtual void notify_data(uint8_t *data, size_t len) = 0;

  String      m_addr;
  BLEClient*  m_Client;
  bool        m_writable;
  bool        m_indicates;
  bool        m_subscribed;
  BLERemoteCharacteristic* m_remoteTx;
  BLERemoteCharacteristic* m_remoteRx;
};

extern struct err_count bt_unknown_data_src;
