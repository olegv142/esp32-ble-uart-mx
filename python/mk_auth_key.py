"""
Make auth key given master key and seed
"""

import sys
from ble_multi_adapter import mk_auth_key, master_key_default

try:
	from master_key import master_key
except:
	master_key = master_key_default

if __name__ == '__main__':
	print(mk_auth_key(sys.argv[1].encode(), master_key))
