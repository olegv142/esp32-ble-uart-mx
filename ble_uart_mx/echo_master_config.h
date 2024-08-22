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
// The following define may be used to configure hardware UART flow control.
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

// If defined reset itself on peripheral disconnection instead of reconnecting
#define RESET_ON_DISCONNECT

// Undefine to keep default power level
#define TX_PW_BOOST ESP_PWR_LVL_P21

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
//#define PEER_ADDR    "EC:DA:3B:BB:CE:02"
//#define PEER_ADDR1   "34:B7:DA:F6:44:B2"
#define PEER_ADDR2   "D8:3B:DA:13:0F:7A"
//#define PEER_ADDR3   "34:B7:DA:FB:58:E2"
#endif
#endif

#ifndef HIDDEN
// Broadcast uptime every second to connected central (for testing)
// #define TELL_UPTIME
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
#define MAX_CHUNK MAX_SIZE
#define MAX_FRAME MAX_CHUNK
#else
#define XHDR_SIZE 1
#define CHKSUM_SIZE 3
#define MAX_CHUNK (MAX_SIZE-XHDR_SIZE-CHKSUM_SIZE)
#define MAX_CHUNKS 9 // Max chunks in single data frame
#define MAX_FRAME (MAX_CHUNK*MAX_CHUNKS)
#ifndef BINARY_DATA_SUPPORT
#define BINARY_DATA_SUPPORT
#endif
#endif

#define UART_RX_BUFFER_SZ 1024
#define UART_TX_BUFFER_SZ 4096

// If defined echo all data received back to sender (for testing)
#define ECHO

// Receive queue length
#define RX_QUEUE 64

//
// Build version string
//

#ifdef EXT_FRAMES
#define _XDATA "X"
#elif BINARY_DATA_SUPPORT
#define _XDATA "B"
#else
#define _XDATA "T"
#endif

#ifdef PASSIVE_ONLY
#define _MODE "P"
#elif defined(AUTOCONNECT)
#define _MODE "A"
#else
#define _MODE ""
#endif

#ifdef HIDDEN
#define _HIDDEN "H"
#else
#define _HIDDEN ""
#endif

#ifndef WRITABLE
#define _RDONLY "R"
#else
#define _RDONLY ""
#endif

#ifdef ECHO
#define _ECHO "e"
#else
#define _ECHO ""
#endif

#ifdef TELL_UPTIME
#define _UTIME "u"
#else
#define _UTIME ""
#endif

#define VARIANT _XDATA _MODE _HIDDEN _RDONLY _ECHO _UTIME
