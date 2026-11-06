# QR204 Printer Bridge Features

An ESP32-C3 turns a QR204 58 mm thermal printer into a network printer: raw ESC/POS over TCP port 9100, plus printer
status, a status LED and Home Assistant integration through ESPHome. Any host on the LAN can send a job — a Home
Assistant action, `nc` from a shell, or the [`airquality` sensor box](https://github.com/Ajimaru/Sensor-Box-Enhanced),
a separate project, which prints its current screen on a long button press.

Port 9100 has no authentication, by protocol design: reaching the device is the only requirement for printing. Keep it
on a trusted network — see the [security policy](https://github.com/Ajimaru/QR204-Printer-Bridge/blob/main/SECURITY.md).

```text
Sensor box (ESP32-S2)  --WiFi, TCP port 9100, ESC/POS-->  ESP32-C3 bridge  --UART (TTL)-->  QR204
airquality                                                thermal-printer
```

See the [Hardware & Printing Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Hardware-and-Printing-Guide)
for the parts list, or jump straight to the [Assembly Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Assembly-Guide) if the hardware is on the bench.

## Why this design

This bridge exists because of how one particular client had to reach the printer. That client, the
`airquality` sensor box, is a separate project; the constraints below are its, not the bridge's:

- It runs on an **ESP32-S2, which has no Bluetooth radio**, so it can reach the printer only via WiFi or a
  cable.
- A direct cable doesn't work well either: the S2 has two hardware UARTs, both taken (ZE08, PMS).
- The ESP32-C3 only does **Bluetooth LE**, not Classic Bluetooth (SPP). The QR204 manual (in `datasheets/`) and the shop
  listings only mention TTL/RS232 and USB, no Bluetooth. Wiring the C3 to the printer's serial port works
  regardless of the printer's Bluetooth variant.
- The C3 speaks raw TCP on port 9100, the usual "network receipt printer" protocol. Anything on the LAN can
  print to it, not only that one device. Home Assistant can also print text through an ESPHome action.

## Bridge features

- Raw TCP port 9100 (JetDirect / AppSocket), the usual network receipt-printer protocol: anything on the LAN
  can print, not just one dedicated client.
- Jobs pass through unchanged; the bridge is a pipe, not a filter.
- 64 KB UART TX ring buffer, so a whole 1.5x screen print (≈23 KB) fits and the sender is never throttled
  mid-job.
- Printer status polling (`DLE EOT 2`) every 10 s while idle, exposed as Home Assistant entities.
- Text printing from Home Assistant through the `print_text` action, with automatic character conversion for
  the printer's GB18030-only firmware.
- Test page and `GS I` printer-info readout as buttons.
- Optional WS2812B status LED with night mode from the device's own sun calculation (no Home Assistant needed).
- Web UI with digest authentication, OTA updates, DHCP plus mDNS (`thermal-printer.local`).

## Entities of the bridge (thermal-printer)

| Entity | Meaning |
| --- | --- |
| Printer Online | The printer answered the status query (DLE EOT 2) in the last ~30 s |
| Printer Paper Out | Status bit "stopped by paper end"; also on while the lid is open |
| Printer Status | Ready / Paper out / Cover open / Error: print head too hot or supply voltage / Error: unrecoverable, power-cycle the printer / Error / No answer |
| Print Client Connected | A TCP client is currently sending a job |
| Print Jobs, Printed Bytes | Counters since the last C3 boot |
| Read Printer Info | Button: asks the printer (`GS I`, `DLE EOT`) and writes the answers to the log (web UI log panel, tag `printer_info`): firmware, manufacturer, model, serial number, model/type/ROM IDs, status bytes, plus the bridge's baud rate, TCP port and job counters. Settings such as baud rate, code page or print depth can't be read back; they are only on the self-test page |
| Print Test Page | Button: short test page from the bridge (status, baud rate, character replacements, 32-character ruler, full-width black bar). The manual's self-test command `DC2 T` is ignored by this firmware |
| Status LED | Config switch: status LED on/off (e.g. at night), restored after a reboot |
| LED Brightness | Config: 1–100 %, default 30 |
| LED Night Off | Config switch: status LED off between sunset and sunrise, except during a print (see "Status LED"); on by default |
| Latitude, Longitude | Config: location for sunrise/sunset, decimal degrees (north/east positive), 0 / 0 = not set |
| Next Sunrise, Next Sunset | When the LED switches next (day and time), "Location not set" while 0 / 0 |
| Location Help | Where to look up the coordinates (OpenStreetMap) |
| LED Test | Button: color test (see "Status LED") |
| Feed Lines | Config: paper feed after `print_text` and the test page, 0–20, default 5 (a sending client may set its own feed instead, as `airquality`'s "Print Feed Lines" does) |
| Action `print_text` | Prints UTF-8 text; ä, ö, ü, ß print as ae, oe, ue, ss, `°` and `µ` as is, other non-ASCII characters as `?` (see [measured firmware behaviour](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Development-Guide#the-unit-used-here-self-test-page)) |

The status is only polled while no job is being sent, and only when the printer's TX line is connected.
`print_text` and "Print Test Page" are skipped (log warning) while a TCP job is running.

Example automation: print a note when CO₂ gets high.

```yaml
actions:
  - action: esphome.thermal_printer_print_text
    data:
      message: "CO2 {{ states('sensor.airquality_scd40_co2') }} ppm - please ventilate"
```

(Replace the entity ID with the real CO₂ entity.)

## Status LED

An optional WS2812B on GPIO10 shows the bridge's state at a glance: blue blinking during a print job, red
blinking without WiFi, yellow when the printer is not ready, green when it is. It can be switched off, dimmed,
and set to stay dark at night from the device's own sun calculation. Colours, night mode and the location
settings are in the [User Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/User-Guide#status-led).

Continue with the [Hardware & Printing Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Hardware-and-Printing-Guide) to gather the parts.