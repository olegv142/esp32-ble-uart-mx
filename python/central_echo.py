"""
Using USB key to connect to the peripheral and echo back all received messages
"""

from ble_multi_adapter import MutliAdapter, find_port, PARITY_NONE
import sys

class CentralEcho(MutliAdapter):
    parity = PARITY_NONE
    rtscts = False

    def __init__(self, port, peers):
        super().__init__(port)
        print('Start connecting to %s' % peers)
        self.peers = peers
        self.msg_cnt = 0

    def on_idle(self, hidden, version, passkey):
        print('Idle, version %s' % version)
        self.connect(self.peers)

    def on_connecting(self, idx):
        print('Connecting to #%d' % idx)

    def on_debug_msg(self, msg):
        print('    %s' % msg)

    def on_peer_msg(self, idx, msg):
        print('[%d] %s' % (idx, msg))
        if msg:
            self.send_data_to(idx, msg, binary=True)
            self.msg_cnt += 1

if __name__ == '__main__':
    # port = find_port(0x1a86, 0x55d3) # USB key
    port = find_port(0x303a, 0x1001) # USB CDC
    if not port:
        print('Adapter not found')
        sys.exit(-1)

    if len(sys.argv) <= 1:
        print('Target address(es) required')
        sys.exit(1)

    with CentralEcho(port, [p.encode() for p in sys.argv[1:]]) as ad:
        ad.reset()
        try:
            while True:
                ad.communicate()
        except KeyboardInterrupt:
            print('%u messages received, %d serial frames lost, %d parse errors' % (
                ad.msg_cnt, ad.lost_frames, ad.parse_errors
            ))
