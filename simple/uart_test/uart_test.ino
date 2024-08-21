#define UART_BAUD_RATE 115200
#define UART_MODE SERIAL_8E1
#define UART_TX_PIN  7
#define UART_RX_PIN  6
#define UART_CTS_PIN 5
#define UART_RTS_PIN 4

static char tx_buff[2048];

void setup()
{
  Serial.begin(UART_BAUD_RATE);

  Serial1.begin(UART_BAUD_RATE, UART_MODE, UART_RX_PIN, UART_TX_PIN);
  Serial1.setPins(UART_RX_PIN, UART_TX_PIN, UART_CTS_PIN, UART_RTS_PIN);
  Serial1.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS_RTS);

  for (int i = 0; i < sizeof(tx_buff); ++i) {
    tx_buff[i] = '0' + (i % 64);
  }
}

void loop()
{
  static unsigned loop_cnt;
  Serial1.write(tx_buff, random(1, sizeof(tx_buff) + 1));
  Serial.print('.');
  if (++loop_cnt % 128 == 0)
    Serial.println();
  delay(50);
}

