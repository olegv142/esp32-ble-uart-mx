# esp32-ble (ble_uart_mx)
This is the multipurpose dual role BLE to serial bridge capable of creating multiple connections to other peripheral devices as well as acting as peripheral accepting connections from other central device. Its operation is controlled by the host via the same serial link as used for data transfers. Multiple compile time configuration options are provided to meet requirements in a variety of applications. For example it may be used for gathering telemetry data from some set of devices, providing communication link for commands / responses from controlling application or for creating bidirectional wireless communication channel between the pair of devices. It uses Arduino as building platform to keep code compact and make building and flashing as simple as possible. The adapter was tested on ESP32, ESP32C3, ESP32C6 and ESP32S3 with Espressif board support package version 3.0.3.

## Architecture and communication protocol

### Device roles
BLE devices may play two different roles. The peripheral role acts as a server providing access to its internal data to the central role acting as a client accessing that data remotely. Unlike many BLE to serial adapters that may be used in only one role at a time the **ble_uart_mx** adapter implements both roles and they can be used simultaneously. The peripheral role is typically used to provide wireless access for some computing device such as desktop or mobile phone. There is also a convenient possibility to access peripheral from browser which allows for creating cross platform web applications. The central role on the other hand may be used to access other peripherals. It may be used for wireless communications with one or more devices or just to create a bidirectional communication link with another adapter as a peripheral device. The **ble_uart_mx** adapter is capable of creating of up to 4 connections to peripheral devices while no more than one central device may create connection to it at the same time. The connection is always initiated by central device. To create connection the 6 byte MAC address of the destination peripheral is required.

### How it works
Technically the BLE peripheral device consists of a collection of services (we have only one). Each service is a collection of characteristics (we have only one). There are also descriptors but we omit them for clarity. The characteristic may be considered as data buffer accessible for reading and writing either locally or remotely. The central device does not have such rich internal structure. It is just able to establish connection to peripheral device in order to subscribe to characteristic updates and to be able to update it remotely. The peripheral device transmits its data by writing it to characteristic. The central device receives them by notification mechanism. The central device writes its data to the characteristic remotely. The peripheral is notified about remote write and receives data written by central. 

<p align="center">
  <img src="https://github.com/olegv142/esp32-ble/blob/main/doc/ble_data_flow.png?raw=true" width="70%" alt="BLE data flow"/>
</p>

### Serial protocol
The serial communication between controlling host and **ble_uart_mx** adapter takes place by sending and receiving messages as shown on the figure below. Every message begins with start marker shown as white circle and ends with end marker shown as black circle. While using hardware UART the start marker is represented by byte with the value of 1 while the end marker is represented by zero byte. While using ESP32 built-in USB CDC adapter the start marker is absent by default while the new line symbol plays the role of the end marker. The first variant is more robust while using new line as terminator simplify entering commands in terminal. The symbol after start marker (or the first message symbol if start marker is not used) determines the type of the input message. Symbols 0..3 indicate the index of the connection to peripheral device where the data that follows should be sent. The > symbols indicates that the data that follows should be sent to the connected central device. The # symbol indicates that the following symbol represents command. There are only 3 commands - reset (R), connect (C) to the set of addresses and advertise (A). The latter is only applicable in case the device was configured as hidden so advertising was not started automatically at startup.

