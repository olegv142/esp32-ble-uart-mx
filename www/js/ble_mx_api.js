'use strict';

let __ble_mx_api = {};

(() => {
	class Connection {
		static bt_svc_id     = 0xFFE0;
		static bt_char_tx_id = 0xFFE1;
		static bt_char_rx_id = 0xFFE2;
		static conn_retry_tout = 500;

		dual_mode = false;
		bt_char = null;
		bt_busy = false;
		tx_queue = [];
		msg_cb = null;

		constructor(msg_cb, dual_mode = False) {
			this.dual_mode = dual_mode;
			this.msg_cb = msg_cb;
		}

		connect(device, conn_cb, disc_cb) {
			const conn = this;
			function on_connect(chars)
			{
				console.log(device.name, 'connected');
				chars[0].addEventListener('characteristicvaluechanged', on_value_changed);
				device.addEventListener('gattserverdisconnected', on_disconnect);
				conn.bt_char = conn.dual_mode ? chars[1] : chars[0];
				if (conn_cb)
					conn_cb(device);
			}
			function on_value_changed(event) {
				if (conn.msg_cb)
					conn.msg_cb(event.target.value);
			}
			function on_disconnect(event)
			{
				const device = event.target;
				console.log(device.name + ' bluetooth device disconnected');
				conn.bt_char = null;
				conn.bt_busy = false;
				conn.tx_queue = [];
				if (disc_cb)
					disc_cb(device);
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
						console.log('Failed to subscribe to ' + device.name + ':', err.message);
						return Promise.reject(err);
					}
				);
			})
			.catch((err) => {
				console.log('Failed to connect to ' + device.name + ':', err.message);
				setTimeout(() => { conn.connect(device, conn_cb, disc_cb); }, Connection.conn_retry_tout);
			});
		}

		is_connected() {
			return this.bt_char !== null;
		}

		is_redonly() {
			return !this.bt_char.properties.write;
		}

		#bt_write(val) {
			this.bt_char.writeValueWithoutResponse(val)
			.then(
				() => {this.#tx_queue_flush();},
				(err) => {console.log('BT write failed'); this.tx_queue.push(val); this.#tx_queue_flush();}
			);
		}

		#tx_queue_flush() {
			const val = this.tx_queue.shift();
			if (val)
				this.#bt_write(val);
			else
				this.bt_busy = false;
		}

		write(val) {
			if (this.bt_busy) {
				this.tx_queue.push(val);
				return;
			}
			this.bt_busy = true;
			this.#bt_write(val);
		}

	}

	function str2Uint8Array(str) {
		return Uint8Array.from(Array.from(str).map(letter => letter.charCodeAt(0)));
	}

	function DataView2str(data) {
		let str = '';
		for (let i = 0; i < data.byteLength; i++) {
			const c = data.getUint8(i);
			str += String.fromCharCode(c);
		}
		return str;
	}

	__ble_mx_api.Connection     = Connection;
	__ble_mx_api.str2Uint8Array = str2Uint8Array;
	__ble_mx_api.DataView2str   = DataView2str;

})();
