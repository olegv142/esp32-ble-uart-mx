// Here you can include the configuration of your choice
#include "default_config.h"

// .. for example
// #include "echo_config.h"
// #include "echo_master_config.h"
// #include "uptime_config.h"

//
// Miscellaneous settings
//

#define SERVICE_UUID           "FFE0"
#define CHARACTERISTIC_UUID_TX "FFE1"

#if (CONFIG_IDF_TARGET_ESP32)
#define MAX_CONNS 2
#else
#define MAX_CONNS 4
#endif

#ifndef MAX_PEERS
#define MAX_PEERS MAX_CONNS
#elif (MAX_PEERS > MAX_CONNS)
#error "The number of connections exceeded BLE stack implementation limit"
#endif

#ifdef NEO_PIXEL_PIN
#define IDLE_RGB 10, 0, 0
#define CONN_RGB 0, 0, 10
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

#ifdef TX_BOOST
#if (CONFIG_IDF_TARGET_ESP32)
#define TX_PW_BOOST ESP_PWR_LVL_P9
#elif (CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2)
#define TX_PW_BOOST ESP_PWR_LVL_P20
#else
#define TX_PW_BOOST ESP_PWR_LVL_P21
#endif
#endif

#ifndef EXT_FRAMES
#define XHDR_SIZE 0
#define CHKSUM_SIZE 0
#define MAX_CHUNK MAX_SIZE
#define MAX_FRAME MAX_CHUNK
#else
#define XHDR_SIZE 1
#define CHKSUM_SIZE 3
#define MAX_CHUNK (MAX_SIZE-XHDR_SIZE-CHKSUM_SIZE)
#define MAX_FRAME (MAX_CHUNK*MAX_CHUNKS)
#ifndef BINARY_DATA_SUPPORT
#define BINARY_DATA_SUPPORT
#endif
#endif

#define UART_RX_BUFFER_SZ ((1+(MAX_FRAME*MAX_BURST+2048)/4096)*4096)
#define UART_TX_BUFFER_SZ (4*UART_RX_BUFFER_SZ)

#ifndef UART_TIMEOUT
#define UART_TIMEOUT 10
#endif

#ifndef CONGESTION_DELAY
#define CONGESTION_DELAY 10
#endif

#ifndef RX_QUEUE
#define RX_QUEUE 32
#endif

#ifndef TX_QUEUE
#define TX_QUEUE 4
#endif

#ifdef CENTRAL_ONLY
#ifndef HIDDEN
#define HIDDEN
#endif
#endif

//
// Build version string
//

#ifdef EXT_FRAMES
#define _XDATA "X"
#elif defined(BINARY_DATA_SUPPORT)
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

#ifdef CENTRAL_ONLY
#define _ADVERT "C"
#elif defined(HIDDEN)
#define _ADVERT "H"
#else
#define _ADVERT ""
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

#define VARIANT _XDATA _MODE _ADVERT _RDONLY _ECHO _UTIME
