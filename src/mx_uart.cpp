#include "mx_uart.h"
#include "debug.h"

#ifdef STREAM_TAGS
uint8_t  uart_last_tx_tag = STREAM_TAG_FIRST - 1;
unsigned uart_tx_msg_sz;
#endif

void uart_init(void)
{
  DataSerial.setRxBufferSize(UART_RX_BUFFER_SZ);
  DataSerial.setTxBufferSize(UART_TX_BUFFER_SZ);
#ifdef HW_UART
  DataSerial.begin(UART_BAUD_RATE, UART_MODE, UART_RX_PIN, UART_TX_PIN);
#if defined(UART_CTS_PIN) && defined(UART_RTS_PIN)
  DataSerial.setPins(UART_RX_PIN, UART_TX_PIN, UART_CTS_PIN, UART_RTS_PIN);
  DataSerial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS_RTS);
#else
#ifdef UART_CTS_PIN
  DataSerial.setPins(UART_RX_PIN, UART_TX_PIN, UART_CTS_PIN, -1);
  DataSerial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS);
#endif
#ifdef UART_RTS_PIN
  DataSerial.setPins(UART_RX_PIN, UART_TX_PIN, -1, UART_RTS_PIN);
  DataSerial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_RTS);
#endif
#endif
#endif
  DataSerial.setTimeout(UART_TIMEOUT);
}

void uart_begin(void)
{
#ifdef UART_BEGIN
  DataSerial.print(UART_BEGIN);
#endif
#ifdef STREAM_TAGS
  uint8_t const next_tag = next_stream_tag(uart_last_tx_tag);
  DataSerial.print((char)next_tag);
  uart_last_tx_tag = next_tag;
  uart_tx_msg_sz = 0;
#endif
}

void uart_end(void)
{
#ifdef STREAM_TAGS
  DataSerial.print((char)closing_stream_tag(uart_last_tx_tag, uart_tx_msg_sz));
#endif
  DataSerial.print(UART_END);
}

unsigned chk_error_cnt(struct err_count* e, const char* msg)
{
  unsigned const err_cnt = e->cnt - e->reported;
  if (err_cnt) {
#ifndef NO_DEBUG
    uart_debug_begin();
    uart_print(msg);
    if (err_cnt > 1) {
      uart_print(' ');
      uart_print(err_cnt);
      uart_print_strz(" times");
    }
    uart_end();
#endif
    e->reported = e->cnt;
    return err_cnt;
  }
  return 0;
}

unsigned chk_error_cnt2(struct err_count* e, const char* pref, char tag, const char* suff)
{
  unsigned const err_cnt = e->cnt - e->reported;
  if (err_cnt) {
#ifndef NO_DEBUG
    uart_debug_begin();
    uart_print(pref);
    uart_print(tag);
    uart_print(suff);
    if (err_cnt > 1) {
      uart_print(' ');
      uart_print(err_cnt);
      uart_print_strz(" times");
    }
    uart_end();
#endif
    e->reported = e->cnt;
    return err_cnt;
  }
  return 0;
}
