"""
BLE multi adapter test script.
Expects serial port name as a parameter.
The script periodically transmits to central a message
with ever incremented sequence number followed by random data
and expects it to echo them back.

Author: Oleg Volkov
"""

import sys
import time
import random
from collections import defaultdict

sys.path.append('.')
from ble_multi_adapter import MutliAdapter

# If false all messages will have maximum allowed size
random_size = True

# The number of messages that should be sent at once
tx_burst = 5

# Use binary data or text
binary_data = True

# Limit maximum message size
max_size = None

if binary_data:
	data_delimiter = b'\xff'
else:
	data_delimiter = b'#'

def random_bytes(len):
	return bytes((
		 random.randrange(0, 255) if binary_data else random.randrange(ord('0'), ord('z')+1) for _ in range(len)
	))

class TestStream:
	def __init__(self):
		self.created_ts = time.time()
		self.is_started = False
		self.last_tx_sn = 0
		self.last_rx_sn = None
		self.msg_cnt = 0
		self.byte_cnt = 0
		self.conn_cnt = 0
		self.valid_cnt = 0
		self.lost_cnt = 0
		self.dup_cnt = 0
		self.reorder_cnt = 0
		self.corrupt_cnt = 0

	def mk_msg(self, max_frame):
		if max_size is not None and max_frame > max_size:
			max_frame = max_size
		self.last_tx_sn += 1
		sn = b'%u' % self.last_tx_sn
		max_data_size = (max_frame - len(sn) - 4) // 2 # takes into account separators (sn#data#data)
		data = random_bytes(max_data_size if not random_size else random.randrange(1, max_data_size+1))
		return b'(' + sn + data_delimiter + data + data_delimiter + data + b')'

	def msg_received(self, msg):
		if msg[:1] != b'(' or msg[-1:] != b')':
			print(' corrupt brackets', end='')
			self.corrupt_cnt += 1
			return False
		m = msg[1:-1].split(data_delimiter)
		self.byte_cnt += len(msg)
		if len(m) != 3:
			print(' corrupt delimiters', end='')
			self.corrupt_cnt += 1
			return False
		try:
			sn = int(m[0])
		except ValueError:
			print(' corrupt sn', end='')
			self.corrupt_cnt += 1
			return False
		if not (valid := (m[1] == m[2])):
			print(' corrupt data', end='')
			self.corrupt_cnt += 1
		if self.last_rx_sn is not None and sn != self.last_rx_sn + 1:
			print(' bad sn: %u %u' % (self.last_rx_sn, sn), end='')
			if sn > self.last_rx_sn + 1:
				self.lost_cnt += sn - self.last_rx_sn - 1
			elif sn == self.last_rx_sn:
				self.dup_cnt += 1
				return False
			else:
				self.reorder_cnt += 1
				return False
		self.last_rx_sn = sn
		return valid

	def chunk_received(self, msg):
		if not msg:
			# stream start tag
			self.is_started = True
			self.last_rx_sn = None
			self.conn_cnt += 1
			return
		if not self.is_started:
			return
		self.msg_cnt += 1
		if self.msg_received(msg):
			self.valid_cnt += 1

	def print_stat(self, prefix):
		print('%sconnected %u time(s)' % (prefix, self.conn_cnt))
		print('%s%u msgs sent, %u received (%u bytes, %u/sec)' % (prefix, self.last_tx_sn, self.msg_cnt, self.byte_cnt, self.byte_cnt / (time.time() - self.created_ts)))
		print('%s%u valid, %u lost, %u dup, %u reorder, %u corrupt)' % (
				prefix, self.valid_cnt, self.lost_cnt, self.dup_cnt, self.reorder_cnt, self.corrupt_cnt
			))

class EchoTest(MutliAdapter):

	def __init__(self, port, targets = None, active = None, peripheral = None):
		super().__init__(port)
		ntargets = len(targets)
		if active is None:
			active = range(ntargets)
		assert len(active) <= ntargets
		if peripheral is None:
			peripheral = not targets
		self.targets = targets
		self.active = active
		self.peripheral = peripheral
		self.max_frame = None
		self.dbg_msgs = defaultdict(int)
		self.pstream = TestStream() if peripheral else None
		self.tstream = [TestStream() for _ in targets]

	def send_msg(self, connected):
		if self.pstream is not None:
			self.send_data(self.pstream.mk_msg(self.max_frame), binary_data)
		if connected:
			for i in self.active:
				self.send_data_to(i, self.tstream[i].mk_msg(self.max_frame), binary_data)

	def send_msgs(self, connected):
		if self.max_frame is None:
			return
		for _ in range(tx_burst):
			if not self.is_congested():
				self.send_msg(connected)

	def on_idle(self, version):
		try:
			v = version.split(b'-')
			self.max_frame = int(v[1])
		except:
			print('bad version: %s', version)
			return
		self.send_msgs(False)
		if self.targets:
			print('Idle, version ' + version.decode())
			self.connect(self.targets)

	def on_connected(self):
		self.send_msgs(True)

	def on_debug_msg(self, msg):
		str = msg.decode()
		print('    ' + str)
		self.dbg_msgs[str] += 1

	def on_central_msg(self, msg):
		print('[.] %r' % msg, end='')
		if self.pstream:
			self.pstream.chunk_received(msg)
		print()

	def on_peer_msg(self, idx, msg):
		print('[%d] %r' % (idx, msg), end='')
		if 0 <= idx < len(self.tstream):
			self.tstream[idx].chunk_received(msg)
		print()

	def print_stat(self):
		hline = '-' * 64
		print(hline)
		if self.pstream:
			self.pstream.print_stat("[.] ")
			print(hline)
		for i in range(len(targets)):
			self.tstream[i].print_stat("[%d] " % i)
			print(hline)
		print('parse errors: %u' % self.parse_errors)
		print('debug messages:')
		for msg, cnt in ad.dbg_msgs.items():
			print('%u: %s' % (cnt, msg))

if __name__ == '__main__':
	start = time.time()	
	if first_only := '--first-only' in sys.argv:
		sys.argv.remove('--first-only')
	if last_only := '--last-only' in sys.argv:
		sys.argv.remove('--last-only')
	if peripheral := '--peripheral' in sys.argv:
		sys.argv.remove('--peripheral')
	targets = [addr.encode() for addr in sys.argv[2:]]
	active = [0] if first_only else [len(targets)-1] if last_only else None
	with EchoTest(sys.argv[1], targets, active, True if peripheral else None) as ad:
		ad.reset()
		try:
			while True:
				ad.communicate()
		except KeyboardInterrupt:
			ad.print_stat()
