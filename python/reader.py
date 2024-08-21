"""
BLE multi adapter (ble_uart_mx) host interface

Author: Oleg Volkov
"""

import sys
from serial import Serial, PARITY_EVEN

com = Serial(sys.argv[1], baudrate=115200, parity=PARITY_EVEN, dsrdtr=True, timeout=.1)
while True:
	rd = com.read(512)
	if rd:
		print(repr(rd))
