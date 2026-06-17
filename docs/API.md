# HTTP API

The API is intentionally small and local-network oriented. It is served by the clock on port 80.

When connected to WiFi:

```text
http://clock.local
```

During setup mode:

```text
http://192.168.4.1
```

## Brightness

Read brightness:

```http
GET /api/brightness
```

Response:

```json
{"brightness":5}
```

Set brightness:

```http
POST /api/brightness?value=5
```

Accepted values are `1` through `8`.

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

## Display Test

Turn on all display segments for five seconds:

```http
POST /api/display-test
```

Many TM1637 clock modules wire the colon but not the individual decimal points, even when the LED package visibly contains them.
