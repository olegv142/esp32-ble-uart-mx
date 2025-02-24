"""
The example illustrates adapter authentication with seed
and auth key (without exposing master key)
"""

from ble_multi_adapter import MutliAdapter, find_port, PARITY_NONE, chk_auth_key
import sys
import random

# use mk_auth_key.py for making key out of the seed
try:
	from auth_key import seed, key
except:
	seed = b'123456'
	key  = b'\x0c)j\x90\xfe\x9f\x1d\x159$\xea$\x1f\x84\xa6q'

class UsbKey(MutliAdapter):
    parity = PARITY_NONE
    rtscts = False

    def __init__(self, port):
        super().__init__(port)
        self.salt = b'hello world %d' % random.randrange(1000)
        self.done = False

    def on_idle(self, hidden, version, passkey):
        print('Idle, version %s' % version)
        if not passkey:
            self.set_auth(seed, self.salt)
        else:
            matched = chk_auth_key(passkey, key, self.salt)
            assert matched
            print ('passkey %s is valid' % passkey)
            self.done = True

    def on_debug_msg(self, msg):
        print('    %s' % msg)

port = sys.argv[1] if len(sys.argv) > 1 else find_port(0x1a86, 0x55d3)
if not port:
    print ('Controller not found', file=sys.stderr)
    sys.exit(-1)

with UsbKey(port) as ad:
    ad.reset()
    while not ad.done:
        ad.communicate()
