# Flashing

This project produces separate binaries for ESP32-C6 and ESP32-C3 boards. Use the files that match your board.

- `wifi-clock-v1.0-esp32c6-factory.bin`: first-time USB flashing for ESP32-C6.
- `wifi-clock-v1.0-esp32c6-ota.bin`: web UI updates for ESP32-C6.
- `wifi-clock-v1.0-esp32c3-factory.bin`: first-time USB flashing for ESP32-C3.
- `wifi-clock-v1.0-esp32c3-ota.bin`: web UI updates for ESP32-C3.

## Install Esptool

Install Python, then:

```powershell
python -m pip install esptool
```

## Find The Serial Port

On Windows, Device Manager usually shows the ESP32-C6 as a USB serial port such as `COM10`.

You can also ask PlatformIO:

```powershell
pio device list
```

## Flash The Factory Binary

ESP32-C6 Super Mini:

```powershell
python -m esptool --chip esp32c6 --port COM10 --baud 460800 write_flash 0x0 releases\v1.0\wifi-clock-v1.0-esp32c6-factory.bin
```

ESP32-C3 Super Mini:

```powershell
python -m esptool --chip esp32c3 --port COM10 --baud 460800 write_flash 0x0 releases\v1.0\wifi-clock-v1.0-esp32c3-factory.bin
```

Replace `COM10` with your port.

If the board is not detected or flashing does not start:

1. Hold the `BOOT` button.
2. Plug in USB-C or tap `RESET`.
3. Start the flash command.
4. Release `BOOT` once esptool connects.

Some ESP32-C6 Super Mini boards enter flashing mode automatically; others need the `BOOT` button.

## Easier Windows Script

The repository includes a helper:

```powershell
.\tools\flash.ps1 -Board c6 -Port COM10
```

For an ESP32-C3:

```powershell
.\tools\flash.ps1 -Board c3 -Port COM10
```

To use a different binary:

```powershell
.\tools\flash.ps1 -Port COM10 -Bin .\releases\v1.0\wifi-clock-v1.0-esp32c6-factory.bin
```

## OTA Updates

After first flash, open the web UI at `http://clock.local`, go to `Firmware`, choose the OTA binary, then press `Flash firmware`.

Use the OTA binary that matches your board:

```text
releases/v1.0/wifi-clock-v1.0-esp32c6-ota.bin
releases/v1.0/wifi-clock-v1.0-esp32c3-ota.bin
```

Settings are stored in NVS and are not erased by OTA updates.

## Optional WiFi Credentials At Build Time

The prebuilt factory binary uses the captive portal for WiFi setup.

If you build your own binary and want first boot to already know the WiFi network, add build flags:

```ini
build_flags =
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DWIFI_CLOCK_PROVISION_SSID=\"YourNetwork\"
  -DWIFI_CLOCK_PROVISION_PASSWORD=\"YourPassword\"
```

Then run:

```powershell
pio run
```

Or build a specific board:

```powershell
pio run -e esp32-c6-supermini
pio run -e esp32-c3-supermini
```

Those credentials are saved to NVS on first boot only if no WiFi credentials are already stored.
