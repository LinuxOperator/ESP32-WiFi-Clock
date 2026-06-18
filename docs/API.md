# HTTP API

The API is intentionally small and local-network oriented. It is served by the clock on port 80.


## Brightness

Read brightness:

```http
GET /api/brightness
```

Response:

```json
{"brightness":5,"displayOn":true}
```

Set brightness:

```http
POST /api/brightness?value=5
```

Accepted values are `1` through `8`.

Brightness is retained when the display is turned off.

## Display Power

Read whether the LED display output is enabled:

```http
GET /api/display
```

Response:

```json
{"displayOn":true}
```

Turn the display off:

```http
POST /api/display?value=off
```

Turn it back on:

```http
POST /api/display?value=on
```

Turning the display off does not change the saved brightness level.

## Blink Colon

Read colon blink:

```http
GET /api/colon
```

Response:

```json
{"colonBlink":true}
```

Set colon blink:

```http
POST /api/colon?value=on
```

Accepted enabled values: `on`, `1`, `true`.

Accepted disabled values: `off`, `0`, `false`.

## Full Settings

Read the current configuration:

```http
GET /api/settings
```

This endpoint includes saved WiFi details because the web UI intentionally shows them. Do not expose the clock directly to the public internet.

It also includes device details such as firmware version, chip type, mDNS name, WiFi signal strength, IP address, MAC address, and uptime.

The `wifiVersion` field reports the active WiFi generation when connected.

## Display Test

Turn on all display segments for five seconds:

```http
POST /api/display-test
```

Many TM1637 clock modules wire the colon but not the individual decimal points, even when the LED package visibly contains them.

## Reboot

Reboot without clearing settings:

```http
POST /api/reboot
```
