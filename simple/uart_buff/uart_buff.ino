#define UART_BAUD_RATE 115200
#define UART_MODE SERIAL_8N1
#define UART_TX_PIN  7
#define UART_CTS_PIN 5
#define UART_TIMEOUT 10
#define UART_BUFFER_SZ 256

/*
Without UART_BUFFER_SZ defined:
..
#63 4.
#64 2.
#65 0

With UART_BUFFER_SZ defined:
..
#13 256.
#14 256.
#15 256
*/

void setup()
{
  Serial.begin(UART_BAUD_RATE);
#ifdef UART_BUFFER_SZ
  Serial1.setTxBufferSize(UART_BUFFER_SZ);
#endif
#ifdef UART_CTS_PIN
  Serial1.begin(UART_BAUD_RATE, UART_MODE, -1, UART_TX_PIN);
  Serial1.setPins(-1, UART_TX_PIN, UART_CTS_PIN, -1);
  Serial1.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS);
#endif
  Serial1.setTimeout(UART_TIMEOUT);
}

void loop()
{
  static int cnt;
  Serial.print('#');
  Serial.print(++cnt);
  Serial.print(' ');
  Serial.print(Serial1.availableForWrite());
  Serial1.print("hi");
  Serial.println('.');
  delay(100);
}
