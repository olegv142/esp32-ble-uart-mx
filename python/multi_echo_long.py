"""
BLE multi adapter test script.
Expects serial port name as a parameter.
The script periodically transmits to central a message
with ever incremented sequence number followed by random data
and expects it to echo them back.

Author: Oleg Volkov
"""

import sys
import random
sys.path.append('.')
from ble_multi_adapter import MutliAdapter

max_len = 256

def random_bytes():
	n = random.randrange(1, max_len)
	return bytes((random.randrange(ord('0'), ord('z') + 1) for _ in range(n)))

class EchoTest(MutliAdapter):
	def __init__(self, port):
		super().__init__(port)
		self.last_tx_sn = 0
		self.last_rx_sn = None
		self.rx_buff = b''
		self.msg_cnt = 0
		self.errors = 0

	def send_msg(self):
		b = random_bytes()
		self.last_tx_sn += 1
		self.send_data((b'(%u' % self.last_tx_sn) + b'#' + b + b'#' + b + b')')

	def on_idle(self, version):
		self.send_msg()

	def on_debug_msg(self, msg):
		print('    ' + msg.decode())

	def msg_received(self, msg):
		self.msg_cnt += 1
		m = msg[1:-1].split(b'#')
		if len(m) != 3:
			print(' bad message', end='')
			self.errors += 1
			return
		try:
			sn = int(m[0])
		except ValueError:
			print(' bad sn', end='')
			self.errors += 1
			return
		if m[1] != m[2]:
			print(' corrupt message', end='')
			self.errors += 1
		if self.last_rx_sn is not None and sn != self.last_rx_sn + 1:
			print(' %u %u' % (self.last_rx_sn, sn), end='')
			self.errors += 1
		self.last_rx_sn = sn

	def chunk_received(self, msg):
		if not msg:
			# stream start tag
			self.last_rx_sn = None
			return
		if msg[:1] == b'(':
			self.rx_buff = msg
		elif self.rx_buff:
			self.rx_buff += msg
		if msg[-1:] == b')':
			self.msg_received(self.rx_buff)
			self.rx_buff = b''

	def on_central_msg(self, msg):
		print('[.] ' + msg.decode(), end='')
		self.chunk_received(msg)
		print()

if __name__ == '__main__':
	with EchoTest(sys.argv[1]) as ad:
		ad.reset()
		try:
			while True:
				ad.poll()
		except KeyboardInterrupt:
			print('%u messages, %u errors' % (ad.msg_cnt, ad.errors))

