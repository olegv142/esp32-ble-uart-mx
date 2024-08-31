#pragma once

// Here you can include the configuration of your choice
#include "config/default.h"
// .. for example

// The following pair of config files is meant to be used for
// creating point to point link with automatic connect.
// The link is using simplified protocol with only data messages.
// #include "config/simple_master.h"
// #include "config/simple_slave.h"

// The following configurations are meant to be used for testing
// #include "config/echo.h"
// #include "config/echo_master.h"
// #include "config/uptime.h"

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

#ifdef PASSIVE_ONLY
#define AUTOCONNECT
#if defined(PEER_ADDR) || defined(PEER_ADDR1) || defined(PEER_ADDR2) || defined(PEER_ADDR3)
#error "Can't have PEER_ADDR with PASSIVE_ONLY"
#endif
#endif

#if defined(CENTRAL_ONLY) && defined(PASSIVE_ONLY)
#error "CENTRAL_ONLY and PASSIVE_ONLY can't be defined at the same time"
#endif

#if defined(CENTRAL_ONLY) && defined(AUTOCONNECT) && !defined(PEER_ADDR)
#error "PEER_ADDR must be defined with AUTOCONNECT"
#endif

#ifdef SIMPLE_LINK
#if !defined(CENTRAL_ONLY) && !defined(PASSIVE_ONLY)
#error "Either CENTRAL_ONLY or PASSIVE_ONLY should be defined with SIMPLE_LINK option"
#endif
#if defined(CENTRAL_ONLY) && !defined(AUTOCONNECT)
#error "AUTOCONNECT must be defined with CENTRAL_ONLY and SIMPLE_LINK options"
#endif
#undef MAX_PEERS
#define MAX_PEERS 1
#undef STATUS_REPORT_INTERVAL
#define NO_DEBUG
#endif

#ifdef NEO_PIXEL_PIN
#define LED_BRIGHT 10
#define IDLE_RGB       LED_BRIGHT, 0, 0
#define CONNECTING_RGB LED_BRIGHT, LED_BRIGHT, 0
#define ACTIVE_RGB     0, 0, LED_BRIGHT
#define PASSIVE_RGB    0, LED_BRIGHT, 0
#endif

#ifdef HW_UART
// Using hardware UART
#define DataSerial Serial1
#define DATA_UART_NUM UART_NUM_1
#ifndef UART_END
#define UART_BEGIN '\1'
#define UART_END   '\0'
#endif
#else
// Using USB CDC
#define DataSerial Serial
#ifndef UART_END
#define UART_END   '\n'
#endif
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

// The maximum allowed size of the BLE characteristic
#ifndef MAX_SIZE
#define MAX_SIZE 244
#endif

#ifndef EXT_FRAMES
#define MAX_CHUNKS 1
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

// Watchdog timeout. It will restart esp32 if some operation will hung.
#ifndef WDT_TIMEOUT
#define WDT_TIMEOUT 20000 // msec
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
