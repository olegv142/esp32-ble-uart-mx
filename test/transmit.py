"""
The script generate data packets to be transmitted by ble_uart_tx
Should be paired with ble_uart_rx + receive.py

Every message is enclosed between begin / end markers. The message consists of the
sequence number followed by random data repeated twice. So the receiver may validate
data integrity and detect missed messages.

Author: Oleg Volkov
"""

import sys
import time
import random
from serial import Serial
from serial.tools.list_ports import comports

baud_rate = 115200
max_len  = 1024
tx_interval = .5

sn = 1
total_bytes = 0

# Built-in USB serial adapter of ESP32
usb_vid = 0x303a
usb_pid = 0x1001

def find_ports():
	for p in comports():
		if p.vid == usb_vid and p.pid == usb_pid:
			yield p.name

random.seed()

def random_bytes():
	n = random.randrange(1, max_len)
	return bytes((random.randrange(ord('0'), ord('z') + 1) for _ in range(n)))

if len(sys.argv) > 1:
	port = sys.argv[1]
else:
	ports = list(find_ports())
	if len(ports) == 1:
		port = ports[0]
		print ('Using %s' % port)
	elif not ports:
		print ('USB port not found')
		sys.exit(255)
	else:
		print ('Multiple USB ports detected. Please pass target port as parameter.')
		sys.exit(255)

with Serial(port, baudrate=baud_rate, timeout=.1) as com:
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
