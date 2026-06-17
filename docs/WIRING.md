# Wiring

The firmware uses two GPIO pins for the TM1637 data link.

| TM1637 module | ESP32-C6/C3 Super Mini |
| --- | --- |
| `CLK` | GPIO6 |
| `DIO` | GPIO7 |
| `GND` | GND |
| `5V` | 5V / VBUS |

Pins labeled `6` and `7` on the commonly sold ESP32-C6 and ESP32-C3 Super Mini boards are GPIO6 and GPIO7. Check your board's silkscreen and pinout before soldering, because clone boards can vary.

The pins can be changed at build time with:

```ini
-DWIFI_CLOCK_DISPLAY_CLK=6
-DWIFI_CLOCK_DISPLAY_DIO=7
```

## 5V Display Modules

Many TM1637 modules are sold as 5V modules. Power the display from the board's USB 5V/VBUS pin, not from a 3.3V pin.

The TM1637 signal lines usually work directly from ESP32 3.3V GPIO, but module designs vary. If a module behaves unreliably, use a logic-level shifter or a TM1637 module known to work with 3.3V logic.

## Decimal Points And PM Indicator

The firmware uses the rightmost decimal point for the optional PM indicator.

Some 4-digit clock-style TM1637 boards use LED packages that physically contain decimal points but do not route those decimal point pins to the TM1637 driver. On those modules:

- The colon can work.
- The display can show time normally.
- The full-segment test may not light decimal points.
- The PM indicator will not be visible.

That is a display module hardware limitation, not a firmware setting.
