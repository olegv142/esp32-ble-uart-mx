#pragma once

// Version info printed as part of idle status message
#define VMAJOR    "1"
#define VMINOR    "0"
#define VARIANT   "A" // A means ASCII, binary data not allowed

#define VERSION VMAJOR "." VMINOR "-" VARIANT

// Device name (may be followed by unique suffix)
#define DEV_NAME  "Mx-"

// If defined the unique suffix based on MAC is added to device name to make it distinguishable
#define DEV_NAME_SUFF_LEN  6

// If defined the device will not advertise so central will be unable to connect to it.
// The connections to peers may be created though.
// #define HIDDEN

#ifndef HIDDEN
// If defined the connected central may write to this device
#define WRITABLE
#endif

// Connected LED pin, active low
#define CONNECTED_LED 8

#define SERVICE_UUID           "FFE0"
#define CHARACTERISTIC_UUID_TX "FFE1"

// Watchdog timeout. It will restart esp32 if some operation will hung.
#define WDT_TIMEOUT            20000 // msec

// If defined the status messages will be output periodically
#define STATUS_REPORT_INTERVAL 1000  // msec

// If defined suppress debug messages
// #define NO_DEBUG

// If HW_UART is defined the hardware serial port will be used for communications.
// Otherwise the USB virtual serial port will be utilized.
#define HW_UART

#ifdef HW_UART
// Use even parity if defined
#define UART_USE_PARITY

#define UART_TX_PIN  7
#define UART_RX_PIN  6
#define UART_BAUD_RATE 115200
#ifndef UART_USE_PARITY
#define UART_MODE SERIAL_8N1
#else
#define UART_MODE SERIAL_8E1
#endif
// If defined the flow control on UART will be configured
// CTS prevents overflow of the host receiving buffer. Use it when
// you have USB serial adapter at the host side. They typically have
// buffer capacity of only 128 bytes.
#define UART_CTS_PIN 5
// RTS prevents overflow of the esp32 receiving buffer.
#define UART_RTS_PIN 4
#endif

#ifdef HW_UART
// Using hardware UART
#define DataSerial Serial1
#define DATA_UART_NUM UART_NUM_1
#define UART_BEGIN '\1'
#define UART_END   '\0'
#else
// Using USB CDC
#define DataSerial Serial
#define UART_END   '\n'
#endif

// There is no flow control in USB serial port.
// The default buffer size is 256 bytes which may be not enough.
#define UART_BUFFER_SZ 4096

// If defined reset itself on peripheral disconnection instead of reconnecting
#define RESET_ON_DISCONNECT

// Undefine to keep default power level
#define TX_PW_BOOST ESP_PWR_LVL_P21

// If AUTOCONNECT is defined it will connect on startup to the predefined set of peers.
// The host commands will be disabled. One may use AUTOCONNECT with no target peers
// to disable creating connections.
// #define AUTOCONNECT

#ifdef AUTOCONNECT
// Peer device address to connect to
#define PEER_ADDR    "EC:DA:3B:BB:CE:02"
//#define PEER_ADDR1   "34:B7:DA:F6:44:B2"
//#define PEER_ADDR2   "D8:3B:DA:13:0F:7A"
//#define PEER_ADDR3   "34:B7:DA:FB:58:E2"
#endif

#ifndef HIDDEN
// Broadcast uptime every second to connected central (for testing)
// #define TELL_UPTIME
#endif

// The maximum size of the message data.
// Larger amount of data should be split onto chunks before sending
// them to the adapter.
#define MAX_CHUNK 512

// If defined echo all data received from peripheral back to it (for testing)
// #define PEER_ECHO

#ifdef PEER_ECHO
#define PEER_ECHO_QUEUE 16
#endif
