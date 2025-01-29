from ble_multi_adapter import MutliAdapter, find_port, PARITY_NONE
import sys
import time

class UsbKey(MutliAdapter):
    parity = PARITY_NONE
    rtscts = False

    def __init__(self, port):
        super().__init__(port)
        self.msg_cnt = 0

    def on_idle(self, hidden, version):
        print('Idle, version %s' % version)

    def on_connecting(self, idx):
        print('Connecting to #%d' % idx)

    def on_debug_msg(self, msg):
        print('    %s' % msg)

    def on_central_msg(self, msg):
        print('[.] %s' % msg)

port = sys.argv[1] if len(sys.argv) > 1 else find_port(0x1a86, 0x55d3)
if not port:
    print ('Controller not found', file=sys.stderr)
    sys.exit(-1)

sn, msg_interval = 0, .2
with UsbKey(port) as ad:
    ad.reset()
    next_msg_ts = time.time() + msg_interval
    while True:
        ad.communicate()
        ts = time.time()
        if ts > next_msg_ts:
            # send message to connected central
            sn += 1
            ad.send_data(b'message #%d' % sn)
            next_msg_ts = ts + msg_interval
