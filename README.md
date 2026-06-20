# WiFi Clock
![Clock Photos](https://i.imgur.com/lH6tMip.jpeg)

ESP32-C6 and ESP32-C3 firmware for a simple, reliable WiFi clock using a 4-digit TM1637 display.

The clock syncs time over WiFi, handles daylight saving time through timezone rules, serves a local web UI at `http://clock.local`, supports browser firmware updates, and can expose controls to Home Assistant over MQTT.

## Features

- Adjustable brightness and display settings
- Auto-detect timezone with automatic NTP time sync
- Configurable mDNS and network hostname
- Captive WiFi setup portal
- Web UI and a basic API
- MQTT support for use with Home Assistant
- OTA firmware upload

![Web UI](https://i.imgur.com/447F4Nl.png)


## Hardware

- ESP32-C6 or C3 Super Mini.
- 4-digit TM1637 display module (with pins `CLK`, `DIO`, `GND`, and `5V`/`VCC`).
- 3D Printed case

Pins used by the display:

| TM1637 module | ESP32-C6/C3 Super Mini |
| --- | --- |
| `CLK` | GPIO6 |
| `DIO` | GPIO7 |
| `GND` | GND |
| `5V` / `VCC` | 3V3 |

Even if the display PCB labels the power pin as `5V`, this project recommends powering the TM1637 module from `3V3`. It is still bright, gives better low-brightness range, and keeps the display logic level aligned with the ESP32 GPIO. See [docs/WIRING.md](docs/WIRING.md) for details.

## First Boot

### Configure WiFi:
- If WiFi is not configured, the clock creates a wifi access point named `Clock Setup`.
- Join that network from a phone or computer (it should open the captive portal automatically. If it does not, open `http://192.168.4.1`).

Once the clock joins your WiFi, you can access the UI by browsing to:

```text
http://clock.local
```

## Flash Prebuilt Binary

### Install esptool if you do not already have it:

```powershell
python -m pip install esptool
```

### Find The Serial Port:

```powershell
pio device list
```

### Flash over USB:

ESP32-C6:

```powershell
python -m esptool --chip esp32c6 --port COM10 --baud 460800 write_flash 0x0 wifi-clock-v1.0-esp32c6-factory.bin
```

ESP32-C3:

```powershell
python -m esptool --chip esp32c3 --port COM10 --baud 460800 write_flash 0x0 wifi-clock-v1.0-esp32c3-factory.bin
```

If the board does not enter flashing mode, hold the `BOOT` button while plugging it in.

See [docs/FLASHING.md](docs/FLASHING.md) for more information on flashing, OTA updates, and optional build-time WiFi provisioning.

## Home Assistant

Enable MQTT in the web UI, enter your broker host, username, and password, then press `Save and test MQTT`.

When connected, Home Assistant MQTT discovery creates entities for:

- Brightness
- Display
- Blink Colon
- 24-hour Mode
- PM Indicator

![Home Assistant](https://i.imgur.com/0d7r4NQ.png)


## API

Briefly:

```http
GET  /api/brightness
POST /api/brightness?value=1..8
GET  /api/display
POST /api/display?value=on
GET  /api/colon
POST /api/colon?value=on
GET  /api/settings
```

See [docs/API.md](docs/API.md) for details.

## Releases

Download prebuilt factory and OTA binaries from [GitHub Releases](https://github.com/LinuxOperator/ESP32-WiFi-Clock/releases). Use factory binaries only for USB flashing. Use OTA binaries from the web UI, and choose the file for the matching ESP32-C3 or ESP32-C6 board; the web UI rejects factory, generic, and wrong-chip filenames.

## Build From Source

Install PlatformIO, then build the default ESP32-C6 target:

```powershell
pio run
pio run -t upload
```

Build a specific target:

```powershell
pio run -e esp32-c6-supermini
pio run -e esp32-c3-supermini
```

The firmware targets 4 MB ESP32-C6 and ESP32-C3 Super Mini boards with a two-slot OTA partition layout from [partitions.csv](partitions.csv).

## Reliability Notes

The clock keeps behaving as a clock through common network failures:

- If saved WiFi is down at boot, it retries every 10 seconds.
- If saved WiFi is still unavailable after five minutes, setup mode starts while station retries continue.
- If WiFi returns later, the clock connects, starts mDNS, syncs NTP, and shuts down setup mode.
- If WiFi drops after a successful connection, the setup AP stays off and the clock keeps showing local RTC time until NTP is available again.
- If time has not synced yet, the display shows `----` instead of a misleading time.

## License

MIT. See [LICENSE](LICENSE).
