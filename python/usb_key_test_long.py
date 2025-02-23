"""
Long messages echo test using ESP32 PICO-D4 USB KEY as adapter
Use https://olegv142.github.io/esp32-ble-uart-mx/?dual&echo&xf for testing
"""

from ble_multi_adapter import MutliAdapter, find_port, PARITY_NONE, CSUM_LEN, bytes_csum_encoded
import sys
import time
import random

max_data_len = 1024
msg_interval = .25
binary    = False
bin_tail  = b'\0\1\2\3'
with_csum = True

def random_bytes(len):
    return bytes((random.randrange(ord('0'), ord('z')+1) for _ in range(len)))

def random_message(sn):
    data = random_bytes(random.randrange(1, max_data_len+1))
    msg = (b'%d#' % sn) + data + b'#' + data
    if binary:
        msg += bin_tail
    return msg

def chk_message(msg):
    """Returns message sn if message is valid or None otherwise"""
    if binary:
        tail_len = len(bin_tail)
        if msg[-tail_len:] != bin_tail:
            return None
        msg = msg[:-tail_len]
    s = msg.split(b'#')
    if len(s) != 3:
        return None
    if s[1] != s[2]:
        return None
    try:
        return int(s[0])
    except:
        return None

class UsbKey(MutliAdapter):
    parity = PARITY_NONE
    rtscts = False

    def __init__(self, port):
        super().__init__(port)
        self.connected = False
        self.tx_cnt = 0
        self.rx_cnt = 0
        self.rx_bytes = 0
        self.msg_errs = 0
        self.msg_lost = 0
        self.msg_dup = 0
        self.last_sn = None

    def send_msg(self, msg):
        self.tx_cnt += 1
        if with_csum:
            msg += bytes_csum_encoded(msg)
        self.send_data(msg, binary)

    def on_idle(self, hidden, version, passkey):
        print('Idle, version %s' % version)
        if hidden:
            self.advertise()

    def on_connecting(self, idx):
        print('Connecting to #%d' % idx)

    def on_debug_msg(self, msg):
        print('    %s' % msg)

    def on_central_msg(self, msg):
        if not msg:
            self.connected = True
            return
        self.rx_cnt += 1
        self.rx_bytes += len(msg)
        if with_csum:
            msg_full, msg, csum = msg, msg[:-CSUM_LEN], msg[-CSUM_LEN:]
            if bytes_csum_encoded(msg) != csum:
                print('bad csum: %s' % msg_full)
                self.msg_errs += 1
                return
        else:
            msg_full = msg
        print('[.] %s' % msg_full)
        sn = chk_message(msg)
        if sn is None:
            self.msg_errs += 1
        elif self.last_sn is not None:
            expect_sn = self.last_sn + 1
            if sn != expect_sn:
                if sn > expect_sn:
                    self.msg_lost += sn - expect_sn
                else:
                    self.msg_dup += 1
        self.last_sn = sn

port = sys.argv[1] if len(sys.argv) > 1 else find_port(0x1a86, 0x55d3)
if not port:
    print ('Controller not found', file=sys.stderr)
    sys.exit(-1)

sn = 0
with UsbKey(port) as ad:
    ad.reset()
    start_ts = time.time()
    next_msg_ts = start_ts + msg_interval
    try:
        while True:
            ad.communicate()
            ts = time.time()
            if ts > next_msg_ts and ad.connected:
                # send message to connected central
                sn += 1
                ad.send_msg(random_message(sn))
                next_msg_ts = ts + msg_interval
    except KeyboardInterrupt:
        elapsed = time.time() - start_ts
        print ('%d msg sent, %d received (%d bytes) in %d sec (%d bytes/sec)' % (ad.tx_cnt, ad.rx_cnt, ad.rx_bytes, elapsed, ad.rx_bytes / elapsed))
        print ('%d msg lost, %d duplicated, %d corrupted' % (ad.msg_lost, ad.msg_dup, ad.msg_errs))