![The bridge architecture and communication protocol](https://github.com/olegv142/esp32-ble/blob/main/doc/mx.png)

The output messages have similar structure. The first symbol after start marker determines the type of the message. Symbols 0..3 indicate the index of the connection to peripheral where data that follows were received. The < symbols indicates that the data that follows were received from the connected central device. The - symbol indicates the start of the debug message. The : symbol marks the status event. There are 3 kinds of status events. The idle event (I) is sent every second in idle state which means that the device was just reset and no connection was made yet. The version string sent with idle events consists of the 3 parts separated by '-' symbol. The first part is the version number, the second part is maximum data frame size, the third part is the set of capability symbols related to adapter configuration options. For example the version string **1.0-2160-X** indicates that the adapter with version 1.0 has maximum data frame size 2160 and using extended data frames. The connecting event (C) notifies user about initiating connection to the particular peripheral. The connected event (D) is sent every second if connections were successfully made to all peripherals listed in connect command.

### Binary data encoding
Since bytes with value 1 and 0 (or just newline symbol depending on the configuration) are used as message start / end markers passing binary data that may contain that bytes will break communication protocol. To allow for passing arbitrary binary data the following data encoding scheme is used. If host needs to pass binary data to device it encodes it into base64 encoding and adds prefix byte with the value 2. It plays the role of encoding marker telling the receiver that data that follows is base64 encoded. The adapter decodes such data and passes them over the air in binary form to avoid size overhead of base64 encoding. The following figure illustrates this schema.

<p align="center">
  <img src="https://github.com/olegv142/esp32-ble/blob/main/doc/mx_data_encoding.png?raw=true" width="50%" alt="BLE data flow"/>
</p>

On receiving data from the connected peer device the adapter checks if data contains reserved bytes with special meaning. If not then its safe to transmit them as plain text to serial channel. Otherwise the adapter encodes data to base64 and prepends encoding marker byte before sending data to serial channel. Since checking every received byte takes CPU time the binary data encoding may be disabled by undefining BINARY_DATA_SUPPORT option in configuration file if it is not required by a particular usage scenario.

### Extended data frames
The next big feature that is enabled by default is extended data frames. It adds the bunch of the following convenient features:
* checksums to detect data lost or corrupted in transit
* automatic large data frames fragmentation
* binary data support as described above

![Extended data frame fragment structure](https://github.com/olegv142/esp32-ble/blob/main/doc/ext_frame.png)

If extended data frames are enabled the adapter will split long data frames onto fragments automatically and transparently for the user. The adapter adds one byte header and 3 byte check sum to every such fragment as shown on the figure above. The header byte has 5 bit sequence number incremented on each fragment, the bit marking data as binary (so it should be encoded to base64 before sending to serial link) and two bits marking the first and the last fragment in the particular data frame (single fragment has them both set). At the end of each fragment is the 3 byte checksum. The checksum is cumulative. So each fragment checksum is calculated over that particular fragment data as well as all previous fragments data. The receiving adapter merges fragments and sends the original data frame to serial link if all check sums match. The maximum number of fragments (and so the maximum data frame size) may be configured at compile time. By default the maximum data frame size is 2160 bytes. One can further increase it by increasing MAX_CHUNKS in the configuration file.

### Simple link protocol
If the adapter is used to create simple communication link between two devices with automatic connection the complex protocol discussed above may be not necessary. One can reduce it to simplified version with only data messages by defining SIMPLE_LINK in the configuration file. This result in almost 'transparent' protocol with data messages enclosed between start (optional) and end tags as shown on the following figure. The binary data may be optionally encoded to base64 representation.   

![Simple link protocol](https://github.com/olegv142/esp32-ble/blob/main/doc/simple_link.png)

There is no indication of the data destination in the simple link protocol. So the adapter should be properly configured as central or peripheral device. The autoconnect target address (PEER_ADDR) should be set for central device.

### Stream tags
Its not uncommon to use serial communication link without hardware flow control just to save pins. Not to mention that ESP32 implementation of the USB CDC serial port does not have flow control at all. So the data may be easily lost in the serial link in case the receiving buffer capacity is exhausted. The stream tags feature helps to detect such data loss / corruption. Stream tags are two bytes immediately following the message start marker (if present) and preceding the message end marker. The first tag is 'opening'. Its value equals to the 0x40 (the code of the symbol @) plus ever incrementing message sequence number modulo 191. The second 'closing' tag value has message length added to the equation as shown by the following figure.  

<p align="center">
  <img src="https://github.com/olegv142/esp32-ble/blob/main/doc/stream_tags.png?raw=true" width="70%" alt="Stream tags"/>
</p>

One can define STREAM_TAGS in the configuration file to add stream tags to the protocol. With STREAM_TAGS defined the adapter is adding stream tags to the output serial data stream. It always able to recognise stream tags on input regardless of that macro definition. Yet with simple link protocol the stream tags must be present on input if and only if the STREAM_TAGS is defined since in simple link protocol the stream tags can't be descriminated from the data.

## Notes on data integrity
The very important question is what BLE stack guarantees regarding integrity of characteristic updates. Does connection state mean some set of guarantees which should be obeyed or connection should be closed by BLE stack? The TCP/IP stack for example follows such strict connection paradigm. The data is either delivered to other side of the connection or connection is closed. It turns out that the connection paradigm in BLE is much looser. The connection at least for the two stacks implementation available for ESP32 is just the context making communication possible but without any guarantees. Though the stack is tending to preserve the integrity and atomicity of the particular update sometimes its failed. Updates may be easily lost, duplicated, reordered or even altered. Yet in some cases the connection may be closed by the stack. But there are no guarantees of updates delivery while the connection is open. That's why its always recommended to use checksums appended transparently to the data when using extended frames. They greatly reduce the possibility of delivering corrupted data. Yet the data frames may still be lost or reordered.

The BLE link is not the only place where data may be corrupted. The serial data link between adapter and the controlling host may also drop or alter bytes transmitted. To reduce the probability of serial data corruption the following guidelines may be useful:
* Use parity bit while transmitting data via physical link (**ble_uart_mx** does it by default).
* Be aware of the possibility of the serial buffer overflow. Use at least RTS hardware flow control at ESP32 side of the connection to prevent its buffer overflow.
* Use stream tags to detect data loss in serial channel.
* Application should not rely solely on the transport layer. If integrity of the messages it is sending and receiving is critical the application should protect them by checksum so messages corrupted by transport layer may be detected.

## Design decisions and limitations

### Packed based communications
One may wander why there is no possibility to create transparent data link. The protocol offers packed based communications instead. By default the data packet size is limited to 2160 bytes while using extended frames or to 244 bytes otherwise. As was mentioned above the BLE stack does not guarantee delivery. Once the API uses some communication abstraction it should guarantee its integrity. To guarantee transparent data stream integrity is quite challenging task. We have to get acknowledges from the other side of the connection which will complicate implementation and slow it down significantly. In fact I don't know any transparent BLE serial adapter implementation that would guarantee data stream integrity. Implementing reliable communications over such unreliable data stream is a challenge. The only way to do it is to split data onto packets providing the means of detecting there boundaries even in the presence of corruptions and add some means of detecting corrupted packets. The adapter using packet based protocol solves the problem of splitting data to packets and maintaining their boundaries for the user. So in the end its more convenient than unreliable transparent data stream.

### Connect by addresses
The adapter does not provide the possibility to connect to target device by its name. The only way to identify the target is by MAC address consisting of 6 bytes. This is intentional since using device names does not work in case there are 2 devices with the same name. One may easily find device address by using **nRF Connect** application which is available for many platforms.

### Error handling
The adapter implements very simple but powerful error handling strategy. Should anything goes wrong it just resets itself. It helps to workaround potential problems with error handling in BLE stack. For example the connect routine may hung forever. Resetting is the only way to recover from such situations. The adapter also restarts itself on any peripheral disconnection (though there is a compile option enabling re-connection without restart in such cases).

## Building and flashing
To be able to build this code examples add the following to Arduino Additional board manager URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Then go to Boards Manager and install **esp32 by Espressif Systems**. Open **ble_uart_mx** project in Arduino. Select **ESP32C3/ESP32C6/ESP32S3 Dev Module** depending on your board and enable **USB CDC On Boot**. After that you can build and flash the adapter code.

The compilation options are placed onto the separate header **ble_uart_mx/mx_config.h** which includes **ble_uart_mx/user_config.h** which includes **ble_uart_mx/config/default.h**. With those options one can
* choose device name
* choose between USB CDC and hardware UART for communications
* configure hardware UART parameters (pins, flow control)
* configure connection status LED
* disable status and/or debug events or enable simple link protocol if user is interested in data events only
* configure device behavior, for example disable discovery
* configure auto-connecting on startup
* configure using extended data frames and/or stream tags
* setup debug options (TELL_UPTIME, ECHO)

Since configuration options are placed onto the separate file you may conveniently create you own file instead of **ble_uart_mx/config/default.h** or set of files for various device variants. The **ble_uart_mx/config/** folder contains the set of configuration files that may be used as starting points while creating your own configuration.

In case you are failed to flash ESP32 board from Arduino do the following:
* press BOOT button
* short press RST button
* release BOOT button
* proceed with flashing in Arduino

## Host API
The host API implementation for python may be found in **python/ble_multi_adapter.py**. It supports all protocol variants using either physical serial port or USB CDC.

## Testing

### The hard way
The **multi_echo_long.py** script in **python** folder is sending packets to other side that is expected to echo them back. One may use ECHO compilation option to echo data right on the device.

### The quick way
If you have only one ESP32 module and want to test **ble_uart_mx** adapter do the following:
* build and flash **ble_uart_mx** project by Arduino
* open Arduino Serial Monitor
* observe idle events
* open https://enspectr.github.io/ble-term in chrome browser
* press 'connect' to establish connection to your device
* try using Serial Monitor and BLE terminal application to send data in both directions

## Power consumption
The results of measuring idle power consumption with maximum and lowered CPU frequency are shown in the table below.  
| MCU | Max frequency | Min frequency (80MHz) |
|-------|----------------|--------------------|
|ESP32C3| 60mA           | 50mA               |
|ESP32C6| 68mA           | 60mA               |
|ESP32S3| 95mA           | 65mA               |

The power consumption under the load were measured in the following test. The central device was creating 3 active connections to peripheral devices each transmitting 50 short messages per second. There were two versions of the test. In the first version the central device did nothing with messages received. In the second version the central device was sending them back to peripheral devices. Results are shown on the following figure.

<p align="center">
  <img src="https://github.com/olegv142/esp32-ble/blob/main/doc/power_consumption.png?raw=true" width="70%" alt="Power consumption with 3 active connections"/>
</p>
The power consumption was significantly improved since SDK v.2. Yet its still not quite suitable for battery powered applications.

## Range testing results
The maximum distance over which we can safely transmit data is an important issue in many applications. Typically small and cheap ESP32 modules have tiny chip antenna soldered on board. With such modules one can expect the operating distance around 10 meters. One can further increase operating range by setting maximum transmission power programmatically. Such power boost is enabled by default in the dapter configuration (TX_BOOST). Yet the ESP32C3 Super Mini modules demonstrated rather low range around 20m even with power boost enabled. The investigation have shown that its not bad antenna that makes receiption weaker than expected. The antenna placement was just choosen improperly. The first rule that is typically violated on all compact boards is placing antenna perpendicular to the edge of the ground polygon. Worse that on ESP32C3 Super Mini the antenna is placed along the edge of the ground polygon with minimal distance to it. So most of the transmitter power were absorbed by the ground plane and converted to the heat rather than electromagnetic radiation. To fix that I've unsoldered antennas and solder them back rotated by 90 degrees as shown on the figure below. As a result the range was vastly improved from 20 to 100 meters.

<p align="center">
  <img src="https://github.com/olegv142/esp32-ble/blob/main/doc/chip_ant_mod.jpg?raw=true" width="70%" alt="Chip antenna improved"/>
</p>

Another possibility is to remove chip antenna and solder external antenna as shown on the figure below. Be aware that chip antenna is fed from one side only. Another side is not connected to anything. So take care to solder cable shield to the ground. Failure to do it may greatly increase power consumption of the module, cause its overheating and damage.

<p align="center">
  <img src="https://github.com/olegv142/esp32-ble/blob/main/doc/ext_ant_.jpg?raw=true" width="50%" alt="External antenna wiring"/>
</p>

Two modules with external monopole antennas soldered this way have demonstrated the same 100м range as modules with chip antennas in the right orientation. Interestingly the best range of about 150+ meters was demonstrated by WeAct ESP32C3 Core boards with printed circuit antenna.

## Connection state indicator
The connection state indicator is very useful feature for testing and debugging. The adapter is able to use either plain LED or serially controlled RGB led (aka neo pixel) as connection state indicator. The RGB LED is more informative so the boards with such LED are more preffereable. 

## Interoperability
The adapters may be used either to connect to the similar adapter or another BLE adapter or application (Web BLE in particular). Note that using extended data frames requires decoding/encoding them at the other side of the connection if its not using the same **ble_uart_mx** adapter. Though this feature may be disabled at compile time. Apart from that the adapter works flawlessly with Web BLE applications.

Another popular Chinese BLE adapter JDY-08 (https://github.com/olegv142/esp32-ble/blob/main/doc/JDY-08.pdf) may be used as peripheral device with **ble_uart_mx** adapter acting as central. Note though that it splits data stream onto chunks with up to 20 bytes each. An attempt to send more than 20 bytes to JDY-08 will fail.

## Known issues
The main fundamental issue with BLE regarding data transmission is the lack of the flow control. To transmit the particular data fragment the peripheral issues notification which is absolutely asynchronous (aka 'fire and forget'). The BLE stack provides the possibility to notify synchronously but its slow and so rarely used. Without flow control the capacity of BLE link may be easily exhausted. This results in an increased number of lost/corrupted BLE characteristic updates, which manifests itself as lost/corrupted data frames. So pushing adapter throughput to the limit is not recommended. The data rate should be limited by the sender. The best usage pattern is sending limited amount of data periodically.

Using adapter with dual peripheral / central roles simultaneously makes the probability of data loss even higher. Possibly it is expected behavior. The central device is expected to schedule radio receive / transmit intervals for itself and for connected peripheral. So working with the same radio in two roles simultaneously is inherently problematic.

Another issue observed in tests is the possibility of deadlock while using CTS/RTS flow control in both directions. Its possible that both adapter and the host are trying to write to the serial channel while their receiving buffers are full. In such case both sides can't make progress. The adapter is able to recover from such freeze due to watchdog which reset it after 20 seconds of main loop inactivity. Yet both CTS/RTS are rarely used, in most cases hardware flow control is not used at all.

## Other experimental projects
A bunch of experimental projects created mostly for testing during the work on this project are located in **simple** folder.

## Useful links

The BLE terminal web page example: https://github.com/enspectr/ble-term

Dual mode Bluetooth to serial adapter based on ESP32: https://github.com/olegv142/esp32-bt-serial

nRF Connect for Mobile - the tool for exploring BLE devices: https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp
