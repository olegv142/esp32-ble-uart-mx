"""
Long messages echo test using ESP32 PICO-D4 USB KEY as adapter
Use https://olegv142.github.io/esp32-ble-uart-mx/?dual&echo&xf&cs for testing
"""

import sys
import time
import random
import gzip
from collections import Counter
from ble_multi_adapter import MutliAdapter, find_port, PARITY_NONE, CSUM_LEN, bytes_csum_encoded

min_msg_interval = .5
max_msg_interval = 2
max_msg_burst = 2

use_compression = True
if use_compression:
    zthreshold = 512 # messages of greater length will be compressed
else:
    zthreshold = None # disable compression
ztag = b'$'

def random_bytes(len):
    return bytes((random.randrange(ord('0'), ord('z')+1) for _ in range(len)))

def sometimes():
    """Return True with 10% probability"""
    return not random.randrange(0, 10)

def random_message(sn, max_size):
    hdr = b'%d#' % sn
    max_len = max_size - len(hdr)
    # use maximum length for every 10th message
    data_len = max_len if sometimes() else random.randrange(1, max_len+1)
    return hdr + random_bytes(data_len)

def get_message_sn(msg):
    """Returns message sn if message is valid or None otherwise"""
    s = msg.split(b'#')
    if len(s) != 2:
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
        self.max_msg = 0
        self.tx_cnt = 0
        self.rx_cnt = 0
        self.rx_bytes = 0
        self.msg_errs = 0
        self.msg_lost = 0
        self.msg_dup = 0
        self.last_sn = 0
        self.last_tx_ts = 0
        self.messages = Counter()

    def send_random_msg(self):
        self.tx_cnt += 1
        msg = random_message(self.tx_cnt, self.max_msg - CSUM_LEN)
        msg += bytes_csum_encoded(msg)
        if zthreshold is not None and len(msg) >= zthreshold and not sometimes():
            zmsg = gzip.compress(msg) + ztag
            if len(zmsg) < len(msg):
                self.send_data(zmsg, True)
                self.last_tx_ts = time.time()
                return
        self.send_data(msg, False)
        self.last_tx_ts = time.time()

    def parse_version(self, version):
        try:
            self.max_msg = int(version.split(b'-')[1])
            print('  max_msg=%d' % self.max_msg)
        except:
            print('  invalid version')

    def on_idle(self, hidden, version, passkey):
        print('Idle, version %s' % version)
        if not self.max_msg:
            self.parse_version(version)
        if hidden:
            self.advertise()

    def on_connecting(self, idx):
        print('Connecting to #%d' % idx)

    def on_debug_msg(self, msg):
        print('    %s' % msg)
        self.messages.update([msg])

    def ready_to_send(self):
        if not self.connected or not self.max_msg:
            return False
        if self.last_sn == self.tx_cnt:
            return True
        idle_time = time.time() - self.last_tx_ts
        if self.last_sn + max_msg_burst > self.tx_cnt:
            if idle_time > min_msg_interval:
                return True
        else:
            if idle_time > max_msg_interval:
                return True
        return False

    def on_central_msg(self, msg):
        if not msg:
            self.connected = True
            return
        self.rx_cnt += 1
        self.rx_bytes += len(msg)
        if msg[-1:] == ztag:
            try:
                msg = gzip.decompress(msg[:-1])
            except:
                self.msg_errs += 1
                return
        msg_full, msg, csum = msg, msg[:-CSUM_LEN], msg[-CSUM_LEN:]
        if bytes_csum_encoded(msg) != csum:
            print('bad csum: %s' % msg_full)
            self.msg_errs += 1
            return
        print('[.] %s' % msg_full)
        sn = get_message_sn(msg)
        if sn is None:
            self.msg_errs += 1
        elif self.last_sn:
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

with UsbKey(port) as ad:
    ad.reset()
    start_ts = time.time()
    try:
        while True:
            ad.communicate()
            if ad.ready_to_send():
                ad.send_random_msg()
    except KeyboardInterrupt:
        elapsed = time.time() - start_ts
        print ('--------------------------------------------------------------')
        print ('%d msg sent, %d received (%d bytes) in %d sec (%d bytes/sec)' % (ad.tx_cnt, ad.rx_cnt, ad.rx_bytes, elapsed, ad.rx_bytes / elapsed))
        print ('%d msg lost, %d duplicated, %d corrupted' % (ad.msg_lost, ad.msg_dup, ad.msg_errs))
        print ('messages:')
        for msg, cnt in ad.messages.items():
            print ('%dx\t%s' % (cnt, msg))
