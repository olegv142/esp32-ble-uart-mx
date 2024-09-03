"""
BLE multi adapter (ble_uart_mx) host interface

Author: Oleg Volkov
"""

import sys
import base64
import binascii
from serial import Serial, PARITY_NONE, PARITY_EVEN

STREAM_TAG_FIRST = ord('@')
STREAM_TAGS_MOD = 191

class AdapterConnection:
	baud_rate   = 115200
	use_parity  = True
	parity      = PARITY_EVEN if use_parity else PARITY_NONE
	start_byte  = b'\1'
	end_byte    = b'\0'
	use_tags    = True
	opt_tags    = True 
	rtscts      = True
	timeout     = .01
	rx_buf_size = 4*4096
	tx_buf_size = 4096
	congest_thr = 16

	def __init__(self, port):
		self.port    = port
		self.com     = None
		self.rx_buff = b''
		self.parse_errors = 0
		self.lost_frames = 0
		self.tx_queue = []
		self.last_tx_tag = STREAM_TAG_FIRST - 1
		self.last_rx_tag = 0

	def __enter__(self):
		self.open()
		return self

	def __exit__(self, ex_type, ex_value, traceback):
		self.close()

	def open(self):
		assert self.com is None
		self.com = Serial(self.port,
			baudrate=self.baud_rate,
			parity=self.parity,
			rtscts=self.rtscts,
			timeout=self.timeout
		)
		self.com.set_buffer_size(
			rx_size = self.rx_buf_size,
			tx_size = self.tx_buf_size
		)

	def close(self):
		if self.com:
			self.com.close()
			self.com = None

	def is_congested(self):
		return len(self.tx_queue) > self.congest_thr

	@staticmethod
	def is_stream_tag(tag):
		return STREAM_TAG_FIRST <= tag < STREAM_TAG_FIRST + STREAM_TAGS_MOD

	@staticmethod
	def get_next_tag_(tag):
		tag += 1
		if tag >= STREAM_TAG_FIRST + STREAM_TAGS_MOD:
			tag = STREAM_TAG_FIRST
		return tag

	def get_next_tag(self):
		self.last_tx_tag = self.get_next_tag_(self.last_tx_tag)
		return self.last_tx_tag

	@staticmethod
	def get_closing_tag(open_tag, msg_len):
		return STREAM_TAG_FIRST + (open_tag - STREAM_TAG_FIRST + msg_len) % STREAM_TAGS_MOD;

	def write_msg(self, msg):
		"""Write message to the adapter"""
		if self.use_tags:
			topen  = self.get_next_tag()
			tclose = self.get_closing_tag(topen, len(msg))
			wire_msg = b''.join((self.start_byte, 
					bytes([topen]),
					msg,
					bytes([tclose]),
					self.end_byte
				))
		else:
			wire_msg = b''.join((self.start_byte,
					msg,
					self.end_byte
				))
		self.com.write(wire_msg)

	def receive(self):
		"""Receive from adapter"""
		while rx_bytes := self.com.read(4096):
			self.process_rx(rx_bytes)

	def communicate(self):
		"""Communicate with adapter"""
		self.receive()
		tx_queue = self.tx_queue
		self.tx_queue = []
		for msg in tx_queue:
			self.write_msg(msg)
			self.receive()

	def submit_msg(self, msg):
		self.tx_queue.append(msg)

	def reset(self):
		self.tx_queue = []
		self.last_rx_tag = 0

	def process_rx(self, rx_bytes):
		self.rx_buff += rx_bytes
		first = 0
		begin = self.rx_buff.find(self.start_byte, 0)
		while begin >= 0:
			end = self.rx_buff.find(self.end_byte, begin + 1)
			if end < 0:
				break
			while True:
				next_begin = self.rx_buff.find(self.start_byte, begin + 1)
				if next_begin >= 0 and next_begin < end:
					begin = next_begin
					self.parse_errors += 1
				else:
					break
			self.process_frame(self.rx_buff[begin+1:end])
			first = end + 1
			begin = next_begin
		self.rx_buff = self.rx_buff[first:]

	def process_frame(self, msg):
		if not len(msg):
			self.parse_errors += 1
			return
		topen = msg[0]
		if not self.is_stream_tag(topen):
			if self.opt_tags:
				self.process_msg(msg)
				return
			else:
				self.parse_errors += 1
				return
		if len(msg) <= 2:
			self.parse_errors += 1
			return
		if self.last_rx_tag:
			next_rx_tag = self.get_next_tag_(self.last_rx_tag)
			if topen != next_rx_tag:
				self.lost_frames += topen - next_rx_tag if topen > next_rx_tag else \
									topen + STREAM_TAGS_MOD - next_rx_tag
		self.last_rx_tag = topen
		if msg[-1] != self.get_closing_tag(topen, len(msg) - 2):
			self.parse_errors += 1
			return
		self.process_msg(msg[1:-1])

	def process_msg(self, msg):
		raise NotImplementedError()

class MutliAdapter(AdapterConnection):
	"""BLE multi-adapter interface class"""
	b64_tag = b'\2'

	def __init__(self, port):
		super().__init__(port)

	def reset(self):
		"""Reset adapter"""
		super().reset()
		self.write_msg(b'#R')

	def connect(self, peers):
		self.send_cmd(b'C' + b' '.join(peers))

	def send_data(self, data, binary=False):
		"""Send data to connected central"""
		if binary:
			data = MutliAdapter.b64_tag + base64.b64encode(data)
		self.submit_msg(b'>' + data)

	def send_data_to(self, idx, data, binary=False):
		"""Send data to peer given its index"""
		if binary:
			data = MutliAdapter.b64_tag + base64.b64encode(data)
		self.submit_msg((b'0'[0] + idx).to_bytes(1, byteorder='big') + data)

	def process_msg(self, msg):
		tag = msg[:1]
		if tag == b':':
			self.on_status_msg(msg[1:])
		elif tag == b'-':
			self.on_debug_msg(msg[1:])
		elif tag == b'<':
			self.on_central_msg_(msg[1:])
		elif msg:
			self.on_peer_msg_(msg[0] - b'0'[0], msg[1:])
		else:
			self.parse_errors += 1

	def on_status_msg(self, msg):
		tag = msg[:1]
		if tag == b'I':
			if msg[1:2] == b'h':
				self.on_idle(True, msg[2:].strip())
			else:
				self.on_idle(False, msg[1:].strip())
		elif tag == b'C':
			self.on_connecting(msg[1] - b'0'[0])
		elif tag == b'D':
			self.on_connected(msg[1:2] == b'h')
		else:
			self.parse_errors += 1

	def on_central_msg_(self, msg):
		if msg[:1] == MutliAdapter.b64_tag:
			try:
				msg = base64.b64decode(msg[1:])
			except binascii.Error:
				self.parse_errors += 1
				return
		self.on_central_msg(msg)

	def on_peer_msg_(self, idx, msg):
		if msg[:1] == MutliAdapter.b64_tag:
			try:
				msg = base64.b64decode(msg[1:])
			except binascii.Error:
				self.parse_errors += 1
				return
		self.on_peer_msg(idx, msg)

	def on_idle(self, hidden, version):
		pass

	def on_connecting(self, idx):
		pass

	def on_connected(self, hidden):
		pass

	def on_debug_msg(self, msg):
		pass

	def on_central_msg(self, msg):
		pass

	def on_peer_msg(self, idx, msg):
		pass
