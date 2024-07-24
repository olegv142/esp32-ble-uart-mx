"""
The script reads and validate data stream received by ble_uart_rx
Should be paired with ble_uart_tx + transmit.py

Note that UART flow control must be enabled in ble_uart_rx
for this test to be run without a lot of errors. To do it one should
define UART_CTS_PIN in ble_uart_rx code and connect it to serial
adapter RTS or DTR input.

Author: Oleg Volkov
"""

import sys
import time
import random
from serial import Serial

baud_rate  = 115200
start_byte = b'\1'
end_byte   = b'\0'
rx_buff    = b''
msg_buff   = b''
wait_start = True

total_bytes = 0
chunk_total = 0
msg_total = 0
msg_bad   = 0
last_sn = None
bad_sn = 0
missed_sn = 0
max_sn_gap = 0

def check_sn(sn):
	global last_sn, bad_sn, missed_sn, max_sn_gap
	if last_sn is None:
		pass
	elif sn <= last_sn:
		bad_sn += 1
	elif sn != last_sn + 1:
		gap = sn - last_sn - 1
		missed_sn += gap
		if gap > max_sn_gap:
			max_sn_gap = gap
	last_sn = sn

def process_msg(msg):
	global msg_total, msg_bad
	msg_total += 1
	s = msg.split(b'#')
	if len(s) != 3 or s[1] != s[2]:
		msg_bad += 1
		return False
	try:
		sn = int(s[0])
	except ValueError:
		msg_bad += 1
		return False
	check_sn(sn)
	return True

def process_chunk(chunk):
	global msg_buff, chunk_total
	msg_buff += chunk
	chunk_total += 1
	first = 0
	while True:
		begin = msg_buff.find(b'(', first)
		if begin < 0:
			break
		end = msg_buff.find(b')', begin + 1)
		if end < 0:
			break
		if process_msg(msg_buff[begin+1:end]):
			print ('*', end='', flush=True)
		else:
			print ('!', end='', flush=True)
		first = end + 1
	msg_buff = msg_buff[first:]

def on_stream_start():
	global msg_buff, wait_start
	msg_buff = b''
	wait_start = False
	print ('.', end='', flush=True)

def process_buffer(rx_bytes):
	global rx_buff, total_bytes
	rx_buff += rx_bytes
	total_bytes += len(rx_bytes)
	first = 0
	while True:
		begin = rx_buff.find(start_byte, first)
		if begin < 0:
			break
		end = rx_buff.find(end_byte, begin + 1)
		if end < 0:
			break
		if rx_buff[end-1:end] == start_byte:
			on_stream_start()
		elif not wait_start:
			process_chunk(rx_buff[begin+1:end])
		first = end + 1
	rx_buff = rx_buff[first:]

def show_stat(elapsed):
	print('\n%u bytes received in %u sec (%u bytes/sec)' % (total_bytes, elapsed, total_bytes/elapsed))
	if chunk_total:
		print ('%u chunks (%u bytes on aver)' % (chunk_total, total_bytes // chunk_total))
	if msg_total:
		print ('%u messages (%u bytes on aver)' % (msg_total, total_bytes // msg_total))
	if msg_bad:
		print ('%u messages were corrupt' % msg_bad)
	if missed_sn:
		print ('%u messages were missed (max %u in a row)' % (missed_sn, max_sn_gap))
	if bad_sn:
		print ('%u messages had bad seq number' % bad_sn)

with Serial(sys.argv[1], baudrate=baud_rate, dsrdtr=True, timeout=1) as com:
	start = time.time()
	if wait_start:
		print ('waiting for stream start tag')
	try:
		while True:
			if rx_bytes := com.read(4096):
				process_buffer(rx_bytes)
	except KeyboardInterrupt:
		show_stat(time.time() - start)
		pass
