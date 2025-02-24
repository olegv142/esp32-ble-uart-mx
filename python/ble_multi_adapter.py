"""
BLE multi adapter host interface
The project home is https://github.com/olegv142/esp32-ble-uart-mx

Author: Oleg Volkov
"""

import time
import base64
import hashlib
import binascii
from serial import Serial, PARITY_NONE, PARITY_EVEN
from serial.tools import list_ports

STREAM_TAG_FIRST = ord('@')
STREAM_TAGS_MOD = 191

class AdapterConnection:
	"""BLE multi-adapter core communication interface class"""
	baud_rate   = 115200
	use_parity  = True
	parity      = PARITY_EVEN if use_parity else PARITY_NONE
	start_tag   = b'\1'
	end_tag     = b'\0'
	b64_tag     = b'\2'
	use_tags    = True
	opt_tags    = True
	rtscts      = True
	timeout     = .01
	wr_timeout  = 20
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
			timeout=self.timeout,
			write_timeout=self.wr_timeout
		)
		if hasattr(self.com, 'set_buffer_size'):
			self.com.set_buffer_size(
				rx_size = self.rx_buf_size,
				tx_size = self.tx_buf_size
			)

	def close(self):
		if self.com:
			self.com.close()
			self.com = None

	def set_start_end_tags(self, tstart, tend):
		"""Set non default start / end tags"""
		self.start_tag = tstart
		self.end_tag   = tend

	def set_terminator(self, tend):
		"""Set end tag while not using start tag"""
		self.set_start_end_tags(b'', tend)

	def set_nl_terminator(self):
		"""Use new line symbol as message terminator"""
		self.set_terminator(b'\n')

	def use_stream_tags(self, use = True):
		self.use_tags = use

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
			wire_msg = b''.join((self.start_tag, 
					bytes([topen]),
					msg,
					bytes([tclose]),
					self.end_tag
				))
		else:
			wire_msg = b''.join((self.start_tag,
					msg,
					self.end_tag
				))
		self.com.write(wire_msg)

	def receive(self):
		"""Receive from adapter"""
		while True:
			rx_bytes = self.com.read(4096)
			if not rx_bytes:
				return
			self.process_rx(rx_bytes)

	def can_transmit(self):
		return True

	def communicate(self):
		"""Communicate with adapter"""
		self.receive()
		can_tx = self.can_transmit()
		if not can_tx:
			return
		tx_queue = self.tx_queue
		self.tx_queue, delay_queue = [], []
		for msg in tx_queue:
			if can_tx and self.can_transmit():
				self.write_msg(msg)
				self.receive()
			else:
				can_tx = False
				delay_queue.append(msg)
		self.tx_queue = delay_queue + self.tx_queue

	def submit_msg(self, msg):
		self.tx_queue.append(msg)

	def reset(self):
		self.tx_queue = []
		self.last_rx_tag = 0

	def encode_binary(self, data):
		return self.b64_tag + base64.b64encode(data)

	def decode_data(self, msg):
		if msg[:1] != self.b64_tag:
			return msg
		try:
			return base64.b64decode(msg[1:])
		except binascii.Error:
			self.parse_errors += 1
			return None

	def process_rx(self, rx_bytes):
		self.rx_buff += rx_bytes
		tail = 0
		begin = self.rx_buff.find(self.start_tag, 0) if self.start_tag else 0
		while 0 <= begin < len(self.rx_buff):
			end = self.rx_buff.find(self.end_tag, begin + 1)
			if end < 0:
				break
			if self.start_tag:
				while True:
					next_begin = self.rx_buff.find(self.start_tag, begin + 1)
					if 0 <= next_begin < end:
						# multiple begin bytes before end
						begin = next_begin
					else:
						break
				if begin != tail:
					# garbage between messages
					self.parse_errors += 1
				begin += 1 # skip start byte
			self.process_frame(self.rx_buff[begin:end])
			tail = end + 1
			begin = next_begin if self.start_tag else tail
		self.rx_buff = self.rx_buff[tail:]

	def process_frame(self, msg):
		if not self.use_tags and not self.opt_tags:
			self.process_msg(msg)
			return
		if not msg or not self.is_stream_tag(msg[0]):
			if self.opt_tags:
				self.process_msg(msg)
				return
			else:
				self.parse_errors += 1
				return
		topen = msg[0]
		if len(msg) < 2:
			self.parse_errors += 1
			return
		if msg[-1] != self.get_closing_tag(topen, len(msg) - 2):
			self.parse_errors += 1
			return
		if self.last_rx_tag:
			next_rx_tag = self.get_next_tag_(self.last_rx_tag)
			if topen != next_rx_tag:
				self.lost_frames += topen - next_rx_tag if topen > next_rx_tag else \
									topen + STREAM_TAGS_MOD - next_rx_tag
		self.last_rx_tag = topen
		self.process_msg(msg[1:-1])

	def process_msg(self, msg):
		raise NotImplementedError()

