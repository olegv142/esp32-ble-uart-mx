"""
BLE multi adapter test script.
Expects serial port name as a parameter.
The script periodically transmits to central a bunch of messages
representing ever incremented number and expects it to echo them back.

Author: Oleg Volkov
"""

import sys
sys.path.append('.')
from ble_multi_adapter import MutliAdapter

class EchoTest(MutliAdapter):
	burst_len = 16

	def __init__(self, port):
		super().__init__(port)
		self.last_tx_sn = 0
		self.last_rx_sn = None
		self.msg_cnt = 0
		self.errors = 0
		self.lost = 0
		self.dup = 0
		self.reorder = 0
		self.corrupt = 0

	def send_msg(self):
		self.last_tx_sn += 1
		self.send_data(b'%u#' % self.last_tx_sn)

	def on_idle(self, hidden, version):
		for _ in range(EchoTest.burst_len):
			self.send_msg()

	def on_debug_msg(self, msg):
		print('    ' + msg.decode())

	def msg_received(self, msg):
		if not msg:
			# stream start tag
			self.last_rx_sn = None
			return
		self.msg_cnt += 1
		try:
			sn = int(msg[:-1])
		except ValueError:
			print(' bad message', end='')
			self.errors += 1
			self.corrupt += 1
			return
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

	def on_central_msg(self, msg):
		print('[.] %r' % msg, end='')
		self.msg_received(msg)
		print()

if __name__ == '__main__':
	with EchoTest(sys.argv[1]) as ad:
		ad.reset()
		try:
			while True:
				ad.communicate()
		except KeyboardInterrupt:
			print('%u messages, %u errors (%u lost, %u dup, %u reorder, %u corrupt), parse errors %u' % (
				ad.msg_cnt, ad.errors, ad.lost, ad.dup, ad.reorder, ad.corrupt, ad.parse_errors
			))

