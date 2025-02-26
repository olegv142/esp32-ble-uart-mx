'use strict';

(() => {

const query_str  = window.location.search;
const url_param  = new URLSearchParams(query_str);
const echo_mode  = url_param.get('echo') !== null;
const dual_mode  = url_param.get('dual') !== null;
const xframes    = url_param.get('xf')   !== null;
const with_csum  = url_param.get('cs')   !== null;

const Connection = xframes ? __ble_mx_api.ConnectionExt : __ble_mx_api.Connection;

const str2Uint8Array = __ble_mx_api.str2Uint8Array;
const DataView2str   = __ble_mx_api.DataView2str;
const str_csum       = __ble_mx_api.str_csum;
const CSUM_LEN       = __ble_mx_api.CSUM_LEN;
const COMPRESS_TAG   = __ble_mx_api.COMPRESS_TAG;
const compress       = __ble_mx_api.compress;
const decompress     = __ble_mx_api.decompress;

const bt_btn  = document.getElementById('bt-btn');
const bt_btn2 = document.getElementById('bt-btn2');
const rx_msg  = document.getElementById('rx-msg');
const tx_msg  = document.getElementById('tx-msg');

const rx_msg_max = parseInt(rx_msg.getAttribute('rows'));

let rx_msgs = [];
let bt_rx_suspended = false;

let bt_conn = null;

function isConnected()
{
	return bt_char !== null;
}

function initPage()
{
	if (!navigator.bluetooth) {
		document.body.innerHTML = '<div class="alert-page">The Bluetooth is not supported in this browser. Please try another one.</div>';
		return;
	}
	bt_btn.textContent = 'Connect';
	bt_btn.onclick = onBtn;
	bt_btn2.onclick = onBtn2;
	bt_conn = new Connection(rx_cb, dual_mode);
	tx_msg.addEventListener('keypress', (e) => {
		if (e.keyCode == 13)
			bt_btn.click();
	});
}

function showMessage(msg)
{
	if (rx_msgs.length >= rx_msg_max)
		rx_msgs.shift();
	rx_msgs.push(msg);
	rx_msg.textContent = rx_msgs.join('\n');
}

function onDisconnection(device)
{
	tx_msg.disabled = true;
	rx_msg.disabled = true;
	bt_btn.disabled = true;
	bt_btn2.disabled = true;
	connectTo(device);
}

function do_receive(data)
{
	let str = DataView2str(data);
	console.debug('rx:', str);
	if (with_csum) {
		if (str.slice(-CSUM_LEN) != str_csum(str, str.length - CSUM_LEN)) {
			console.error('bad csum:', str);
			return;
		}
		str = str.slice(0, -CSUM_LEN);
	}
	if (!bt_rx_suspended)
		showMessage(str);
}

function rx_cb(data, is_binary=false)
{
	const len = data.byteLength;
	if (!with_csum || data.getUint8(len - 1) != COMPRESS_TAG) {
		do_receive(data);
		if (echo_mode)
			bt_conn.write(data, is_binary);
		return;
	}
	decompress(new DataView(data.buffer, 0, len - 1)).then(d => {
		console.log("unzip:", len - 1, '->', d.byteLength);
		do_receive(d);
		if (echo_mode)
			bt_conn.write(data, is_binary);
	})
	.catch((err) => {console.error('failed to decompress', err);});
}

function suspendRx(flag)
{
	bt_rx_suspended = flag;
	bt_btn2.textContent = flag ? 'Resume' : 'Suspend';
}

function onBTConnected(device)
{
	tx_msg.disabled = bt_btn.disabled = bt_conn.is_redonly();
	rx_msg.disabled = false;
	bt_btn.textContent = 'Send';
	bt_btn2.disabled = false;
	bt_btn2.classList.remove('hidden');
	suspendRx(bt_rx_suspended);
}

function connectTo(device)
{
	bt_conn.connect(device, onBTConnected, onDisconnection);
}

function doConnect(devname)
{
	console.log('doConnect', devname);
	bt_btn.disabled = true;
	let filters = [{services: [Connection.bt_svc_id]}];
	if (devname) {
		filters.push({name: devname});
	}
	return navigator.bluetooth.requestDevice({
		filters: filters,
	}).
	then((device) => {
		console.log(device.name, 'selected');
		connectTo(device);
	})
	.catch((err) => {
		console.error('Failed to discover BT devices');
		bt_btn.textContent = 'Connect';
		bt_btn.disabled = false;
	});
}

function txString(str)
{
	if (with_csum)
		str += str_csum(str);
	console.debug('tx:', str);
	bt_conn.write(str2Uint8Array(str));
}

function onBtn(event)
{
	if (bt_conn.is_connected())
		txString(tx_msg.value + '\r');
	else
		doConnect();
}

function onBtn2(event)
{
	suspendRx(!bt_rx_suspended);
}

initPage();

})();

