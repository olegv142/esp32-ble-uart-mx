# esp32-ble
The collection of useful ESP32 BLE projects for Arduino

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
