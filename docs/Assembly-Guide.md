# QR204 Printer Bridge Assembly Guide

Identify the printer variant first, then wire the power supply. Parts list:
[Hardware & Printing Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Hardware-and-Printing-Guide).

> [!WARNING]
> Set both buck converters with a multimeter **before** connecting them to the printer or the ESP32-C3.
> Mini-360 modules often ship with their output close to the input voltage, and 12 V on the C3's `5V` pin
> destroys the board. Never wire an RS232-level printer port directly to the C3 — ±5…12 V destroys it.

## Step 1: identify the printer variant

Do this before connecting the C3.

1. **Power model.** Check the label or the power connector: 5–9 V model (power on pin 2) or 12 V model (power
   on pin 1). This guide assumes the 5–9 V model on 5 V.
2. **Self-test page.** Hold the FEED button (the one with the LED) while switching the printer on. It prints a
   test page. Note the **baud rate** and the interface it lists.
3. **Serial levels.** With the printer on and idle, measure the voltage between serial pin 2 (TX) and pin 5 (GND):

   | Reading | Port | What to do |
   | --- | --- | --- |
   | about +3.3 V | 3.3 V TTL | Connect TX directly to the C3 |
   | about +5 V | 5 V TTL | Put a divider between the printer's TX and the C3's RX: 10 kΩ in series, 20 kΩ from C3 RX to GND |
   | negative (−5 to −12 V) | RS232 | **Never connect directly.** Use a MAX3232 module between printer and C3 |

   The printer's RX line takes the C3's 3.3 V output directly in most cases. If a 5 V printer ignores the C3
   (nothing prints, the test page button does nothing), add a level shifter on that line too.

The unit this project was built with reads: firmware `QR701_T1US-N32-2008`, interface **USB & TTL**, **9600 baud**,
code page PC936 (GB18030). Its full self-test readout and every measured deviation from the manual are in the
Development Guide, [The unit used here](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Development-Guide#the-unit-used-here-self-test-page-2026-09-28).

The TX level is not on the self-test page. Measured on this unit (2026-09-29, printer on 9 V, idle): TX and RX
both at 3.3 V, i.e. 3.3 V TTL. It connects directly to the C3, no divider, no MAX3232.

## Step 2: power supply and wiring

One 12 V / 3 A supply feeds two buck converters: an MP1584EN for the printer (9 V) and a Mini-360 for the C3 (5 V).

```text
12 V / 3 A PSU ──┬── MP1584EN, 8.7–9.0 V ── 470–1000 µF / 25 V ──  printer power pin 2 (VH), pin 3 GND
                 │
                 └── Mini-360, 5.0 V ────── 100–220 µF / 16 V ───  C3 5V / GND
```

**Set both converters with a multimeter before connecting anything to their outputs.** Mini-360 modules often
ship with their output close to the input voltage; 12 V on the C3's 5V pin destroys the board. Then fix the
trim pots with a drop of lacquer or glue.

- **Printer: 8.7–9.0 V, not higher.** The manual gives 9.5 V as the limit, and trim pots drift. Check the
  voltage again under load, while the self-test page or the bridge's test page (black bar) prints.
- **C3: 5.0 V.** The Super Mini's 3.3 V regulator is not rated for more than about 6 V.
- The converters are not isolated: IN−, OUT− and the PSU's negative lead are one ground, so printer and C3
  share it automatically. Still connect serial pin 5 to the C3's GND.
- Printer power wires short and at least AWG 22 (2 A+ peaks).
- Capacitors: both converter modules already carry small ceramic capacitors. Add the electrolytics **at the
  loads**, not at the converters: 470–1000 µF right at the printer's power connector (required; 25 V, so it
  also survives a converter that fails through to 12 V) and 100–220 µF right at the C3's 5V/GND pins
  (recommended). Mind the polarity. A 470 µF / 25 V at the converters' 12 V input only helps with a long cable
  from the PSU.
- On many Super Mini boards the 5V pin is wired straight to USB VBUS. Don't plug in USB while the Mini-360
  powers the C3; update over the air instead.

| From | To | Notes |
| --- | --- | --- |
| PSU +12 V / GND | IN+ / IN− of both converters | |
| MP1584EN OUT+ (8.7–9.0 V) | printer power pin 2 (VH) | 5–9 V model; 470–1000 µF across pins 2 (+) and 3 (−) |
| MP1584EN OUT− | printer power pin 3 (GND) | |
| Mini-360 OUT+ (5.0 V) | C3 `5V` pin | 100–220 µF across 5V (+) and GND (−) |
| Mini-360 OUT− | C3 `GND` | |
| C3 GPIO7 (TX) | printer serial pin 3 (RX) | |
| printer serial pin 2 (TX) | C3 GPIO6 (RX) | Directly, via divider or MAX3232 depending on step 1 above (this unit: 3.3 V, direct). Optional: without it printing still works, but the status entities show "No answer" |
| printer serial pin 5 (GND) | C3 `GND` | |
| C3 `3V3` | WS2812B VDD (often labelled `5V` or `VCC`) | Optional status LED |
| C3 `GND` | WS2812B GND | |
| C3 GPIO10 | WS2812B DIN | Optional 330 Ω in series; the input side, not DOUT (follow the arrow on the breakout board) |

The WS2812B runs from 3.3 V here, below its datasheet minimum of 3.5 V; it works in practice, and the C3's
3.3 V data signal then meets the LED's input threshold without a level shifter. Blue and green are a bit dimmer
at 3.3 V than at 5 V. Don't move the LED to the 5 V rail without a level shifter: at 5 V it needs at least 3.5 V
on DIN. One LED draws at most ~60 mA (full white), well within the Super Mini's 3.3 V regulator.

GPIO20/21 (the C3's labelled RX/TX pins) are deliberately not used: the ROM bootloader prints its boot messages
on GPIO21, and the printer would print them as garbage on every C3 reset.

## Pin map (ESP32-C3 Super Mini)

| GPIO | Use |
| --- | --- |
| GPIO06 | UART1 RX ← printer TX (serial pin 2) |
| GPIO07 | UART1 TX → printer RX (serial pin 3) |
| GPIO10 | WS2812B status LED (DIN) |
| GPIO08 | On-board LED (strapping pin), unused |
| GPIO09 | BOOT button (strapping pin), unused |
| GPIO18 / GPIO19 | USB D− / D+ (flashing, logger) |
| GPIO20 | Free (UART0 RX), not used for the printer |
| GPIO21 | Keep free: the ROM bootloader prints its boot messages here |

The full table, including the flash-reserved pins, is at the end of
[`thermal-printer.yaml`](https://github.com/Ajimaru/QR204-Printer-Bridge/blob/main/esphome/thermal-printer.yaml).

Continue with the [Setup Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Setup-Guide) for firmware and
first tests.