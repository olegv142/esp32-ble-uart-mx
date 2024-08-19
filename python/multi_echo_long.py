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
from collections import defaultdict

sys.path.append('.')
from ble_multi_adapter import MutliAdapter

# If false all messages will have maximum allowed size
random_size = True

# Use the following to decide how mane messages should be sent at once
max_burst = 5000

# Use binary data or text
binary_data = True

if binary_data:
	data_delimiter = b'\xff'
else:
	data_delimiter = b'#'

def random_bytes(len):
	return bytes((
		 random.randrange(0, 255) if binary_data else random.randrange(ord('0'), ord('z')+1) for _ in range(len)
	))

class EchoTest(MutliAdapter):

	def __init__(self, port, target=None):
		super().__init__(port)
		self.target = target
		self.max_frame = None
		self.started = False
		self.last_tx_sn = 0
		self.last_rx_sn = None
		self.msg_cnt = 0
		self.conn_cnt = 0
		self.errors = 0
		self.lost = 0
		self.dup = 0
		self.reorder = 0
		self.corrupt = 0
		self.dbg_msgs = defaultdict(int)

	def send_msg(self):
		self.last_tx_sn += 1
		sn = b'%u' % self.last_tx_sn
		max_data_size = (self.max_frame - len(sn) - 4) // 2 # takes into account separators (sn#data#data)
		data = random_bytes(max_data_size if not random_size else random.randrange(1, max_data_size+1))
		msg = b'(' + sn + data_delimiter + data + data_delimiter + data + b')'
		if self.target is None:
			self.send_data(msg, binary_data)
		else:
			self.send_data_to(0, msg, binary_data)

	def send_msgs(self):
		if self.max_frame is None:
			return
		for _ in range(max_burst // self.max_frame):
			self.send_msg()

	def on_idle(self, version):
		try:
			v = version.split(b'-')
			self.max_frame = int(v[1])
		except:
			print('bad version: %s', version)
			return
		if self.target is None:
			self.send_msgs()
		else:
			print('Idle, version ' + version.decode())
			self.connect([self.target])

	def on_connected(self):
		self.send_msgs()

	def on_debug_msg(self, msg):
		str = msg.decode()
		print('    ' + str)
		self.dbg_msgs[str] += 1

	def msg_received(self, msg):
		m = msg[1:-1].split(data_delimiter)
		self.msg_cnt += 1
		if len(m) != 3:
			print(' corrupt delimiters', end='')
			self.errors += 1
			self.corrupt += 1
			return
		try:
			sn = int(m[0])
		except ValueError:
			print(' corrupt sn', end='')
			self.errors += 1
			self.corrupt += 1
			return
		if m[1] != m[2]:
			print(' corrupt data', end='')
			self.errors += 1
			self.corrupt += 1
		if self.last_rx_sn is not None and sn != self.last_rx_sn + 1:
			print(' bad sn: %u %u' % (self.last_rx_sn, sn), end='')
			self.errors += 1
			if sn > self.last_rx_sn + 1:
				self.lost += 1
			elif sn == self.last_rx_sn:
				self.dup += 1
			else:
				self.reorder += 1
				return
		self.last_rx_sn = sn

	def chunk_received(self, msg):
		if not msg:
			# stream start tag
			self.started = True
			self.last_rx_sn = None
			self.conn_cnt += 1
			return
		if not self.started:
			return
		if msg[:1] == b'(' and msg[-1:] == b')':
			self.msg_received(msg)
		else:
			print(' corrupt brackets', end='')
			self.errors += 1
			self.corrupt += 1

	def on_central_msg(self, msg):
		print('[.] %r' % msg, end='')
		if self.target is None:
			self.chunk_received(msg)
		print()

	def on_peer_msg(self, idx, msg):
		print('[%d] %r' % (idx, msg), end='')
		if self.target is not None:
			self.chunk_received(msg)
		print()

if __name__ == '__main__':
	with EchoTest(sys.argv[1], sys.argv[2].encode() if len(sys.argv) > 2 else None) as ad:
		ad.reset()
		try:
			while True:
				ad.poll()
		except KeyboardInterrupt:
			print('connected %u time(s)' % ad.conn_cnt)
			print('%u messages, %u errors (%u lost, %u dup, %u reorder, %u corrupt), parse errors %u' % (
				ad.msg_cnt, ad.errors, ad.lost, ad.dup, ad.reorder, ad.corrupt, ad.parse_errors
			))
			print('debug messages:')
			for msg, cnt in ad.dbg_msgs.items():
				print('%u: %s' % (cnt, msg))

