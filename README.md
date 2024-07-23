# esp32-ble
The collection of useful ESP32 BLE projects for Arduino targeting telemetry / monitoring applications.

## Simple receiver / transmitter
The ble_receiver / ble_transmitter are examples of creating one way communication channel with automatic reconnection. The code also illustrates using watchdog for improved reliability and maximizing transmission power for extending range.

The ble_uart_tx example adds receiving data for transmission from the (USB virtual) serial port, increasing the MTU and the ability to add a hexadecimal suffix to the device name so that it can be distinguished in case there are several devices around with the same firmware.

The ble_uart_rx example adds sending received data to hardware serial port, increasing the MTU and using hardware reset on watchdog timeout for better reliability.

## Using watchdog for better reliability
The BT stack is complex and not well tested bunch of software. Using it one can easily be trapped onto the state
where there is no way out. The biggest problem is that connect routine may hung forever. Although the connect call
has timeout parameter, it does not help. The call may complete on timeout without errors, but the connection will
not actually be established. That's why we are using watchdog to detect connection timeout. Its unclear if soft
reset by watchdog is equivalent to the power cycle or reset by pulling low EN pin. That's why there is an option
to implement hard reset on connect timeout by hard wiring some output pin to EN input of the chip.
See ble_uart_rx as an example of such approach implementation. There is an option to self reset receiver after
connection for testing, see SELF_RESET_AFTER_CONNECTED macro.

## Building and flashing
To be able to build this code examples add the following to Arduino Additional board manager URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Then go to Boards Manager and install **esp32 by Espressif Systems**.

In case you are failed to flash ESP32 board from Arduino do the following:
* press BOOT button
* short press RST button
* release BOOT button
* proceed with flashing in Arduino

## Testing
There are two python scripts for testing transmitter / receiver pair. The **test/transmit.py** opens serial port passed as parameter to the script and sends messages to it periodically. Each message has sequence number followed by the random data repeated twice so the receiver can verify message integrity and detect lost messages. The **test/receive.py** opens serial port passed as parameter to the script and parse messages at the receiver side of the connection validating them. It also prints various statistic when terminated by pressing Ctrl-C.

## Power consumption
The following values were measured with SDK v.3.0.3
| Mode     | Supply current |
|----------|----------------|
| Transmit | 55 mA          |
| Receive  | 60 mA          |
| Scan     | 95 mA          |

The power consumption was significantly improved since SDK v.2. Yet its still not quite suitable for battery powered applications.

## Range testing results
The maximum distance over which we can safely transmit data is an important issue in many applications. Typically small and cheap ESP32 modules have small chip antenna soldered on board. With such modules one can expect the opearating distance around 10 meters. The efficiency of such chip antenna is close to nothing. Even printed circuit antenna is better and gives you more range. The external antenna is much better choice. One can solder it just above the chip antenna as shown on the figure below.

![The ESP32 C3 Super mini module with extenal antenna](https://github.com/olegv142/esp32-ble/blob/main/doc/c3_supermini_with_antenna.jpg)

One can further increase operating range by setting maximum transmission power programmatically. I have tested two modules with external antennas like shown on the figure above with elevated TX power. This combination has demonstrated quite impressive results. Inside the building I've got stable transmission between basement and second floor through two layers of reinforced concrete slabs. Outdoors, it showed stable transmission at a distance of 100m with line of sight.
