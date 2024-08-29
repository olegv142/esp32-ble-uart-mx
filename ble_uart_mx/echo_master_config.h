#pragma once

// Version info printed as part of idle status message
#define VMAJOR    "1"
#define VMINOR    "0"

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

// If defined all debug messages will be suppressed
// #define NO_DEBUG

#ifndef NO_DEBUG
// Enable more debug messages
// #define VERBOSE_DEBUG
#endif

// If HW_UART is defined the hardware serial port will be used for communications.
// Otherwise the USB virtual serial port will be utilized.
// #define HW_UART

#define UART_BAUD_RATE 115200

#ifdef HW_UART
// Use even parity if defined
#define UART_USE_PARITY

#define UART_TX_PIN  7
#define UART_RX_PIN  6
#ifndef UART_USE_PARITY
#define UART_MODE SERIAL_8N1
#else
#define UART_MODE SERIAL_8E1
#endif
// The following defines may be used to configure hardware UART flow control.
// CTS prevents overflow of the host receiving buffer. Be ware that using both
// CTS and RTS flow control may lead to deadlock when both adapter and the host
// are blocked while writing to the serial link with their receiving buffers full.
// The adapter is able to recover from such freeze due to watchdog which reset
// it after 20 seconds of main loop inactivity.
// #define UART_CTS_PIN 5
// RTS prevents overflow of the esp32 receiving buffer.
// Its safe to have it enabled even in case you don't actually use it.
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

// Undefine TX_PW_BOOST to keep default power level
#if (CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2)
#define TX_PW_BOOST ESP_PWR_LVL_P20
#else
#define TX_PW_BOOST ESP_PWR_LVL_P21
#endif

// If defined creating connections to other peripherals will be disabled
// #define PASSIVE_ONLY

#ifdef PASSIVE_ONLY
#define AUTOCONNECT
#else
// If AUTOCONNECT is defined it will connect on startup to the predefined set of peers.
// The host commands will be disabled. One may use AUTOCONNECT with no target peers
// to disable creating connections.
#define AUTOCONNECT
#ifdef AUTOCONNECT
// Peer device address to connect to
#define PEER_ADDR    "DC:54:75:EE:0C:95"
//#define PEER_ADDR    "EC:DA:3B:BB:CE:02"
//#define PEER_ADDR1   "34:B7:DA:F6:44:B2"
//#define PEER_ADDR2   "D8:3B:DA:13:0F:7A"
//#define PEER_ADDR3   "34:B7:DA:FB:58:E2"
#endif
#endif

#ifndef HIDDEN
// Broadcast millisecond uptime to connected central (for testing) if defined
// The value defined is broadcast period in milliseconds
// #define TELL_UPTIME 20
#endif

// The maximum allowed size of the BLE characteristic
#define MAX_SIZE 244

// If defined the binary data transmission is supported (WIP, not implemented yet)
// #define BINARY_DATA_SUPPORT

// If define adapter will transparently use extended data frames with the following features:
// - checksums to detect data lost or corrupted in transit
// - automatic large frames fragmentation
// - binary data support
#define EXT_FRAMES

// The maximum size of the message data
#ifndef EXT_FRAMES
#define XHDR_SIZE 0
#define CHKSUM_SIZE 0
#define MAX_CHUNKS 1
#define MAX_CHUNK MAX_SIZE
#define MAX_FRAME MAX_CHUNK
#else
#define XHDR_SIZE 1
#define CHKSUM_SIZE 3
#define MAX_CHUNKS 9 // Max chunks in single data frame
#define MAX_CHUNK (MAX_SIZE-XHDR_SIZE-CHKSUM_SIZE)
#define MAX_FRAME (MAX_CHUNK*MAX_CHUNKS)
#ifndef BINARY_DATA_SUPPORT
#define BINARY_DATA_SUPPORT
#endif
#endif

#define MAX_BURST 1 // How many messages may be submitted at once
#define UART_RX_BUFFER_SZ ((1+(MAX_FRAME*MAX_BURST+2048)/4096)*4096)
#define UART_TX_BUFFER_SZ (4*UART_RX_BUFFER_SZ)

// If defined echo all data received back to sender (for testing)
#define ECHO

