// Here you can include the configuration of your choice
#include "default_config.h"

// .. for example
// #include "echo_config.h"
// #include "echo_master_config.h"

//
// Miscellaneous settings
//

#ifndef MAX_PEERS
#define MAX_PEERS 4
#elif (MAX_PEERS > 4)
#error "The number of connections exceeded BLE stack implementation limit"
#endif

#ifndef UART_TIMEOUT
#define UART_TIMEOUT 10
#endif

#ifndef CONGESTION_DELAY
#define CONGESTION_DELAY 10
#endif

#ifndef RX_QUEUE
#define RX_QUEUE 64
#endif

#ifndef TX_QUEUE
#define TX_QUEUE 8
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

#ifdef HIDDEN
#define _ACCESS "H"
#elif !defined(WRITABLE)
#define _ACCESS "R"
#else
#define _ACCESS ""
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

#define VARIANT _XDATA _MODE _ACCESS _ECHO _UTIME

#if defined(HIDDEN) && defined(PASSIVE_ONLY)
#error "Defining HIDDEN and PASSIVE_ONLY at the same time makes adapter unusable"
#endif
