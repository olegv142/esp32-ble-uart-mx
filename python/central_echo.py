"""
Connect to one or more peripheral(s) and echo back all received messages
"""

import sys
from collections import Counter
from ble_multi_adapter import MutliAdapter, find_port, PARITY_NONE

class CentralEcho(MutliAdapter):
    parity = PARITY_NONE
    rtscts = False

    def __init__(self, port, peers):
        super().__init__(port)
        print('Start connecting to %s' % peers)
        self.peers = peers
        self.msg_cnt = 0
        self.messages = Counter()

    def on_idle(self, hidden, version, passkey):
        print('Idle, version %s' % version)
        self.connect(self.peers)

    def on_connecting(self, idx):
        print('Connecting to #%d' % idx)

    def on_debug_msg(self, msg):
        print('    %s' % msg)
        self.messages.update([msg])

    def on_peer_msg(self, idx, msg):
        print('[%d] %s' % (idx, msg))
        if msg:
            self.send_data_to(idx, msg, binary=True)
            self.msg_cnt += 1

if __name__ == '__main__':
    # port = find_port(0x1a86, 0x55d3) # USB key
    # port = find_port(0x303a, 0x1001) # USB CDC

    if len(sys.argv) <= 2:
        print('port name and target address(es) must be passed as parameters')
        sys.exit(1)

    port = sys.argv[1]
    with CentralEcho(port, [p.encode() for p in sys.argv[2:]]) as ad:
        ad.reset()
        try:
            while True:
                ad.communicate()
        except KeyboardInterrupt:
            print ('--------------------------------------------------------------')
            print('%u messages received, %d serial frames lost, %d parse errors' % (
                ad.msg_cnt, ad.lost_frames, ad.parse_errors
            ))
            print('messages:')
            for msg, cnt in ad.messages.items():
                print ('%dx\t%s' % (cnt, msg))
