#pragma once

/*
 * BLE device core implementation
 */

#include "mx_types.h"
#include <Arduino.h>

extern String             bt_dev_name;
extern String             bt_dev_addr;
extern struct err_count   bt_notify_err;
extern int                bt_connected_centrals;
extern bool               bt_advertising_enabled;

typedef void (*bt_rx_cb_t)(struct data_chunk*);

void bt_device_init(bt_rx_cb_t rx_cb);
void bt_device_start(void);
void bt_maybe_start_advertising(void);
bool bt_transmit_to_central(uint8_t* pdata, size_t sz);