class MutliAdapter(AdapterConnection):
	"""BLE multi-adapter interface class"""
	stall_tout = 2 # sec

	def __init__(self, port):
		super().__init__(port)
		self.status_ts = 0
		self.is_stall = True
		self.stall_ts = None
		self.stall_time = 0

	def is_congested(self):
		"""Client should avoid submitting new data if adapter is congested"""
		return super().is_congested() or self.is_stall

	def reset(self):
		"""Reset adapter"""
		super().reset()
		self.write_msg(b'#R')
		self.is_stall = True
		self.stall_ts = None

	def chk_stall(self):
		"""Adapter is considered stall if its not sending status messages at expected interval (1 sec)"""
		now = time.time()
		if not self.is_stall and now > self.status_ts + self.stall_tout:
			self.is_stall = True
			self.stall_ts = now
		return self.is_stall

	def communicate(self):
		"""Send/receive data to/from adapter"""
		self.chk_stall()
		super().communicate()

	def can_transmit(self):
		"""Called by communicate implementation to check if we allowed to transmit data to adapter"""
		return not self.chk_stall()

	def connect(self, peers):
		"""Connect to the list of device addresses"""
		self.submit_msg(b'#C' + b' '.join(peers))

	def advertise(self):
		"""Turn on advertising if was hidden"""
		self.submit_msg(b'#A')

	def set_auth(self, seed, salt):
		"""Setup authentication"""
		self.submit_msg(b'#K' + seed + b'&' + salt)

	def send_data(self, data, binary=False):
		"""Send data to connected central"""
		if binary:
			data = self.encode_binary(data)
		self.submit_msg(b'>' + data)

	def send_data_to(self, idx, data, binary=False):
		"""Send data to peer given its index"""
		if binary:
			data = self.encode_binary(data)
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

	def on_stable_status(self):
		self.status_ts = time.time()
		if self.is_stall:
			self.is_stall = False
			if self.stall_ts:
				self.stall_time += time.time() - self.stall_ts

	def on_status_msg(self, msg):
		tag = msg[:1]
		if tag == b'I':
			self.on_stable_status()
			hidden = (msg[1:2] == b'h')
			tail = msg[2:] if hidden else msg[1:]
			m = tail.split()
			self.on_idle(hidden, m[0], m[1] if len(m) > 1 else None)
		elif tag == b'C':
			self.on_connecting(msg[1] - b'0'[0])
		elif tag == b'D':
			self.on_stable_status()
			self.on_connected(msg[1:2] == b'h')
		else:
			self.parse_errors += 1

	def on_central_msg_(self, msg):
		data = self.decode_data(msg)
		if data is not None:
			self.on_central_msg(data)

	def on_peer_msg_(self, idx, msg):
		data = self.decode_data(msg)
		if data is not None:
			self.on_peer_msg(idx, data)

	def led_set_rgb(self, r, g, b):
		"""Manually control the neo-pixel LED on board"""
		self.submit_msg(b'#L%u %u %u' % (r, g, b))

	def led_set_auto(self):
		"""Switch to auto control of the neo-pixel LED on board"""
		self.submit_msg(b'#L')

	def on_idle(self, hidden, version, passkey):
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

class SimpleAdapter(AdapterConnection):
	"""BLE simple link adapter interface class"""
	use_tags = False
	opt_tags = False

	def __init__(self, port):
		super().__init__(port)

	def send_data(self, data, binary=False):
		"""Send data frame to connected peer device"""
		if binary:
			data = self.encode_binary(data)
		self.submit_msg(data)

	def process_msg(self, msg):
		data = self.decode_data(msg)
		if data is not None:
			self.on_data_received(data)

	def on_data_received(self, data):
		pass


def find_port(vid = 0x303A, pid = 0x1001):
	"""Find connected controller by USB device VID:PID"""
	for p in list_ports.comports():
		if p.vid == vid and p.pid == pid:
			return p.device
	return None

master_key_default = bytes(range(10))

def mk_auth_key(seed, master_key=master_key_default):
	"""Make auth key given master key and seed value"""
	h = hashlib.md5()
	h.update(seed)
	h.update(master_key)
	return h.digest()

def chk_auth(passkey, seed, salt, master_key=master_key_default):
	"""Verify passkey received in idle message given known master key"""
	return chk_auth_key(passkey, mk_auth_key(seed, master_key), salt)

def chk_auth_key(passkey, auth_key, salt):
	"""
	Verify passkey received in idle message against auth key derived from the master key.
	This routine may be used without exposing master key.
	"""
	try:
		bkey = base64.b64decode(passkey)
	except binascii.Error:
		return False
	h = hashlib.md5()
	h.update(auth_key)
	h.update(salt)
	return h.digest()[:len(bkey)] == bkey

def bytes_csum(buf):
	"""Calculate FNV1a hash of the byte array"""
	csum = 2166136261
	for b in buf:
		csum = ((csum ^ b) * 16777619) & 0xffffffff
	return csum

CSUM_LEN = 5
CSUM_BASE = 85
CSUM_CODE_FIRST = 40

def encode_csum(csum):
	"""Encode checksum to 5 symbol representation"""
	codes = []
	for i in range(CSUM_LEN):
		codes.append(CSUM_CODE_FIRST + csum % CSUM_BASE)
		csum //= CSUM_BASE
	assert csum == 0
	return bytes(codes)

def bytes_csum_encoded(buf):
	return encode_csum(bytes_csum(buf))
