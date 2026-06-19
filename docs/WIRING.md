# Wiring

The firmware uses two GPIO pins for the TM1637 data link.

| TM1637 module | ESP32-C6/C3 Super Mini |
| --- | --- |
| `CLK` | GPIO6 |
| `DIO` | GPIO7 |
| `GND` | GND |
| `5V` / `VCC` | 3V3 |

Pins labeled `6` and `7` on the commonly sold ESP32-C6 and ESP32-C3 Super Mini boards are GPIO6 and GPIO7. Check your board's silkscreen and pinout before soldering, because clone boards can vary.

The pins can be changed at build time with:

```ini
-DWIFI_CLOCK_DISPLAY_CLK=6
-DWIFI_CLOCK_DISPLAY_DIO=7
```

## Display Power

Many TM1637 modules are sold or silkscreened as 5V modules, but this project recommends connecting the module's `5V`/`VCC` pin to the ESP32 board's `3V3` pin.

In testing, the module worked reliably at 3.3V and the display became dimmer across the same firmware brightness range. Full brightness at 3.3V is still bright enough for this clock, while brightness level 1 is more usable in a dark room.

Using 3.3V also keeps the TM1637 logic level aligned with the ESP32 GPIO pins. If your specific module does not start reliably at 3.3V, use a different TM1637 module known to work at 3.3V or power it from 5V with appropriate level shifting.

## Decimal Points And PM Indicator

The firmware uses the rightmost decimal point for the optional PM indicator.

Some 4-digit clock-style TM1637 boards use LED packages that physically contain decimal points but do not route those decimal point pins to the TM1637 driver. On those modules:

- The colon can work.
- The display can show time normally.
- The full-segment test may not light decimal points.
- The PM indicator will not be visible.

That is a display module hardware limitation, not a firmware setting.
