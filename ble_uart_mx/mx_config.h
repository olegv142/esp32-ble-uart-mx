// Here you can include the configuration of your choice
#include "default_config.h"

// .. for example
// #include "echo_config.h"
// #include "echo_master_config.h"

//
// Miscellaneous settings
//

#ifndef MAX_PEERS
#define MAX_PEERS 8
#endif

#ifndef UART_TIMEOUT
#define UART_TIMEOUT 10
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

#ifdef FAST_WRITES
#define _FAST "F"
#else
#define _FAST ""
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

#define STR_(arg) #arg
#define STR(arg) STR_(arg)

#define VARIANT STR(MAX_PEERS) _XDATA _FAST _MODE _ACCESS _ECHO _UTIME

#if defined(HIDDEN) && defined(PASSIVE_ONLY)
#error "Defining HIDDEN and PASSIVE_ONLY at the same time makes adapter unusable"
#endif

#if !defined(HIDDEN) && !defined(PASSIVE_ONLY)
#warning "Dual role adapter is not recommended for production. Define either HIDDEN or PASSIVE_ONLY to restrict it to one role."
#endif
