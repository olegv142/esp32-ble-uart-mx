"""
BLE multi adapter (ble_uart_mx) host interface

Author: Oleg Volkov
"""

import sys
import time
from serial import Serial, PARITY_EVEN

chunk_sz = 256
validate = True
total_sz = 0
next_sn = None
err_cnt = 0
delay = .01

def validate_data(rd):
	global next_sn, err_cnt
	for b in rd:
		sn = b - ord('0')
		if next_sn is not None and sn != next_sn:
			err_cnt += 1
		next_sn = (sn + 1) % 64


com = Serial(sys.argv[1], baudrate=921600, parity=PARITY_EVEN, rtscts=True, timeout=.1)
try:
	while True:
		rd = com.read(chunk_sz)
		total_sz += len(rd)
		if rd:
			print('.', end='', flush=True)
		if validate:
			validate_data(rd)
		if delay:
			time.sleep(delay)
except KeyboardInterrupt:
	print('\n%u bytes received' % total_sz)
	if validate:
		print('%u errors' % err_cnt)
