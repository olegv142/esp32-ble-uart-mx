# esp32-ble (ble_uart_mx)
This is the multipurpose dual role BLE to serial bridge capable of creating multiple connections to other peripheral devices as well as acting as peripheral accepting connections from other central device. Its operation is controlled by the host via the same serial link as used for data transfers. Multiple compile time configuration options are provided to meet requirements in a variety of applications. For example it may be used for gathering telemetry data from some set of devices, providing communication link for commands / responses from controlling application or for creating bidirectional wireless communication channel between the pair of devices. It uses Arduino as building platform to keep code compact and make building and flashing as simple as possible. The adapter was tested on ESP32C3 Super Mini modules with Espressif board support package version 3.0.3.

## Architecture and communication protocol

### Device roles
BLE devices may play two different roles. The peripheral role acts as a server providing access to its internal data to the central role acting as a client accessing that data remotely. Unlike many BLE to serial adapters that may be used in only one role at a time the **ble_uart_mx** adapter implements both roles and they can be used simultaneously. The peripheral role is typically used to provide wireless access for some computing device such as desktop or mobile phone. There is also a convenient possibility to access peripheral from browser which allows for creating cross platform web applications. The central role on the other hand may be used to access other peripherals. It may be used for wireless communications with one or more devices or just to create a bidirectional communication link with another adapter as a peripheral device. The **ble_uart_mx** adapter is capable of creating of up to 8 connections to peripheral devices while no more than one central device may create connection to it at the same time. The connection is always initiated by central device. To create connection the 6 byte MAC address of the destination peripheral is required.

### How it works
Technically the BLE peripheral device consists of a collection of services (we have only one). Each service is a collection of characteristics (we have only one). There are also descriptors but we omit them for clarity. The characteristic may be considered as data buffer accessible for reading and writing either locally or remotely. The central device does not have such rich internal structure. It is just able to establish connection to peripheral device in order to subscribe to characteristic updates and to be able to update it remotely. The peripheral device transmits its data by writing it to characteristic. The central device receives them by notification mechanism. The central device writes its data to the characteristic remotely. The peripheral is notified about remote write and receives data written by central. 

<p align="center">
  <img src="https://github.com/olegv142/esp32-ble/blob/main/doc/ble_data_flow.png?raw=true" width="70%" alt="BLE data flow"/>
</p>

### Serial protocol
The serial communication between controlling host and **ble_uart_mx** adapter takes place by sending and receiving messages as shown on the figure below. Every message begins with start marker shown as white circle and ends with end marker shown as black circle. While using hardware UART the start marker is represented by byte with the value of 1 while the end marker is represented by zero byte. While using ESP32 built-in USB CDC adapter the start marker is absent while the new line symbol plays the role of the end marker. The symbol after start marker (the first message symbol if USB CDC is used) determines the type of the input message. Symbols 0..7 indicate the index of the connection to peripheral device where the data that follows should be sent. The > symbols indicates that the data that follows should be sent to the connected central device. The # symbol indicates that the following symbol represents command. There are only 2 commands - reset (R) and connect (C) to the set of addresses.

