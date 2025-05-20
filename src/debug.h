#pragma once

#include "mx_uart.h"
#include "watchdog.h"

#define LINE_STRING STRINGIZE(__LINE__)

#define BUG() do { fatal("BUG at line " LINE_STRING); } while (0)
#define BUG_ON(cond) do { if (cond) BUG(); } while (0)

struct err_count {
  unsigned cnt;
  unsigned reported;
  err_count() : cnt(0), reported(0) {}
};

static inline void fatal(const char* what)
{
#ifndef NO_DEBUG
  uart_debug_begin();
  uart_print_strz("fatal: ");
  uart_print(what);
  uart_end();
#endif
#ifdef HW_UART // Duplicate msg to other uart
  Serial.print("fatal: ");
  Serial.println(what);
#endif
  delay(100); // give host a chance to read message
  reset_self();
}
