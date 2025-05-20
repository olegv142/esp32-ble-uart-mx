#include "debug.h"
#include "mx_uart.h"
#include "mx_config.h"
#include <Esp.h>

void fatal(const char* what)
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

void reset_self(void)
{
  esp_restart();
}