![The bridge architecture and communication protocol](https://github.com/olegv142/esp32-ble/blob/main/doc/mx.png)

The output messages have similar structure. The first symbol after start marker determines the type of the message. Symbols 0..7 indicate the index of the connection to peripheral where data that follows were received. The < symbols indicates that the data that follows were received from the connected central device. The - symbol indicates the start of the debug message. The : symbol marks the status event. There are 3 kinds of status events. The idle event (I) is sent every second in idle state which means that the device was just reset and no connection was made yet. The version string sent with idle events consists of the 3 parts separated by '-' symbol. The first part is the version number, the second part is maximum data frame size, the third part is the set of capability symbols related to adapter configuration options. For example the version string **1.0-2160-X** indicates that the adapter with version 1.0 has maximum data frame size 2160 and using extended data frames. The connecting event (C) notifies user about initiating connection to the particular peripheral. The connected event (D) is sent every second if connections were successfully made to all peripherals listed in connect command.

### Binary data encoding
Since bytes with value 1 and 0 are used as message start / end markers passing binary data that may contain that bytes will break communication protocol. To allow for passing arbitrary binary data the following data encoding scheme is used. If host needs to pass binary data to device it encodes it into base64 encoding and adds prefix byte with the value 2. It plays the role of encoding marker telling the receiver that data that follows is base64 encoded. The adapter decodes such data and passes them over the air in binary form to avoid size overhead of base64 encoding. The following figure illustrates this schema.

<p align="center">
  <img src="https://github.com/olegv142/esp32-ble/blob/main/doc/mx_data_encoding.png?raw=true" width="50%" alt="BLE data flow"/>
</p>

On receiving data from the connected peer device the adapter checks if data contains bytes with values 0, 1, 2. If not then its safe to transmit them as plain text to serial channel. Otherwise the adapter encodes data to base64 and prepends encoding marker byte before sending data to serial channel. Since checking every received byte takes CPU time the binary data encoding may be disabled by undefining BINARY_DATA_SUPPORT option in configuration file if it is not required by a particular usage scenario.

### Extended data frames
The next big feature that is enabled by default is extended data frames. It adds the bunch of the following convenient features:
* checksums to detect data lost or corrupted in transit
* automatic large data frames fragmentation
* binary data support as described above

![Extended data frame fragment structure](https://github.com/olegv142/esp32-ble/blob/main/doc/ext_frame.png)

If extended data frames are enabled the adapter will split long data frames onto fragments automatically and transparently for the user. The adapter adds one byte header and 3 byte check sum to every such fragment as shown on the figure above. The header byte has 5 bit sequence number incremented on each fragment, the bit marking data as binary (so it should be encoded to base64 before sending to serial link) and two bits marking the first and the last fragment in the particular data frame (single fragment has them both set). At the end of each fragment is the 3 byte checksum. The checksum is cumulative. So each fragment checksum is calculated over that particular fragment data as well as all previous fragments data. The receiving adapter merges fragments and sends the original data frame to serial link if all check sums match. The maximum number of fragments (and so the maximum data frame size) may be configured at compile time. By default the maximum data frame size is 2160 bytes. One can further increase it by increasing MAX_CHUNKS in configuration file.

## Notes on data integrity
The very important question is what BLE stack guarantees regarding integrity of characteristic updates. Does connection state mean some set of guarantees which should be obeyed or connection should be closed by BLE stack? The TCP/IP stack for example follows such strict connection paradigm. The data is either delivered to other side of the connection or connection is closed. It turns out that the connection paradigm in BLE is much looser. The connection at least for the two stacks implementation available for ESP32 is just the context making communication possible but without any guarantees. Though the stack is tending to preserve the integrity and atomicity of the particular update sometimes its failed. Updates may be easily lost, duplicated, reordered or even altered. Yet in some cases the connection may be closed by the stack. But there are no guarantees of updates delivery while the connection is open. That's why its always recommended to use checksums appended transparently to the data when using extended frames. They greatly reduce the possibility of delivering corrupted data. Yet the data frames may still be lost or reordered.

The BLE link is not the only place where data may be corrupted. The serial data link between adapter and the controlling host may also drop or alter bytes transmitted. To reduce the probability of serial data corruption the following guidelines may be useful:
* Use parity bit while transmitting data via physical link (**ble_uart_mx** does it by default).
* Be aware of the possibility of receiver buffer overflow. This is especially likely to happen when using USB to serial adapter chips typically having rather small (128 byte) buffer. Use at least CTS hardware flow control at ESP32 side of the connection to prevent other side buffer overflow.
* Be aware that during connecting to peripheral the adapter does not process data from serial port. So passing large amount of data to the adapter while its connecting may overflow serial buffer at ESP32 side. To prevent this scenario one may either use RTS flow control or keep track connecting/connected events to suspend data transmission while adapter is connecting.
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
Then go to Boards Manager and install **esp32 by Espressif Systems**. After that you can open **ble_uart_mx** project in Arduino and build it.

The compilation options are placed onto the separate header **ble_uart_mx/mx_config.h**. With those options one can
* choose device name
* choose between USB CDC and hardware UART for communications
* configure hardware UART parameters (pins, flow control)
* disable status and/or debug events if user is interested in data events only
* configure device behavior, for example disable discovery
* configure auto-connecting on startup
* configure using extended data frames or binary data encoding
* setup debug options (TELL_UPTIME, PEER_ECHO)

Since configuration options are placed onto the separate file you may conveniently create you own file or set of files for various device variants.

In case you are failed to flash ESP32 board from Arduino do the following:
* press BOOT button
* short press RST button
* release BOOT button
* proceed with flashing in Arduino

## Host API
The host API implementation for python may be found in **python/ble_multi_adapter.py**. Currently only hardware serial link is supported. Note that USB CDC link typically receives some debug information during ESP32 boot so its useful mostly for testing and debugging (unless the ESP32 chip has another USB adapter as S3 for example).

## Testing

### The hard way
There are pair of test scripts **multi_echo.py** and **multi_echo_long.py** in **python** folder sending packets to other side that is expected to echo them back. One may use PEER_ECHO compilation option to echo data right on the device or use **central_echo.py** script for that purpose.

### The quick way
If you have only one ESP32 module and want to test **ble_uart_mx** adapter do the following:
* enable using built-in USB CDC by commenting out HW_UART define in mx_config.h
* build and flash **ble_uart_mx** project by Arduino
* open Arduino Serial Monitor
* observe idle events
* open https://enspectr.github.io/ble-term in chrome browser
* press 'connect' to establish connection to your device
* try using Serial Monitor and BLE terminal application to send data in both directions

## Power consumption
With ESP32C3 one can expect the power current of around 65mA. The power consumption was significantly improved since SDK v.2. Yet its still not quite suitable for battery powered applications.

## Range testing results
The maximum distance over which we can safely transmit data is an important issue in many applications. Typically small and cheap ESP32 modules have tiny chip antenna soldered on board. With such modules one can expect the operating distance around 10 meters. The efficiency of such chip antenna is close to nothing. Even printed circuit antenna is better and gives you more range. The external antenna is much better choice. One can solder it instead of the chip antenna as shown on the figure below.

![The ESP32 C3 Super mini module with external antenna](https://github.com/olegv142/esp32-ble/blob/main/doc/c3_supermini_with_antenna.jpg)

One can further increase operating range by setting maximum transmission power programmatically (TX_PW_BOOST option in mx_config.h). I have tested two modules with external antennas like shown on the figure above with elevated TX power. Inside the building I've got stable transmission between adjacent floors through the layer of reinforced concrete slabs. Outdoors, it showed stable transmission at a distance of 120m with line of sight.

## Interoperability
The adapters may be used either to connect to the similar adapter or another BLE adapter or application (Web BLE in particular). Note that using extended data frames requires decoding/encoding them at the other side of the connection if its not using the same **ble_uart_mx** adapter. Though this feature may be disabled at compile time. Apart from that the adapter works flawlessly with Web BLE applications.

Another popular Chinese BLE adapter JDY-08 (https://github.com/olegv142/esp32-ble/blob/main/doc/JDY-08.pdf) may be used as peripheral device with **ble_uart_mx** adapter acting as central. Note though that it splits data stream onto chunks with up to 20 bytes each. An attempt to send more than 20 bytes to JDY-08 will fail.

## Known issues
The main fundamental issue with BLE regarding data transmission is the lack of the flow control. To transmit the particular data fragment the peripheral issues notification which is absolutely asynchronous (aka 'fire and forget'). The BLE stack provides the possibility to notify synchronously but its slow and so rarely used. Without flow control the capacity of BLE link may be easily exhausted. So pushing throughput to the limit is not recommended. The data rate should be limited by the sender. The best usage pattern is sending limited amount of data periodically.

The first symptom of exhausting BLE link capacity is loosing characteristic update notifications which manifests itself as data frames delivery failures. Another issue is UART buffer overflow which may lead to code execution freezing for unknown reason. The adapter is able to recover from such freeze due to watchdog which reset it after 20 seconds of main loop inactivity. So if you need to transmit large bursts of the data make sure the UART buffer is able to accommodate that burst at the receiver. The buffer size is declared as UART_BUFFER_SZ (4096 bytes by default) in the configuration file. You can adjust this size according your data burst size. Be aware that binary data are encoded to base64 before placing them to serial buffer which increases data size by the factor of 4/3.

## Other experimental projects
A bunch of experimental projects created mostly for testing during the work on this project are located in **simple** folder.

## Useful links

The BLE terminal web page example: https://github.com/enspectr/ble-term

Dual mode Bluetooth to serial adapter based on ESP32: https://github.com/olegv142/esp32-bt-serial

nRF Connect for Mobile - the tool for exploring BLE devices: https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp
