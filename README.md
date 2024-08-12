# esp32-ble
The collection of useful ESP32 BLE projects for Arduino targeting telemetry / monitoring applications. The main goals were testing various scenarios of using BLE stack in order to reveal its limitations and find workarounds. To avoid reading details go ahead to the [conclusion](#conclusion).

## Simple receiver / transmitter examples
The **ble_receiver** / **ble_transmitter** are examples of creating one way communication channel with automatic re-connection. The code also illustrates using watchdog for improved reliability and maximizing transmission power for extending range.

The **ble_uart_tx** example adds receiving data for transmission from the (USB virtual) serial port, increasing the MTU and the ability to add a hexadecimal suffix to the device name so that it can be distinguished in case there are several devices around with the same firmware.

The **ble_uart_rx** example adds sending received data to hardware serial port, increasing the MTU and using hardware reset on watchdog timeout for better reliability. It also has the option to establish connection to the transmitter without scan in case its address is known.

## Dual role example
The **ble_uart_dual** example illustrates creating dual role device capable of connecting to the transmitter as central and acting as peripheral accepting connections from other central. Here we start to hit BLE stack limitations / bugs. In particular the BLEServerCallbacks methods should be called on every connect / disconnect of the remote central. It turns out that they also called when we act as central and connecting to the remote peripheral. Hopefully we can workaround this issue by just not strictly relying on that callbacks.

## Multi-adapter
The **ble_uart_multi** is dual role device capable of creating multiple connections to transmitters as central as well as acting as peripheral accepting connections from other central. It was designed as multipurpose BLE to serial adapter accepting commands from controlling host via serial link. The primary use case is gathering telemetry data from transmitters and providing communication link for other central for commands / responses. Here we hit even more stack limitations / bugs. For example the BLEDevice maintains the pointer to the last created client connection. It is considered current and used to deliver some events from lower layers of the stack. As a consequence some events may not be delivered in case several connections were created. In particular RSSI query will hung forever if made to not the last created client. Hopefully we can workaround this issue by not querying RSSI or doing it once right after connect.
The host code controlling such adapter may be found in **python/ble_multi_adapter.py**.

## Using watchdog for better reliability
The BT stack is complex and not well tested bunch of software. Using it one can easily be trapped onto the state
where there is no way out. The biggest problem is that connect routine may hung forever. Although the connect call
has timeout parameter, it does not help. The call may complete on timeout without errors, but the connection will
not actually be established. That's why we are using watchdog to detect connection timeout. Its unclear if soft
reset by watchdog is equivalent to the power cycle or reset by pulling low EN pin. That's why there is an option
to implement hard reset on connect timeout by hard wiring some output pin to EN input of the chip.
See **ble_uart_rx** as an example of such approach implementation. There is an option to self reset receiver after
connection for testing, see SELF_RESET_AFTER_CONNECTED macro.

## Serial data flow control
Whenever data is transmitted over serial channel its important to ensure the receiver is able to process data we are sending to it. Otherwise the data sent may be silently lost. When using UART the receiver is typically set RTS or DTR signal low to indicate its ready to process new data. To implement flow control this signal should be connected to CTS input of the transmitter and the latter should be configured to use it. The **ble_uart_rx** code has an example of such configuration. Note that if serial data receiver is unable to process the data (for ex. the serial port is not open) the transmitter will just hung on writing the new data. In such case **ble_uart_rx** will be reset by watchdog.

The USB virtual serial port used by **ble_uart_tx** example also has potential issues related to flow control. The problem is that Arduino implementation of such serial port does not have flow control at all. The host may keep sending data even in case the receiving buffer in ESP32 is full so the new data will be silently dropped. The buffer size is 256 bytes by default. The **ble_uart_tx** example increases this buffer up to 4096 bytes to avoid potential overflow issues. Note that the possibility to loose data in a buffer still exists but yet the data may be lost in BLE transmission anyway.

## Notes on data integrity
In general there are two possible data integrity models that can be implemented in the transmission channel. The strongest is data stream integrity where data can't be lost or reordered across the entire data stream (just like TCP/IP). The implementation of such integrity model is hard and typically not needed. The second model is message integrity when the message of limited length is either delivered unmodified or not delivered at all. So messages may be dropped or reordered but can't be modified (just like UDP). To implement such integrity model over unreliable data link one should take care of the following:
* There should be means of detecting message boundaries in the data stream even if the stream is corrupted
* There should be means of validating message integrity so the modified messages will be dropped

The test scripts in the **python** folder implement message boundary detection by enclosing message between start/end markers that can't be present in the message body. The test scripts detect corrupt messages by repeating message payload twice in the message body. The less space consuming approach typically used is to add checksum to the message.

The **ble_uart_rx** uses its own start/end markers while sending received data fragments to the serial port. It also sends empty start/end marker pair after connect / re-connect as the start of the stream tag. The receiving application may use start of the stream tag to drop buffered incomplete messages received before disconnection. See **python/receive.py** for example of the stream parsing. If you don't want **ble_uart_rx** to send start/end markers, undefine UART_BEGIN and UART_END in the code. After that **ble_uart_rx** will behave as transparent serial port.

## Lost updates detection
The BLE transmits data by updating the so called *characteristic*. So the data are delivered in chunks with every chunk corresponding to the particular characteristic update. In theory updates should be delivered in order or the communication channel should go to disconnected state. In practice this assertion may be violated due to the bugs in the complex BLE stack implementation. See **Notes about NimBLE** for discussion of the testing results. To be able to detect missing updates the transmitter inserts sequence tag as the first symbol of the characteristic value. The sequence tag is assigned a values from 16 characters sequence 'a', 'b', .. 'p'. The next update uses next letter as sequence tag. The 'p' letter is followed by the 'a' again. The sequence tag symbol is followed by the data to be transmitted. The receiving application may use sequence tags to detect lost chunks of data transmitted or just ignore them.  See **python/receive.py** for example of the tags validation and missing updates detection. If you don't need sequence tags, undefine USE_SEQ_TAG in **ble_uart_tx.ino** code. Note that using sequence tags by itself does not guarantee data integrity. They just help to identify the root cause of the data stream corruption during testing.

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
There are two python scripts for testing **ble_uart_tx** / **ble_uart_rx** pair. The **python/transmit.py** opens serial port passed as parameter to the script and sends messages to it periodically. Each message has sequence number followed by the random data repeated twice so the receiver can verify message integrity and detect lost messages. The **python/receive.py** opens serial port passed as parameter to the script and parse messages at the receiver side of the connection validating them. It also prints various statistic when terminated by pressing Ctrl-C. 

## Power consumption
The following values were measured with SDK v.3.0.3
| Mode     | Supply current |
|----------|----------------|
| Transmit | 55 mA          |
| Receive  | 60 mA          |
| Scan     | 95 mA          |

The power consumption was significantly improved since SDK v.2. Yet its still not quite suitable for battery powered applications.

## Range testing results
The maximum distance over which we can safely transmit data is an important issue in many applications. Typically small and cheap ESP32 modules have tiny chip antenna soldered on board. With such modules one can expect the operating distance around 10 meters. The efficiency of such chip antenna is close to nothing. Even printed circuit antenna is better and gives you more range. The external antenna is much better choice. One can solder it instead of the chip antenna as shown on the figure below.

![The ESP32 C3 Super mini module with external antenna](https://github.com/olegv142/esp32-ble/blob/main/doc/c3_supermini_with_antenna.jpg)

One can further increase operating range by setting maximum transmission power programmatically. I have tested two modules with external antennas like shown on the figure above with elevated TX power. This combination has demonstrated quite impressive results. Inside the building I've got stable transmission between basement and second floor through two layers of reinforced concrete slabs. Outdoors, it showed stable transmission at a distance of 120m with line of sight.

## Notes about NimBLE
The **nim_ble_uart_rx** / **nim_ble_uart_tx** are two examples adopted for using NimBLE stack instead of Bluedroid used by default. The NimBLE stack has 2 times smaller amount of code, much less RAM usage and better written in general. For example there are no such stupid things in API as passing class instances with a lot of data by value. Nevertheless testing has revealed severe problems related to using NimBLE stack:
1. The automatic gain control does not work. Placing transmitter close to receiver severe disrupts communications.
2. There is no guarantee of the characteristic update delivery. Typically if the communication channel is connected that means updates from one side of the channel are guaranteed to be delivered to other side in the original order. So we can assume that the update is either delivered or channel is switched to the disconnected state. At least this is the case for Bluedroid stack. Note that this does mean we should not care about data integrity. Bugs breaking the assertion just mentioned are always possible. For the case of NimBLE stack we can't assume anything about updates delivery at all. Updates may be lost anytime but channel is still kept in connected state.
3. In case of bad reception the communication channel may become permanently broken. It can fall to some pathological state where even re-connection does not repair communication. Only transmitter reset is able to repair it so the data may be transmitted again.

So for now considering all issues mentioned above I strongly not recommend using NimBLE stack for anything more complex than LED blinking.

## Notes about bidirectional communications
One might wonder what about a bidirectional communication channel. Why is there no code for a pair of devices that can simply create a bidirectional virtual serial link. The answer is because there are no simple way to do it reliably with the current Espressif SDK.

The serial communication from peripheral to the central works by updating characteristic and notifying central. This approach works well. It seems that notification routine is synchronous which means it does not return until new value is delivered to the central. The common approach to communication in backward direction (from central to peripheral) is to remotely write to the same or some other characteristic. Unfortunately testing shows that with current SDK this approach leads to data loss and/or duplication.

The test **python/multi_echo.py** works with **ble_uart_multi** example by sending bursts of messages to connected central every second. Each message contains the string representation of ever incrementing sequence counter. The central just sends back every message it receives. One can run such central in browser by following the link https://enspectr.github.io/ble-term/?echo. Results for burst length of 3 are shown on the following figure where browser window on the left shows messages received by central while terminal window on the right shows echo replies received by ESP32C3 module running **ble_uart_multi** code. While messages received by central (running in browser) looks perfectly valid the replies (that should be identical to the messages received by central) contains a lot of missed and duplicated messages.

![BLE write bug in echo test](https://github.com/olegv142/esp32-ble/blob/main/doc/ble_write_bug_in_echo_test.jpg)

The root cause of this issue is asynchronous write notification mechanism implemented in ESP32. The value written by central is stored in the buffer while notification callback is scheduled for execution at some later time. Before actual callback execution the value in the buffer may be overwritten by some other write. As a result overwritten value will be lost while several notification callbacks will deliver the same value most recently written. There is no simple workaround for this issue. The one that is feasible is to implement separate characteristic representing written characteristic version incremented on each write. The writer can subscribe to its updates and delay writing the next value until it receives confirmation of delivery of the previous value. But for now it looks overly complex.

## Conclusion
Despite years of development Espressif BLE stack works without issues only in very basic situations. Even such common task as creating bidirectional virtual serial port has no simple and reliable solution.

## Useful links

The BLE receiver web page example: https://github.com/enspectr/ble-term

Dual mode Bluetooth to serial adapter based on ESP32: https://github.com/olegv142/esp32-bt-serial
