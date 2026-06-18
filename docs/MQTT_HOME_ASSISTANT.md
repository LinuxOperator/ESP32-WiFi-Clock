# MQTT And Home Assistant

MQTT is disabled by default.

Enable it in the web UI, then enter:

- Host: IP address or hostname. Port `1883` is assumed unless you add `:port`.
- Username: optional.
- Password: optional.

Press `Save and test MQTT`. The result message includes the broker address and a connection error when the test fails.

## Discovery

When MQTT connects, the clock publishes Home Assistant discovery messages under:

```text
homeassistant/...
```

The state and command topics use:

```text
clock/<device-id>/...
```

## Entities

Home Assistant should create:

| Entity | Type | Direction |
| --- | --- | --- |
| Display | Switch | Read/write |
| Brightness | Number slider, `1` to `8` | Read/write |
| Blink Colon | Switch | Read/write |
| 24-hour Mode | Switch | Read/write |
| PM Indicator | Switch | Read/write |

Turning Display off blanks the TM1637 LEDs without changing the saved brightness level.

PM Indicator is automatically turned off when 24-hour mode is enabled.

## Reliability

The firmware retries MQTT every 10 seconds while WiFi is connected. It republishes retained state periodically and republishes discovery after reconnecting.

MQTT is not required for clock operation. If the broker is down, the display, web UI, WiFi reconnect logic, and NTP sync continue independently.
