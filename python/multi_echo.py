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
	burst_len = 8
	def __init__(self, port):
		super().__init__(port)
		self.last_tx_sn = 0
		self.last_rx_sn = None
		self.errors = 0

	def send_msg(self):
		self.last_tx_sn += 1
		self.send_data(str(self.last_tx_sn).encode() + b'#')

	def on_idle(self, version):
		for _ in range(EchoTest.burst_len):
			self.send_msg()

	def on_debug_msg(self, msg):
		print('    ' + msg.decode())

	def on_central_msg(self, msg):
		print('[.] ' + msg.decode(), end='')
		if not msg:
			# stream start tag
			self.last_rx_sn = None
			return
		sn = int(msg[:-1])
		if self.last_rx_sn is not None and sn != self.last_rx_sn + 1:
			print(' %u %u' % (self.last_rx_sn, sn))
			self.errors += 1
		else:
			print()
		self.last_rx_sn = sn

if __name__ == '__main__':
	with EchoTest(sys.argv[1]) as ad:
		ad.reset()
		try:
			while True:
				ad.poll()
		except KeyboardInterrupt:
			print('%u errors' % ad.errors)

