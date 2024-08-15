"""
BLE multi adapter (ble_uart_mx) host interface

Author: Oleg Volkov
"""

import sys
from serial import Serial, PARITY_NONE, PARITY_EVEN

use_parity = True

class MutliAdapter:
	"""BLE multi-adapter interface class"""
	baud_rate  = 115200
	parity     = PARITY_EVEN if use_parity else PARITY_NONE
	start_byte = b'\1'
	end_byte   = b'\0'
	dsrdtr     = True
	timeout    = .01

	def __init__(self, port):
		self.port    = port
		self.com     = None
		self.rx_buff = b''

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
			dsrdtr=MutliAdapter.dsrdtr,
			timeout=MutliAdapter.timeout
		)

	def close(self):
		if self.com:
			self.com.close()
			self.com = None

	def send_cmd(self, cmd):
		"""Send command to the adapter"""
		self.com.write(MutliAdapter.start_byte + b'#' + cmd + MutliAdapter.end_byte)

	def reset(self, drain = True):
		self.send_cmd(b'R')
		if drain:
			self.com.read(4096)

	def connect(self, peers):
		self.send_cmd(b'C' + b' '.join(peers))

	def send_data(self, data):
		"""Send data to connected central"""
		self.com.write(MutliAdapter.start_byte + b'>' + data + MutliAdapter.end_byte)

	def send_data_to(self, idx, data):
		"""Send data to peer given its index"""
		self.com.write(MutliAdapter.start_byte + (b'0'[0] + idx).to_bytes(1, byteorder='big') + data + MutliAdapter.end_byte)

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
		tag = msg[:1]
		if tag == b':':
			self.on_status_msg(msg[1:])
		elif tag == b'-':
			self.on_debug_msg(msg[1:])
		elif tag == b'<':
			self.on_central_msg(msg[1:])
		else:
			self.on_peer_msg(msg[0] - b'0'[0], msg[1:])

	def on_status_msg(self, msg):
		tag = msg[:1]
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
