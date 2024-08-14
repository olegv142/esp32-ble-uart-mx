# esp32-ble (ble_uart_mx)
This is the multipurpose dual role BLE to serial bridge capable of creating multiple connections to other peripheral devices as well as acting as peripheral accepting connections from other central device. Its operation is controlled by host via the same serial link as used for data transfers. Multiple compile time configuration options are provided to meet requirements in a variety of applications. For example it may be used for gathering telemetry data from some set of devices, providing communication link for commands / responses from controlling application or for creating bidirectional wireless communication channel between the pair of devices. It uses Arduino as building platform to keep code small and make using it as simple as possible. The adapter was tested on ESP32C3 Super Mini modules with Espressif board support package version 3.0.3.

## Architecture and communication protocol
BLE devices may play two different roles. The peripheral role acts as a server providing access to its internal data to the central role acting as a client accessing that data remotely. Unlike many BLE to serial adapters having only one role the **ble_uart_mx** adapter implements both roles. The peripheral role is typically used to provide wireless access for some computing device such as desktop or mobile phone. There is also a convenient possibility to access peripheral from browser which allows for creating cross platform web applications. The central role on the other hand may be used to access other peripherals. It may be used for wireless communications with one or more devices or just to create a bidirectional communication link with another adapter as a peripheral device. The **ble_uart_mx** adapter is capable of creating of up to 8 connections to peripheral devices while nor more than one central device may create connection to it at the same time. The connection is always initiated by central device. To create connection the 6 byte MAC address of the destination peripheral is required.

![The bridge architecture and communication protocol](https://github.com/olegv142/esp32-ble/blob/main/doc/mx.png)

The serial communication between controlling host and **ble_uart_mx** adapter takes place by sending and receiving messages as shown on the figure above. Every message begins with start marker shown as white circle and ends with end marker shown as black circle. While using hardware UART the start marker is represented by byte with the value of 1 while the end marker is represented by zero byte. While using ESP32 built-in USB CDC adapter the start marker is absent while the new line symbol plays the role of the end marker. The symbol after start marker (the first message symbol if USB CDC is used) determines the type of the input message. Symbols 0..7 indicate the index of the connection to peripheral device where the data that follows should be sent. The > symbols indicates that the data that follows should be sent to the connected central device. The # symbol indicates that the following symbol represents command. There are only 2 commands - reset (R) and connect (C) to the set of addresses.

The output messages have similar structure. The first symbol after start marker determines the type of the message. Symbols 0..7 indicate the index of the connection to peripheral where data that follows were received. The < symbols indicates that the data that follows were received from the connected central device. The - symbol indicates the start of the debug message. The : symbol marks the status event. There are 3 kinds of status events. The idle event (I) is sent every second in idle state which means that the device was just reset and no connection was made yet. The connecting event (C) notifies user about initiating connection to the particular peripheral. The connected event (D) is sent every second if connections were successfully made to all peripherals listed in connect command.

## How it works
Technically the BLE peripheral device consists of a collection of services (we have only one). Each service is a collection of characteristics (we have only one). There are also descriptors but we omit them for clarity. The characteristic may be considered as data buffer accessible for reading and writing either locally or remotely. The central device does not have such reach internal structure. It is just able to establish connection to peripheral device in order to subscribe to characteristic updates and be able to update it remotely. The peripheral device transmits its data by writing it to characteristic. The central device receives them by notification mechanism. The central device writes its data to the characteristic remotely. The peripheral is notified about remote write and receives data written by central. 

<p align="center">
  <img src="https://github.com/olegv142/esp32-ble/blob/main/doc/ble_data_flow.png?raw=true" width="70%" alt="BLE data flow"/>
</p>

## Data integrity
The very important question is what BLE stack guarantees regarding integrity of characteristic updates. Does connection state mean some set of guarantees which should be obeyed or connection should be closed by BLE stack? The TCP/IP stack for example follows such strict connection paradigm. The data is either delivered to other side of the connection or connection is closed. It turns out that the connection paradigm in BLE is much looser. The connection at least for the two stacks implementation available for ESP32 is just the context making communication possible but without any guarantees except the atomicity and integrity of the particular characteristic update. That means that if the update is delivered to the other side of the connection, it is delivered unchanged. But updates may be easily lost or duplicated. Yet in some cases the connection may be closed by the stack. But there are no guarantees of updates delivery while the connection is open.

## Design decisions and limitations
### Packed based communications
One may wander why there is no possibility to create transparent data link. The protocol offers packed based communications instead with data packet size limited by 512 bytes.
As was mentioned above the BLE stack does not guarantee delivery. The only thing it does guarantee is integrity of the single characteristic update with size limited to 512 bytes.
Once the API uses some communication abstraction it should guarantee its integrity. To guarantee transparent data stream integrity is quite challenging task. We have to get acknowledges from the other side of the connection which will complicate implementation and slow it down significantly. In fact I don't know any transparent BLE serial adapter implementation that would guarantee data stream integrity. Implementing reliable communications over such unreliable data stream is a challenge. The only way to do it is to split data onto packets providing the means of detecting there boundaries even in the presence of corruptions and add means of detecting corrupted packets.
On the other hand using packets that fit to the characteristic guarantees packet integrity (but does not guarantee delivery). The packet boundaries are preserved so user does not have to create its own way of detecting packet boundaries. So if the user is taking care about data integrity the packet based protocol is more convenient than unreliable transparent data stream.

### Connect by addresses
The adapter does not provide the possibility to connect to target device by its name. The only way to identify the target is by MAC address consisting of 6 bytes. This is intentional since using device names does not work in case there are 2 devices with the same name. One may easily find device address by using **nRF Connect** application which is available for many platforms.

### Error handling
The adapter implements very simple but powerful error handling strategy. Should anything goes wrong it just reset itself. It helps to workaround potential problems with error handling in BLE stack. For example the connect routine may hung forever. Resetting is the only way to recover from such situations.

### No binary data support
Since bytes with value 1 and 0 are used as message start / end markers passing binary data that may contain that bytes will break communication protocol. If passing binary data is mandatory user may apply base64 encoding to the data.

## Building and flashing
To be able to build this code examples add the following to Arduino Additional board manager URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Then go to Boards Manager and install **esp32 by Espressif Systems**. Open **ble_uart_mx** project in Arduino and build it.

The compilation options are placed onto the separate header **ble_uart_mx/mx_config.h**. With those options one can
* choose device name
* choose between USB CDC and hardware UART for communications
* configure hardware UART flow control
* disable status and/or debug events if user is interested in data events only
* configure device behavior, for example disable discovery
* configure auto-connecting on startup
* setup debug options (TELL_UPTIME, PEER_ECHO)

Since configuration options are placed onto the separate file you may conveniently create you own file or set of files for various device variants.

In case you are failed to flash ESP32 board from Arduino do the following:
* press BOOT button
* short press RST button
* release BOOT button
* proceed with flashing in Arduino

## Host API
The host API implementation for python may be found in **python/ble_multi_adapter.py**. Currently only hardware serial link is supported. Note that USB CDC link typically receives some debug information during ESP32 boot so its useful mostly for debugging (unless the ESP32 chip has another USB adapter as S3 for example).

## Testing
### The hard way
There are pair of test scripts **multi_echo.py** and **multi_echo_long.py** in **python** folder sending packets to other side that is expected to echo them back. One may use PEER_ECHO compilation option to echo data right on the device or use **central_echo.py** script for that purpose.

### The quick way
If you have only one ESP32 module and want to test **ble_uart_mx** adapter do the following:
* enable using built-in USB CDC by commenting out HW_UART define in mx_config.h
* build and flash adapter code
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

## Other experimental projects
A bunch of experimental projects created mostly for testing during the work on this project are located in **simple** folder.

## Useful links

The BLE terminal web page example: https://github.com/enspectr/ble-term

Dual mode Bluetooth to serial adapter based on ESP32: https://github.com/olegv142/esp32-bt-serial

nRF Connect for Mobile - tool for exploring BLE devices: https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp&pcampaignid=web_share
