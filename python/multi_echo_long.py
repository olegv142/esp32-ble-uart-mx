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

# The max message length is 512 bytes
# So we have 8 bytes for sequence number plus 4 separator bytes (##)
max_data_len = 500

def random_bytes():
	n = random.randrange(1, max_data_len//2)
	return bytes((random.randrange(ord('0'), ord('z') + 1) for _ in range(n)))

class EchoTest(MutliAdapter):
	burst_len = 16

	def __init__(self, port):
		super().__init__(port)
		self.started = False
		self.last_tx_sn = 0
		self.last_rx_sn = None
		self.msg_buff = None
		self.msg_cnt = 0
		self.errors = 0
		self.lost = 0
		self.dup = 0
		self.reorder = 0

	def send_msg(self):
		b = random_bytes()
		self.last_tx_sn += 1
		self.send_data((b'(%u' % self.last_tx_sn) + b'#' + b + b'#' + b + b')')

	def on_idle(self, version):
		for _ in range(EchoTest.burst_len):
			self.send_msg()

	def on_debug_msg(self, msg):
		print('    ' + msg.decode())

	def msg_received(self):
		m = self.msg_buff[1:-1].split(b'#')
		self.msg_buff = None
		self.msg_cnt += 1
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
			print(' sn: %u %u' % (self.last_rx_sn, sn), end='')
			self.errors += 1
			if sn > self.last_rx_sn + 1:
				self.lost += 1
			elif sn == self.last_rx_sn:
				self.dup += 1
			else:
				self.reorder += 1
		self.last_rx_sn = sn

	def chunk_received(self, msg):
		if not msg:
			# stream start tag
			self.started = False
			self.last_rx_sn = None
			return
		if msg[:1] == b'(':
			self.msg_buff = msg
			self.started = True
		elif self.msg_buff:
			self.msg_buff += msg
		elif self.started:
			print(' bad chunk', end='')
			self.errors += 1
			return
		if msg[-1:] == b')':
			self.msg_received()

	def on_central_msg(self, msg):
		print('[.] %r' % msg, end='')
		self.chunk_received(msg)
		print()

if __name__ == '__main__':
	with EchoTest(sys.argv[1]) as ad:
		ad.reset()
		try:
			while True:
				ad.poll()
		except KeyboardInterrupt:
			print('%u messages, %u errors (%u lost, %u dup, %u reorder), parse errors %u' % (
				ad.msg_cnt, ad.errors, ad.lost, ad.dup, ad.reorder, ad.parse_errors
			))

