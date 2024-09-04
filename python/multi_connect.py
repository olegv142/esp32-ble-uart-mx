"""
BLE multi adapter test script.
Expects serial port name as a parameter optionally
followed by peer device addresses to connect to.

Author: Oleg Volkov
"""

import sys
sys.path.append('.')
from ble_multi_adapter import MutliAdapter, MutliAdapterUSB

# If True use hardware UART else USB CDC
hw_uart = False

class TestMutliAdapter(MutliAdapter if hw_uart else MutliAdapterUSB):
	def __init__(self, port, peers=None):
		super().__init__(port)
		self.peers = peers

	def on_idle(self, hidden, version):
		print(('Idle hidden, version ' if hidden else 'Idle, version ') + version.decode())
		if self.peers:
			self.connect(self.peers)

	def on_connecting(self, idx):
		print('Connecting to #%d' % idx)

	def on_connected(self, hidden):
		print('Connected hidden' if hidden else 'Connected')

	def on_debug_msg(self, msg):
		print('    ' + msg.decode())

	def on_central_msg(self, msg):
		print('[.] %r' % msg)

	def on_peer_msg(self, idx, msg):
		print('[%d] %r' % (idx, msg))

if __name__ == '__main__':
	port  = sys.argv[1]
	peers = [addr.encode() for addr in sys.argv[2:]]
	with TestMutliAdapter(port, peers) as ad:
		ad.reset()
		try:
			while True:
				ad.communicate()
		except KeyboardInterrupt:
			print('%u parse errors' % ad.parse_errors)
