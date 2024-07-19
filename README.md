# esp32-ble
The collection of useful ESP32 BLE projects for Arduino targeting telemetry / monitoring applications.

## Simple receiver / transmitter
The ble_receiver / ble_transmitter are examples of creating one way communication channel with automatic reconnection. The code also illustrates using watchdog for improved reliability and maximizing transmission power for extending range.

## Building and flashing
To be able to build this code examples add the following to Arduino Additional board manager URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

In case you are failed to flash ESP32 board from Arduino do the following:
* press BOOT button
* short press RST button
* release BOOT button
* proceed with flashing in Arduino

## Power consumption
The following values were measured with SDK v.3.0.3
| Mode     | Supply current |
|----------|----------------|
| Transmit | 46 mA          |
| Receive  | 60 mA          |
| Scan     | 95 mA          |

The power consumption was significantlty improved since SDK v.2. Yet its still not quite suitable for battery powered applications.

## Range testing results
The maximum distance over which we can safely transmit data is an important issue in many applications. Typically small and cheap ESP32 modules have small chip antenna soldered on board. With such modules one can expect the opearating distance around 10 meters. The efficiency of such chip antenna is close to nothing. Even printed circuit antenna is better and gives you more range. The external antenna is much better choice. One can solder it just above the chip antenna as shown on the figure below.

![The ESP32 C3 Super mini module with extenal antenna](https://github.com/olegv142/esp32-ble/blob/main/doc/c3_supermini_with_antenna.jpg)

One can further increase operating range by setting maximum transmission power programmatically. I have tested two modules with external antennas like shown on the figure above with elevated TX power. This combination demonstrated quite impressive results. Inside the building I've got stable transmission between basement and second floor through two layers of reinforced concrete slabs. Outdoors, it showed stable transmission at a distance of 100m with line of sight.
