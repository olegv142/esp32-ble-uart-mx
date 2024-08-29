"""
BLE multi adapter (ble_uart_mx) host interface

Author: Oleg Volkov
"""

import sys
import base64
import binascii
from serial import Serial, PARITY_NONE, PARITY_EVEN

use_parity = True

class MutliAdapter:
	"""BLE multi-adapter interface class"""
	baud_rate   = 115200
	parity      = PARITY_EVEN if use_parity else PARITY_NONE
	start_byte  = b'\1'
	end_byte    = b'\0'
	b64_tag     = b'\2'
	rtscts      = True
	timeout     = .01
	congest_thr = 16

	def __init__(self, port):
		self.port    = port
		self.com     = None
		self.rx_buff = b''
		self.parse_errors = 0
		self.tx_queue = []

	def __enter__(self):
		self.open()
		return self

	def __exit__(self, ex_type, ex_value, traceback):
		self.close()

	def open(self):
		assert self.com is None
		self.com = Serial(self.port,
			baudrate=MutliAdapter.baud_rate,
			parity=MutliAdapter.parity,
			rtscts=MutliAdapter.rtscts,
			timeout=MutliAdapter.timeout
		)
		self.com.set_buffer_size(rx_size = 4*4096, tx_size = 4096)

	def close(self):
		if self.com:
			self.com.close()
			self.com = None

	def is_congested(self):
		return len(self.tx_queue) > MutliAdapter.congest_thr

	def write_msg(self, msg):
		"""Write message to the adapter"""
		self.com.write(msg)

	def send_cmd(self, cmd):
		"""Send command to the adapter"""
		self.tx_queue.append(MutliAdapter.start_byte + b'#' + cmd + MutliAdapter.end_byte)

	def reset(self):
		"""Reset adapter"""
		self.tx_queue = []
		self.write_msg(MutliAdapter.start_byte + b'#R' + MutliAdapter.end_byte)

	def connect(self, peers):
		self.send_cmd(b'C' + b' '.join(peers))

	def send_data(self, data, binary=False):
		"""Send data to connected central"""
		if binary:
			data = MutliAdapter.b64_tag + base64.b64encode(data)
		self.tx_queue.append(MutliAdapter.start_byte + b'>' + data + MutliAdapter.end_byte)

	def send_data_to(self, idx, data, binary=False):
		"""Send data to peer given its index"""
		if binary:
			data = MutliAdapter.b64_tag + base64.b64encode(data)
		self.tx_queue.append(MutliAdapter.start_byte + (b'0'[0] + idx).to_bytes(1, byteorder='big') + data + MutliAdapter.end_byte)

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

	def process_rx(self, rx_bytes):
		self.rx_buff += rx_bytes
		first = 0
		begin = self.rx_buff.find(MutliAdapter.start_byte, 0)
		while begin >= 0:
			end = self.rx_buff.find(MutliAdapter.end_byte, begin + 1)
			if end < 0:
				break
			while True:
				next_begin = self.rx_buff.find(MutliAdapter.start_byte, begin + 1)
				if next_begin >= 0 and next_begin < end:
					begin = next_begin
					self.parse_errors += 1
				else:
					break
			self.process_msg(self.rx_buff[begin+1:end])
			first = end + 1
			begin = next_begin
		self.rx_buff = self.rx_buff[first:]

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
			self.on_connected()
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

	def on_connected(self):
		pass

	def on_debug_msg(self, msg):
		pass

	def on_central_msg(self, msg):
		pass

	def on_peer_msg(self, idx, msg):
		pass
