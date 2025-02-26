'use strict';

let __ble_mx_api = {};

(() => {
	//
	// This is the interface to the esp32-ble-uart-mx adapters
	// See https://github.com/olegv142/esp32-ble-uart-mx
	//
	class Connection
	{
		// The plain connection for message passing
		static bt_svc_id     = 0xFFE0;
		static bt_char_tx_id = 0xFFE1;
		static bt_char_rx_id = 0xFFE2;
		static conn_retry_tout = 500;

		dual_mode = false;
		bt_char   = null;
		bt_busy   = false;
		tx_queue  = [];
		msg_cb    = null;

		constructor(msg_cb, dual_mode = False) {
			this.dual_mode = dual_mode;
			this.msg_cb = msg_cb;
		}

		connect(device, conn_cb, disc_cb) {
			const conn = this;
			function on_connect(chars)
			{
				let listen_char = chars[0];
				function on_disconnect(event)
				{
					const device = event.target;
					console.log(device.name + ' bluetooth device disconnected');
					listen_char.removeEventListener('characteristicvaluechanged', on_value_changed);
					device.removeEventListener('gattserverdisconnected', on_disconnect);
					conn.bt_char = null;
					conn.bt_busy = false;
					conn.tx_queue = [];
					if (disc_cb)
						disc_cb(device);
				}
				console.log(device.name, 'connected');
				listen_char.addEventListener('characteristicvaluechanged', on_value_changed);
				device.addEventListener('gattserverdisconnected', on_disconnect);
				conn.bt_char = conn.dual_mode ? chars[1] : chars[0];
				if (conn_cb)
					conn_cb(device);
			}
			function on_value_changed(event) {
				if (conn.msg_cb)
					conn.msg_cb(event.target.value);
			}
			device.gatt.connect().
			then((server) => {
				console.log(device.name, 'GATT server connected, getting service...');
				return server.getPrimaryService(Connection.bt_svc_id);
			}).
			then((service) => {
				console.log(device.name, 'service found, getting characteristic...');
				return Promise.all([
				  service.getCharacteristic(Connection.bt_char_tx_id),
				  conn.dual_mode ? service.getCharacteristic(Connection.bt_char_rx_id) : null
				]);
			}).
			then((chars) => {
				console.log(device.name, 'characteristic found');
				return chars[0].startNotifications().then(
					() => {
						on_connect(chars);
					},
					(err) => {
						console.error('Failed to subscribe to ' + device.name + ':', err.message);
						return Promise.reject(err);
					}
				);
			})
			.catch((err) => {
				console.error('Failed to connect to ' + device.name + ':', err.message);
				setTimeout(() => { conn.connect(device, conn_cb, disc_cb); }, Connection.conn_retry_tout);
			});
		}

		is_connected() {
			return this.bt_char !== null;
		}

		is_redonly() {
			return !this.bt_char.properties.write;
		}

		#bt_write(data) {
			this.bt_char.writeValueWithoutResponse(data)
			.then(
				() => {this.#tx_queue_flush();},
				(err) => {console.error('BT write failed'); this.tx_queue.push(data); this.#tx_queue_flush();}
			);
		}

		#tx_queue_flush() {
			const data = this.tx_queue.shift();
			if (data)
				this.#bt_write(data);
			else
				this.bt_busy = false;
		}

		// Transmit data packet.
		// The data argument may be DataView or Uint8Array.
		write(data) {
			if (this.bt_busy) {
				this.tx_queue.push(data);
				return;
			}
			this.bt_busy = true;
			this.#bt_write(data);
		}

	}
	//
	// Extended frames support
	//
	const MAX_SIZE    = 244;
	const MAX_CHUNKS  = 35;
	const XHDR_SIZE   = 1;
	const CHKSUM_SIZE = 3;
	const MAX_PAYLOAD = MAX_SIZE - XHDR_SIZE - CHKSUM_SIZE;

	const XH_BINARY_BIT = 5;
	const XH_FIRST_BIT  = 6;
	const XH_LAST_BIT   = 7;

	const XH_BINARY  = (1<<XH_BINARY_BIT);
	const XH_FIRST   = (1<<XH_FIRST_BIT);
	const XH_LAST    = (1<<XH_LAST_BIT);

	const XH_SN_BITS = XH_BINARY_BIT;
	const XH_SN_MASK = (1<<XH_SN_BITS)-1;

	class ConnectionExt extends Connection
	{
		// Connection for extended frames transmission
		next_sn = 0;

		constructor(ext_frame_cb, dual_mode)
		{
			let next_sn = 0;
			let last_chunk = -1;
			let last_chksum = 0;
			let total_len = 0;
			let chunks = Array(MAX_CHUNKS);

			// The message callback that performs packets checking and defragmentation
			function chunk_rx_cb(data)
			{
				const len = data.byteLength;
				if (len <= XHDR_SIZE + CHKSUM_SIZE || len > MAX_SIZE) {
					console.error('invalid chunk size: ' + len);
					return;
				}
				const h = data.getUint8(0);
				if (!(h & XH_FIRST)) {
					if (last_chunk < 0 ||
						next_sn != (h & XH_SN_MASK) ||
						last_chunk + 1 >= MAX_CHUNKS
					) {
						console.error('chunk(s) lost');
						return;
					}
				}
				let chksum = (h & XH_FIRST) ? CHKSUM_INI : last_chksum;
				chksum = fnv1a(data, len - CHKSUM_SIZE, chksum);
				if (
					data.getUint8(len-CHKSUM_SIZE)   != (chksum & 0xff) ||
					data.getUint8(len-CHKSUM_SIZE+1) != ((chksum>>8) & 0xff) ||
					data.getUint8(len-CHKSUM_SIZE+2) != (((chksum>>16)^(chksum>>24)) & 0xff)
				) {
					console.error('invalid checksum');
					return;
				}
				if (h & XH_FIRST) {
					last_chunk = -1;
					total_len = 0;
				}
				total_len += len - XHDR_SIZE - CHKSUM_SIZE;
				last_chksum = chksum;
				next_sn = (h + 1) & XH_SN_MASK;
				chunks[++last_chunk] = data;
				if (!(h & XH_LAST))
					return;
				const buf = new Uint8Array(total_len);
				let off = 0;
				for (let i = 0; i <= last_chunk; ++i) {
					const data_len   = chunks[i].byteLength - XHDR_SIZE - CHKSUM_SIZE;
					const chunk_data = new Uint8Array(chunks[i].buffer, XHDR_SIZE, data_len);
					buf.set(chunk_data, off);
					off += data_len;
				}
				ext_frame_cb(
						new DataView(buf.buffer, 0, total_len),
						(h & XH_BINARY) != 0 // binary flag
					);
			}
			super(chunk_rx_cb, dual_mode);
		}

		// Transmit data frame splitting it to chunks if necessary.
		// The data argument may be DataView or Uint8Array.
		write(data, is_binary=false)
		{
			let len = data.byteLength;
			let chksum = CHKSUM_INI;
			let first = XH_FIRST;
			let off = 0;
			const binary = is_binary ? XH_BINARY : 0;
			while (len) {
				const chunk_len = len > MAX_PAYLOAD ? MAX_PAYLOAD : len;
				len -= chunk_len;
				const last = !len ? XH_LAST : 0;
				const hchunk_len = XHDR_SIZE + chunk_len;
				const msg_len = hchunk_len + CHKSUM_SIZE;
				const buf = new Uint8Array(msg_len);
				buf[0] = first + last + binary + this.next_sn;
				first = 0;
				this.next_sn = (this.next_sn + 1) & XH_SN_MASK;
				buf.set(new Uint8Array(data.buffer, off, chunk_len), XHDR_SIZE);
				const msg_data = new DataView(buf.buffer, 0, msg_len);
				chksum = fnv1a(msg_data, hchunk_len, chksum);
				buf[hchunk_len]   = chksum & 0xff;
				buf[hchunk_len+1] = (chksum>>8) & 0xff;
				buf[hchunk_len+2] = ((chksum>>16)^(chksum>>24)) & 0xff;
				off += chunk_len;
				super.write(msg_data);
			}
		}
	}
	//
	// Helper functions
	//
	function str2Uint8Array(str) {
		return Uint8Array.from(Array.from(str).map(letter => letter.charCodeAt(0)));
	}

	function DataView2str(data) {
		let str = '';
		for (let i = 0; i < data.byteLength; ++i) {
			const c = data.getUint8(i);
			str += String.fromCharCode(c);
		}
		return str;
	}

	const FNV32_PRIME  = 16777619;
	const FNV32_OFFSET = 2166136261;
	const CHKSUM_INI   = FNV32_OFFSET;

	function fnv1a_up(b, hash) {
		return Math.imul(hash ^ b, FNV32_PRIME) >>> 0;
	}

	function fnv1a(data, len, hash) {
		for (let i = 0; i < len; ++i)
			hash = fnv1a_up(data.getUint8(i), hash);
		return hash;
	}

	const CSUM_LEN = 5;
	const CSUM_BASE = 85;
	const CSUM_CODE_FIRST = 40;
	const COMPRESS_TAG = 36; // $ code

	function encode_csum(csum) {
		let str = '';
		for (let i = 0; i < CSUM_LEN; ++i) {
			str += String.fromCharCode(CSUM_CODE_FIRST + (csum % CSUM_BASE));
			csum = (csum / CSUM_BASE) >>> 0;
		}
		return str;
	}

	function str_csum(str, len) {
		let csum = CHKSUM_INI;
		if (len === undefined)
			len = str.length;
		for (let i = 0; i < len; ++i)
			csum = fnv1a_up(str.charCodeAt(i), csum);
		return encode_csum(csum);
	}

	async function compress(data) {
		const b = new Blob([data]);
		const flt = new CompressionStream("gzip");
		const dst = b.stream().pipeThrough(flt);
		const res = await new Response(dst).blob();
		return new DataView(await res.arrayBuffer());
	}

	async function decompress(data) {
		const b = new Blob([data]);
		const flt = new DecompressionStream("gzip");
		const dst = b.stream().pipeThrough(flt);
		const res = await new Response(dst).blob();
		return new DataView(await res.arrayBuffer());
	}

	__ble_mx_api.Connection     = Connection;
	__ble_mx_api.ConnectionExt  = ConnectionExt;
	__ble_mx_api.str2Uint8Array = str2Uint8Array;
	__ble_mx_api.DataView2str   = DataView2str;
	__ble_mx_api.str_csum       = str_csum;
	__ble_mx_api.CSUM_LEN       = CSUM_LEN;
	__ble_mx_api.COMPRESS_TAG   = COMPRESS_TAG;
	__ble_mx_api.compress       = compress;
	__ble_mx_api.decompress     = decompress;

})();
