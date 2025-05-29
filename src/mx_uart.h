#pragma once

/*
 * UART related helpers
 */

#include "mx_config.h"
#include "stream_tags.h"
#include "util.h"

#ifdef STREAM_TAGS
extern uint8_t  uart_last_tx_tag;
extern unsigned uart_tx_msg_sz;
#endif

void uart_init(void);
void uart_begin(void);
void uart_end(void);

static inline void uart_write(const char* data, size_t sz)
{
  DataSerial.write(data, sz);
#ifdef STREAM_TAGS
  uart_tx_msg_sz += sz;
#endif
}

static inline void uart_print(char c)
{
  uart_write(&c, 1);
}

static inline void uart_debug_begin(void)
{
  uart_begin();
  uart_print('-');
}

static inline void uart_print(const char* str)
{
  uart_write(str, strlen(str));
}

#define uart_print_strz(s) uart_write(s, STRZ_LEN(s))

static inline void uart_print(String const& str)
{
  uart_write(str.c_str(), str.length());
}

static inline void uart_print(int val)
{
  String s(val);
  uart_write(s.c_str(), s.length());
}

static inline void uart_print(unsigned val)
{
  String s(val);
  uart_write(s.c_str(), s.length());
}

static inline void uart_print_hex(unsigned val)
{
  String s(val, HEX);
  uart_write(s.c_str(), s.length());
}

static inline void uart_print(unsigned long val)
{
  String s(val);
  uart_write(s.c_str(), s.length());
}

static inline void uart_print_hex(unsigned long val)
{
  String s(val, HEX);
  uart_write(s.c_str(), s.length());
}

static inline void print_hex_byte(uint8_t v, char buff[3])
{
  snprintf(buff, 3, "%02x", v);
}

static inline void uart_print_hex_byte(uint8_t v)
{
  char buff[3];
  print_hex_byte(v, buff);
  uart_write(buff, 2);
}

static inline void debug_msg(const char* msg)
{
#ifndef NO_DEBUG
  uart_debug_begin();
  uart_print(msg);
  uart_end();
#endif
}

#ifndef NO_DEBUG
#define debug_strz(msg) do {uart_debug_begin(); uart_print_strz(msg); uart_end();} while (0)
#else
#define debug_strz(msg) do {} while (0)
#endif

/*
 * Print error counter to serial port
 */

struct err_count;

unsigned chk_error_cnt(struct err_count* e, const char* msg);
unsigned chk_error_cnt2(struct err_count* e, const char* pref, char tag, const char* suff);
