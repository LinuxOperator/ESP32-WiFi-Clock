# WiFi Clock

ESP32-C6 and ESP32-C3 firmware for a simple, reliable WiFi clock using a 4-digit TM1637 display.

The clock syncs time over WiFi, handles daylight saving time through timezone rules, serves a local web UI at `http://clock.local`, supports browser firmware updates, and can expose controls to Home Assistant over MQTT.

## Features

- 4-digit TM1637 clock display with optional blinking colon.
- 12-hour mode by default, optional 24-hour mode.
- Optional PM indicator using the rightmost decimal point when the display module wires that segment.
- NTP time sync with automatic daylight saving time changes.
- Default timezone: Denver / Mountain Time.
- Captive WiFi setup portal when WiFi is not configured.
- Five-minute boot grace period before setup mode if saved WiFi is temporarily unavailable.
- Persistent settings in ESP32 NVS across power cycles and OTA updates.
- Web UI for WiFi, display settings, MQTT, display test, reset, and OTA firmware upload.
- Basic HTTP API for brightness and colon blink.
- Optional MQTT discovery for Home Assistant.

## Hardware

- ESP32-C6 Super Mini or ESP32-C3 Super Mini.
- 4-digit TM1637 display module with `CLK`, `DIO`, `GND`, and `5V`.
- USB-C power through the ESP32 board.

Pins used by the display:

| TM1637 module | ESP32-C6 Super Mini |
| --- | --- |
| `CLK` | GPIO6 |
| `DIO` | GPIO7 |
| `GND` | GND |
| `5V` | 5V / VBUS |

See [docs/WIRING.md](docs/WIRING.md) for notes about 5V TM1637 modules and decimal points.

## First Boot

If WiFi is not configured, the clock creates an access point named `Clock Setup`.

Join that network from a phone or computer. iOS should open the captive portal automatically. If it does not, open `http://192.168.4.1`.

Once the clock joins your WiFi, browse to:

```text
http://clock.local
```

## Flash A Prebuilt Binary

Install esptool if you do not already have it:

```powershell
python -m pip install esptool
```

For first-time flashing, use the factory binary that matches your board.

ESP32-C6 Super Mini:

```powershell
python -m esptool --chip esp32c6 --port COM10 --baud 460800 write_flash 0x0 releases\v1.0\wifi-clock-v1.0-esp32c6-factory.bin
```

ESP32-C3 Super Mini:

```powershell
python -m esptool --chip esp32c3 --port COM10 --baud 460800 write_flash 0x0 releases\v1.0\wifi-clock-v1.0-esp32c3-factory.bin
```

If the board does not enter flashing mode, hold the `BOOT` button while plugging it in or while tapping `RESET`, then release `BOOT` once flashing starts.

See [docs/FLASHING.md](docs/FLASHING.md) for easier script-based flashing, OTA updates, and optional build-time WiFi provisioning.

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

## Web UI

The web UI lets you configure:

- WiFi network and password.
- Timezone, including browser-assisted auto-detect.
- Brightness from `1` through `8`.
- Blinking or solid colon.
- 12-hour or 24-hour display.
- Optional PM indicator.
- MQTT/Home Assistant settings.
- OTA firmware upload.
- Full display test.
- Reset and reboot.

## API

Briefly:

```http
GET  /api/brightness
POST /api/brightness?value=1..8
GET  /api/colon
POST /api/colon?value=on
GET  /api/settings
```

See [docs/API.md](docs/API.md) for details.

## Home Assistant

Enable MQTT in the web UI, enter your broker host, username, and password, then press `Save and test MQTT`.

When connected, Home Assistant MQTT discovery creates entities for:

- Brightness
- Blink Colon
- 24-hour Mode
- PM Indicator

See [docs/MQTT_HOME_ASSISTANT.md](docs/MQTT_HOME_ASSISTANT.md).

## Reliability Notes

The clock keeps behaving as a clock through common network failures:

- If saved WiFi is down at boot, it retries every 10 seconds.
- If saved WiFi is still unavailable after five minutes, setup mode starts while station retries continue.
- If WiFi returns later, the clock connects, starts mDNS, syncs NTP, and shuts down setup mode.
- If WiFi drops after a successful connection, the setup AP stays off and the clock keeps showing local RTC time until NTP is available again.
- If time has not synced yet, the display shows `----` instead of a misleading time.

## License

MIT. See [LICENSE](LICENSE).
