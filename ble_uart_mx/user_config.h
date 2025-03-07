#pragma once

// Here you can include the configuration of your choice
// .. for example

// Peripheral device with USB interface
//#include "config/usb_peripheral.h"
// USB key as central device
// #include "config/usb_key_central.h"
// USB key dual role
// #include "config/usb_key.h"
// USB key with hidden peripheral role
#include "es_key.h"
#include "config/usb_key_hidden.h"

// Serial adapter
// #include "config/ble_serial.h"
// Serial adapter as central device only
// #include "config/ble_serial_central.h"

// The following pair of config files is meant to be used for
// creating point to point link with automatic connect.
// The link is using simplified protocol with only data messages.
// #include "config/simple_master.h"
// #include "config/simple_slave.h"
