"""
BLE multi adapter test script.
Expects serial port name as a parameter.
The script echo all data received from central back to it.

Author: Oleg Volkov
"""

import sys
sys.path.append('.')
from ble_multi_adapter import MutliAdapter

class Echo(MutliAdapter):
	def __init__(self, port):
		super().__init__(port)

	def on_debug_msg(self, msg):
		print('    ' + msg.decode())

	def on_central_msg(self, msg):
		print('[.] ' + msg.decode())
		if msg:
			self.send_data(msg)

if __name__ == '__main__':
	with Echo(sys.argv[1]) as ad:
		ad.reset()
		while True:
			ad.communicate()

