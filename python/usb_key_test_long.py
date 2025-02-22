from ble_multi_adapter import MutliAdapter, find_port, PARITY_NONE
import sys
import time
import random

max_len = 1024
msg_interval = .5

def random_bytes(len):
    return bytes((random.randrange(ord('0'), ord('z')+1) for _ in range(len)))

def random_message(sn):
    msg = random_bytes(random.randrange(1, max_len+1))
    return (b'%d#' % sn) + msg + b'#' + msg

def chk_message(msg):
    """Returns message sn if message is valid or None otherwise"""
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
        self.ready = False
        self.tx_cnt = 0
        self.rx_cnt = 0
        self.msg_errs = 0
        self.msg_lost = 0
        self.msg_dup = 0
        self.last_sn = None

    def send_msg(self, msg):
        self.tx_cnt += 1
        self.send_data(msg)

    def on_idle(self, hidden, version, passkey):
        print('Idle, version %s' % version)
        if hidden:
            self.advertise()

    def on_connecting(self, idx):
        print('Connecting to #%d' % idx)

    def on_debug_msg(self, msg):
        print('    %s' % msg)

    def on_central_msg(self, msg):
        print('[.] %s' % msg)
        if not msg:
            self.ready = True
            return
        self.rx_cnt += 1
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
            if ts > next_msg_ts and ad.ready:
                # send message to connected central
                sn += 1
                ad.send_msg(random_message(sn))
                next_msg_ts = ts + msg_interval
    except KeyboardInterrupt:
        print ('%d msg sent, %d received in %d sec' % (ad.tx_cnt, ad.rx_cnt, time.time() - start_ts))
        print ('%d msg lost, %d duplicated, %d corrupted' % (ad.msg_lost, ad.msg_dup, ad.msg_errs))
