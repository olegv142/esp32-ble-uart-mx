"""
BLE multi adapter test script
"""

import sys
sys.path.append('.')
from ble_multi_adapter import MutliAdapter

class TestMutliAdapter(MutliAdapter):
	def __init__(self, port, peers=None):
		super().__init__(port)
		self.peers = peers

	def on_idle(self, version):
		print('Idle, version ' + version.decode())
		if self.peers:
			self.connect(self.peers)

	def on_connecting(self, idx):
		print('Connecting to #%d' % idx)

	def on_connected(self):
		print('Connected')

	def on_debug_msg(self, msg):
		print('    ' + msg.decode())

	def on_central_msg(self, msg):
		print('[.] ' + msg.decode())
		self.send_data(msg) # send it back

	def on_peer_msg(self, idx, msg):
		print(('[%d] ' % idx) + msg.decode())

if __name__ == '__main__':
	port  = sys.argv[1]
	peers = [addr.encode() for addr in sys.argv[2:]]
	with TestMutliAdapter(port, peers) as ad:
		ad.reset()
		while True:
			ad.poll()

