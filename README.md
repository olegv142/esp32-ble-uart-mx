# esp32-ble
This is the multipurpose dual role BLE to serial bridge capable of creating multiple connections to other peripheral devices as well as acting as peripheral accepting connections from other central device. Its operation is controlled by host via the same serial link as used for data transfers. Multiple compile time configuration options are provided to meet requirements in a variety of applications. For example it may be used for gathering telemetry data from some set of devices, providing communication link for commands / responses from controlling application or for creating bidirectional wireless communication channel between the pair of devices. It uses Arduino as building platform to keep code small and makes using it as simple as possible.

## Architecture and communication protocol
![The bridge architecture and communication protocol](https://github.com/olegv142/esp32-ble/blob/main/doc/mx.png)

## Building and flashing
To be able to build this code examples add the following to Arduino Additional board manager URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Then go to Boards Manager and install **esp32 by Espressif Systems**.

The compilation options are placed onto the separate header **ble_uart_mx/mx_config.h**. With those options one can
* choose device name
* choose between USB CDC and hardware UART for communications
* configure hardware UART flow control
* disable status events if not needed
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
The host API implementation for python may be found in **python/ble_multi_adapter.py**.

## Testing
There are pair of test scripts **multi_echo.py** and **multi_echo_long.py** in **python** folder sending packets to other side that is expected to echo them back. One may use PEER_ECHO compilation option to echo data right on the device or use **central_echo.py** script for that purpose.

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

## Other experimental projects
A bunch of experimental projects created mostly for testing during the work on this project are located in **simple** folder.

## Useful links

The BLE terminal web page example: https://github.com/enspectr/ble-term

Dual mode Bluetooth to serial adapter based on ESP32: https://github.com/olegv142/esp32-bt-serial
