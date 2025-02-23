from ble_multi_adapter import MutliAdapter, find_port, PARITY_NONE, chk_auth, master_key_default
import sys
import random

try:
	from master_key import master_key
except:
	master_key = master_key_default

class UsbKey(MutliAdapter):
    """
    Check authentication with default master key
    """
    parity = PARITY_NONE
    rtscts = False

    def __init__(self, port):
        super().__init__(port)
        self.seed = b'hello %d' % random.randrange(1000)
        self.salt = b'world %d' % random.randrange(1000)
        self.done = False

    def on_idle(self, hidden, version, passkey):
        print('Idle, version %s' % version)
        if not passkey:
            self.set_auth(self.seed, self.salt)
        else:
            matched = chk_auth(passkey, self.seed, self.salt, master_key)
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
