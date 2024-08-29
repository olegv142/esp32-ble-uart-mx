"""
BLE multi adapter test script.
Expects serial port name as a parameter followed by the peer device addresses to connect to.
The script echo all data received from any connected peer back to it.

Author: Oleg Volkov
"""

import sys
sys.path.append('.')
from ble_multi_adapter import MutliAdapter

class Echo(MutliAdapter):
	def __init__(self, port, peers):
		super().__init__(port)
		self.peers = peers

	def on_idle(self, hidden, version):
		print('Idle, version ' + version.decode())
		self.connect(self.peers)

	def on_connecting(self, idx):
		print('Connecting to #%d' % idx)

	def on_debug_msg(self, msg):
		print('    ' + msg.decode())

	def on_peer_msg(self, idx, msg):
		print(('[%d] ' % idx) + msg.decode())
		if msg:
			self.send_data_to(idx, msg)

if __name__ == '__main__':
	port  = sys.argv[1]
	peers = [addr.encode() for addr in sys.argv[2:]]
	with Echo(port, peers) as ad:
		ad.reset()
		while True:
			ad.communicate()

