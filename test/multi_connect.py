"""
BLE multi adapter host interface
"""

import sys
from serial import Serial

class MutliAdapter:
	"""BLE multi-adapter interface class"""
	baud_rate  = 115200
	start_byte = b'\1'
	end_byte   = b'\0'

	def __init__(self, port, timeout=1):
		self.port    = port
		self.com     = None
		self.timeout = timeout
		self.rx_buff = b''

	def __enter__(self):
		self.open()
		return self

	def __exit__(self, ex_type, ex_value, traceback):
		self.close()

	def open(self):
		assert self.com is None
		self.com = Serial(self.port, baudrate=MutliAdapter.baud_rate, timeout=self.timeout)

	def close(self):
		if self.com:
			self.com.close()
			self.com = None

	def send_cmd(self, cmd):
		"""Send command to the adapter"""
		self.com.write(MutliAdapter.start_byte + b'#' + cmd + MutliAdapter.end_byte)

	def reset(self):
		self.send_cmd(b'R')

	def connect(self, peers):
		self.send_cmd(b'C' + b' '.join(peers))

	def send_data(self, data):
		"""Send data to connected central"""
		self.com.write(MutliAdapter.start_byte + b'>' + data + MutliAdapter.end_byte)

	def poll(self):
		if rx_bytes := self.com.read(4096):
			self.process_rx(rx_bytes)

	def process_rx(self, rx_bytes):
		self.rx_buff += rx_bytes
		first = 0
		while True:
			begin = self.rx_buff.find(MutliAdapter.start_byte, first)
			if begin < 0:
				break
			end = self.rx_buff.find(MutliAdapter.end_byte, begin + 1)
			if end < 0:
				break
			self.process_msg(self.rx_buff[begin+1:end])
			first = end + 1
		self.rx_buff = self.rx_buff[first:]

	def process_msg(self, msg):
		tag = msg[0:1]
		if tag == b':':
			self.on_status_msg(msg[1:])
		elif tag == b'-':
			self.on_debug_msg(msg[1:])
		elif tag == b'<':
			self.on_central_msg(msg[1:])
		else:
			self.on_peer_msg(msg[0] - b'0'[0], msg[1:])

	def on_status_msg(self, msg):
		tag = msg[0:1]
		if tag == b'I':
			self.on_idle(msg[1:].strip())
		elif tag == b'C':
			self.on_connecting(msg[1] - b'0'[0])
		elif tag == b'D':
			self.on_connected()

	def on_idle(self, version):
		pass

	def on_connecting(self, idx):
		pass

	def on_connected(self):
		pass

	def on_debug_msg(self, msg):
		pass

	def on_central_msg(self, msg):
		pass

	def on_peer_msg(self, idx, msg):
		pass

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

