"""
The following example illustrates sending / receiving
messages to / from connected central and controlling
the on-board neo-pixel LED.
One can use https://olegv142.github.io/esp32-ble-uart-mx/?dual&echo&xf
to receive those messages and echo them back
"""

import sys
import time
from ble_multi_adapter import MutliAdapter, find_port

class TestAdapter(MutliAdapter):
	def __init__(self, port):
		super().__init__(port)

	def on_idle(self, hidden, version, passkey):
		print('  v.%s' % version)

	def on_debug_msg(self, msg):
		print('    %s' % msg)

	def on_central_msg(self, msg):
		print('[.] %s' % msg)

if __name__ == '__main__':
	port = sys.argv[1] if len(sys.argv) > 1 else find_port()
	if not port:
		print ('Controller not found', file=sys.stderr)
		sys.exit(-1)

	sn, msg_interval = 0, .2
	blink_interval, blink_duration = 5, .5
	blink_rgb = (128, 32, 64)
	with TestAdapter(port) as ad:
		ad.reset()
		next_msg_ts = time.time() + msg_interval
		next_blink_ts = time.time() + msg_interval
		blink_off_ts = None
		while True:
			ad.communicate()
			ts = time.time()
			if ts > next_msg_ts:
				# send message to connected central
				sn += 1
				ad.send_data(b'message #%d' % sn)
				next_msg_ts = ts + msg_interval
			if ts > next_blink_ts:
				# blink on
				ad.led_set_rgb(*blink_rgb)
				blink_off_ts  = ts + blink_duration
				next_blink_ts = ts + blink_interval
			elif blink_off_ts and ts > blink_off_ts:
				# blink off
				ad.led_set_auto()
				blink_off_ts = None
