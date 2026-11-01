# QR204 Printer Bridge Setup Guide

This guide covers the firmware, how a client finds the bridge, and the end-to-end test. Start with the [Assembly Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Assembly-Guide) if the hardware is not wired yet.

## Step 1: firmware

1. Secrets: as shipped, the bridge reuses the `api_encryption_key`, `wifi_ssid`, `wifi_password`,
   `web_server_username` and `web_server_password` from `secrets.yaml` on the HA host, so nothing needs to be
   added. For a key of its own, add one to
   `secrets.yaml` (`openssl rand -base64 32`) and point `api: encryption: key:` at it.
2. Check the settings in `thermal-printer.yaml` under `substitutions:`:
   - `printer_baud`: the rate from the self-test page (default 9600).
   - `printer_tx_pin` / `printer_rx_pin`: 7 / 6, as wired above.
3. Copy `thermal-printer.yaml` and `printer_bridge.h` to `/homeassistant/esphome/`.
4. First flash over USB: ESPHome Device Builder → the new "thermal-printer" card → Install → Plug into this
   computer. Later updates go over the air.
5. Adopt the device in Home Assistant (Settings → Devices & services → ESPHome), using the API key from item 1 above
   (or a key of its own, from item 1).

To print screens from the [`airquality` sensor box](https://github.com/Ajimaru/Sensor-Box-Enhanced), that device needs
its own client include and a reflash. Both belong to its repository, not to this one.

### How a client finds the bridge

The C3 gets its IP address by DHCP and announces itself via mDNS as `thermal-printer.local`, so a client can
address it by name and a new DHCP address needs no change. `airquality` keeps that address in its own
"Printer Host" entity (its web UI, section "Printer"; IP or host name, default `thermal-printer.local`) and
resolves it on every print. If name resolution ever fails, enter the C3's current IP there instead (router
device list or the C3's web UI); no reflash needed.

### Web UI of the bridge

<http://thermal-printer.local/>, with the `web_server` credentials from item 1. Section "Printer" at the top: status,
online, paper out, test page button, job counters and the status LED controls. Below it the same diagnostics as on the
usual ESPHome diagnostics (device info, reset reason, heap, loop time, uptime, WiFi strength, CPU frequency) and
restart, factory reset and safe mode.

## Step 2: test the chain from the printer back to the client

1. **UART only:** press "Print Test Page" in the bridge's web UI or on the thermal-printer device in HA. The printer prints a short test page: status,
   baud rate, the character replacements, a ruler line and a black bar. Nothing printed means wiring, level or baud rate.
2. **Printer status:** "Printer Online" turns on and "Printer Status" shows "Ready" within about 10 s (only with
   the printer's TX wired to the C3).
3. **Network path, from a computer on the LAN:**

   ```sh
   printf '\x1b@Hello QR204\n\n\n\n' | nc -w 2 thermal-printer.local 9100
   ```

4. **Home Assistant:** Developer tools → Actions → `esphome.thermal_printer_print_text` with
   `message: "Hello from Home Assistant"`.
5. **Sensor box (optional, separate project):** press "Print Screen" in its web UI (`http://<device-ip>/`).
   "Printer Status" there
   goes to "Sending", then "Done", and the page prints.

Continue with the [User Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/User-Guide) to operate it, or the
[Development Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Development-Guide) for the protocol details.
