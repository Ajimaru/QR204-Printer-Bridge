# QR204 Printer Bridge User Guide

Day-to-day operation: printing from the [`airquality` sensor box](https://github.com/Ajimaru/Sensor-Box-Enhanced),
from Home Assistant or from any machine on the LAN, what the status LED means, what can be tuned and what to do when
something goes wrong. Flashing and first tests are in the [Setup Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Setup-Guide).

## Printing from the airquality sensor box

- **Short press** of the top button: next page. The page now changes when the button is **released**, not when
  it is pressed, so holding the button to print doesn't switch the page first.
- **Hold for 5 s**: short beep, then the current page prints. The print starts while the button is still held.
- **Result on the screen**: when the job is handed to the bridge, a green "Copy sent" box shows in the middle of
  the screen for 3 s; if it couldn't be sent (e.g. bridge not reachable), a red "Send failed" box. The box is
  only on the screen, not on the printout. "Copy sent" means the C3 has read the whole job (`airquality` waits for
  that, ~8 s for 1.5x); paper problems show in the bridge's own "Printer Status".
- **Two short beeps**: the print failed, or the previous print was still being sent. "Printer Status" on `airquality`
  says why (e.g. "Error: printer bridge not reachable").
- **Web UI / HA:** the "Print Screen" button does the same as holding the top button.

Each print has a bold header line with the device name, the date and time, then the screen. Dark UI colors are
inverted: the black background becomes paper, all text, lines and graphs print black. `airquality` hands the job to
the C3 within a second or two and carries on normally; the printer then needs 13–25 s at 9600 baud.

Print settings live in the sensor box's own web UI (section "Printer", stored on that device, no reflash).
They are listed here because they decide what the printer receives; the bridge itself has no say in them:

| Entity | Meaning |
| --- | --- |
| Printer Host | Address of the bridge (default `thermal-printer.local`) |
| Print Size | 1x: 240 × 320 dots, 30 × 40 mm, ~13 s. 1.5x (default): 360 × 480 dots, 45 × 60 mm, ~24 s. Plus ~3 s with the header |
| Print Header | Box name and date above the image (default on) |
| Print Ink Threshold | A pixel prints black when its brightest color channel reaches this value (16–240, default 64). Lower keeps more of the fonts' anti-aliased edges (bolder text), higher gives thinner text |
| Print Feed Lines | Paper feed after the image (plus one line feed), so its end clears the tear bar (default 5) |

## Printing from elsewhere on the LAN

Any host can print by opening TCP port 9100 and sending ESC/POS bytes:

```sh
printf '\x1b@Hello QR204\n\n\n\n' | nc -w 2 thermal-printer.local 9100
```

Bytes ≥ 0x80 sent this way print as Chinese characters — the printer's firmware is always in GB18030 mode.
Stick to ASCII, or use the `print_text` action, which converts text first.

From Home Assistant:

```yaml
actions:
  - action: esphome.thermal_printer_print_text
    data:
      message: "CO2 {{ states('sensor.airquality_scd40_co2') }} ppm - please ventilate"
```

## Status LED

The optional WS2812B on the C3 shows the bridge's state, highest priority first:

| LED | Meaning |
| --- | --- |
| Blue, blinking fast (2 Hz) | Print job: data from a TCP client, `print_text` or the test page is being received or printed. Stays on until the UART has sent the last byte, plus 2 s (a screen print: ~25 s) |
| Red, blinking (1 Hz) | No WiFi connection (also right after power-on, until WiFi is up) |
| Yellow | WiFi up, but the printer is not ready: no answer to the status query (not connected, TX not wired, powered off), paper out or lid open, error (print head too hot, supply voltage) |
| Green | WiFi up and the printer reports "Ready" |
| Off | "Status LED" switched off in the web UI, or "LED Night Off" is on and it is night (see below) |

"LED Test" in the web UI shows red, green, blue, yellow and white for 1 s each, then the LED returns to the
status. The C3 log (DEBUG) has a `status_led` line for every change of state.

**Night mode.** With "LED Night Off" on, the LED goes dark between sunset and sunrise (sun more than 0.833°
below the horizon, as in the usual sunrise/sunset times). A print job still blinks blue. The C3 computes the sun
position itself (ESPHome `sun`, time from SNTP), so this works without Home Assistant and without any sensor box.
It needs the location: enter "Latitude" and "Longitude" in the web UI (decimal degrees, north/east positive; two
decimals are plenty). To look them up, open [OpenStreetMap](https://www.openstreetmap.org), search the address,
right-click the spot and choose "Show address": the sidebar shows "latitude, longitude". "Location Help" in the
web UI repeats this. "Next Sunrise" and "Next Sunset" show when the LED switches. While both numbers are 0 (not
set) or SNTP has no time yet, the LED shows the status as usual.

## Tuning

- **What counts as ink, size, header, feed:** the "Print ..." entities in the sensor box's own web UI (see
  "Printing from the airquality sensor box"); the bridge's own feed is "Feed Lines" in its web UI.
- **Darkness and peak current:** not adjustable. Neither `ESC 7` nor `DC2 #` changes the darkness on this
  firmware (see the [measured firmware behaviour](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Development-Guide#the-unit-used-here-self-test-page)), and per the manual the printer adjusts heating time and interval to the supply voltage by
  itself. The self-test page shows its own "print depth" setting (level 38 on this unit).
- **Width:** 384 dots is the printer's maximum; 1.5× (360 dots) is the largest scale that fits.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| "Print Test Page" prints nothing | TX/RX swapped, missing GND, wrong level (RS232 variant, 5 V printer input), printer not powered |
| Garbage characters | `printer_baud` doesn't match the self-test page |
| Chinese characters, or a line break missing after them | Raw bytes >= 0x80 sent to the printer (e.g. Latin-1 or UTF-8 from `nc`); the printer is always in GB18030 mode. A sending client, `print_text` and the test page all convert text first (see the [measured firmware behaviour](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Development-Guide#the-unit-used-here-self-test-page)) |
| A stray letter (e.g. "P") at the start of a print | A job sent `ESC 7 n1 n2 n3`; this firmware takes one parameter and prints the rest |
| Prints, but "Printer Status" says "No answer" | Printer TX not wired to the C3 (fine if you don't need the status) or wrong level on that line |
| Part of a screen print missing (e.g. half a word), the rest of the page fine, although the bridge got the whole job ("Printed Bytes" matches) | Bytes corrupted on the serial line: loose or missing GND between the C3 and the printer's serial pin 5 (seen after rewiring) |
| Printer resets or stops in the middle of an image | Power supply or converter too weak (12 V / 3 A PSU, MP1584EN for the printer), thin or long power wires, capacitor missing |
| "Printer Status" says "Paper out" with paper in | Lid not fully closed (the unit has no lid sensor, the paper sensor sees the open lid), or the roll is in the wrong way round: the thermal side must face the print head |
| "Printer Status" says "Error: print head too hot or supply voltage" | Head overheated by long dark prints (let it cool, print less black) or the supply outside 5–9 V: check the MP1584EN output under load, 8.7–9.0 V |
| C3 restarts when printing starts | C3 and printer share an overloaded supply; check the 12 V PSU and the printer converter under load |
| C3 restarts a few seconds into a screen print, "Reset Reason" says "task watchdog" | `printer_bridge.h` from an older release: a job larger than the 16 KB TX buffer makes ESP-IDF 5.5's `uart_write_bytes()` busy-wait and starve the ESPHome loop. The current bridge only writes what fits and sleeps otherwise |
| Faint print | Low supply voltage, old or cheap paper, paper in the wrong way round |
| Small grey text on a screen print has gaps (e.g. "PM1" as "PM'") | Its anti-aliased strokes fall below "Print Ink Threshold"; lower it (64 works well on this unit, the first default was 96) |
| `airquality` says "Error: printer bridge not reachable" | C3 offline, or `airquality`'s "Printer Host" (web UI, section "Printer") doesn't point to the C3: try its current IP instead of `thermal-printer.local` |
| `airquality` says "Error: cannot resolve …" | mDNS name not found (C3 offline or name typo); enter the C3's IP in "Printer Host" |
| `airquality` says "Error: send failed …" | WiFi dropped during the transfer; print again |
| `airquality` says "Error: bridge dropped the job" or "… did not confirm the job" | The bridge closed the connection early or didn't finish reading within 60 s: C3 restarted or WiFi dropped; check the bridge's log and "Reset Reason" |
| The feed at the end is missing, the printer stops at the same place every time, the next print starts with garbage | The end of the job didn't reach the printer, so it still waits for the rest of the image. With an older 16 KB TX buffer the last ~0.6 KB of a 1.5x screen print got lost whenever the C3 had to throttle the sender; the bridge now buffers a whole screen (64 KB). The C3 log shows the bytes per job ("Job N: 22774 bytes" for a complete 1.5x print with header). Send a few hundred NUL bytes (e.g. `head -c 1200 /dev/zero \| nc -w 2 thermal-printer.local 9100`) or power-cycle the printer to clear it |
| Holding the button changes the page anyway | Released before 5 s; the page changes on every release shorter than 1 s |
| Status LED stays dark | DIN and DOUT swapped, "Status LED" switched off, no 3.3 V at the LED |
| Status LED shows the wrong colors (e.g. red instead of green) | LED with a different channel order: change `channel_colors: GRB` (e.g. to `RGB`) in `thermal-printer.yaml` |
| Status LED flickers or shows random colors | Long or unshielded data wire; add the 330 Ω series resistor, keep the wire short, and a 100 nF capacitor across the LED's VDD/GND if the breakout board has none |
| Status LED yellow although the printer prints | The printer's TX line is not wired to the C3, so the status query gets no answer |
| `airquality` log: "display took a long time" (~400–500 ms) right after a print request | Expected: that one update draws the page twice (screen + printer canvas); a normal update takes ~190–240 ms |

---

For the measured firmware quirks, the ESC/POS job layout and the bridge internals, continue with the
[Development Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Development-Guide).
