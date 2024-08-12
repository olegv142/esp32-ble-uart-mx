"""
BLE multi adapter test script.
Expects serial port name as a parameter.
The script periodically transmits to central a bunch of messages
representing ever incremented number and expects it to echo them back.

You may use the following WebBLE application as central:
https://enspectr.github.io/ble-term/?echo

Author: Oleg Volkov
"""

import sys
sys.path.append('.')
from ble_multi_adapter import MutliAdapter

class EchoTest(MutliAdapter):
	burst_len = 3
	def __init__(self, port):
		super().__init__(port)
		self.last_sn = 0

	def send_msg(self):
		self.last_sn += 1
		self.send_data(str(self.last_sn).encode() + b'#')

	def on_idle(self, version):
		for _ in range(EchoTest.burst_len):
			self.send_msg()

	def on_debug_msg(self, msg):
		print('    ' + msg.decode())

	def on_central_msg(self, msg):
		print('[.] ' + msg.decode())


if __name__ == '__main__':
	with EchoTest(sys.argv[1]) as ad:
		ad.reset()
		while True:
			ad.poll()

