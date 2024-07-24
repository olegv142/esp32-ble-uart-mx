"""
The script generate data packets to be transmitted by ble_uart_tx
Should be paired with ble_uart_rx + receive.py

Author: Oleg Volkov
"""

import sys
import time
import random
from serial import Serial

baud_rate = 115200
max_len  = 1024
tx_interval = .5

sn = 1
total_bytes = 0

random.seed()

def random_bytes():
	n = random.randrange(1, max_len)
	return bytes((random.randrange(ord('0'), ord('z') + 1) for _ in range(n)))

with Serial(sys.argv[1], baudrate=baud_rate, timeout=.1) as com:
	start = time.time()
	try:		
		while True:
			b = random_bytes()
			com.write(msg := (b'(%d' % sn) + b'#' + b + b'#' + b + b')')
			sn += 1
			total_bytes += len(msg)
			time.sleep(tx_interval)
			print ('*', end='', flush=True)
	except KeyboardInterrupt:
		now = time.time()
		elapsed = now - start
		print('\n%u bytes transferred in %u sec (%u bytes/sec)' % (total_bytes, elapsed, total_bytes/elapsed))
		pass
