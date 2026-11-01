# QR204 Printer Bridge Setup Guide

This guide covers the firmware, how a client finds the bridge, and the end-to-end test. Start with the [Assembly Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Assembly-Guide) if the hardware is not wired yet.

## Step 1: firmware

The firmware is compiled on your own machine from the ESPHome config, so you need ESPHome and a USB cable for
the first flash. There is no prebuilt `.bin`: the WiFi credentials and the API key are compiled into the
firmware, so a ready-made image would either not connect or carry somebody else's secrets.

### What you need

- **ESPHome**, in one of two forms: the *ESPHome Device Builder* add-on in Home Assistant, or the command line
  (`pip install esphome`). Home Assistant is not required to run the bridge afterwards: port 9100 and the web UI
  work without it.
- A **USB data cable** from your computer to the C3. Do not plug in USB while the Mini-360 powers the C3's `5V`
  pin (see the [Assembly Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Assembly-Guide)).
- The **files**: download `QR204-Printer-Bridge-<version>.zip` from the
  [Releases page](https://github.com/Ajimaru/QR204-Printer-Bridge/releases) and unpack it, or clone this repository. It contains `thermal-printer.yaml`,
  `printer_bridge.h` and `secrets.yaml.example`. Check the `.sha256` file next to the download if you like.

### Install

1. **Secrets.** Copy `secrets.yaml.example` to `secrets.yaml` and replace all five values:

   | Key | Value |
   | --- | --- |
   | `wifi_ssid`, `wifi_password` | Your WiFi. The password is also the password of the fallback access point, so it needs at least 8 characters |
   | `api_encryption_key` | Generate with `openssl rand -base64 32`. It also protects over-the-air updates, so keep it secret |
   | `web_server_username`, `web_server_password` | Login for the bridge's web UI |

   If your ESPHome folder already has a `secrets.yaml` with these five names (a Home Assistant host that runs
   other ESPHome devices, for instance), add nothing: the bridge reuses them. To give it a key of its own, add
   another entry and point `api: encryption: key:` at it.
2. **Settings.** Check the `substitutions:` at the top of `thermal-printer.yaml`:
   - `printer_baud`: the rate from the printer's self-test page (default 9600).
   - `printer_tx_pin` / `printer_rx_pin`: 7 / 6, as wired in the Assembly Guide.
3. **Folder.** Put `thermal-printer.yaml`, `printer_bridge.h` and `secrets.yaml` together in one folder: on a
   Home Assistant host `/homeassistant/esphome/`, otherwise any folder you run ESPHome from.
4. **First flash over USB.**
   - Device Builder: the new "thermal-printer" card → Install → Plug into this computer.
   - Command line: `esphome run thermal-printer.yaml`, then pick the USB serial port.

   The first build downloads the ESP-IDF toolchain and takes several minutes. Later updates go over the air:
   `esphome run thermal-printer.yaml --device thermal-printer.local`.
5. **Home Assistant (optional).** The device shows up under Settings → Devices & services → ESPHome. Enter the
   `api_encryption_key` when asked.

To print screens from the [`airquality` sensor box](https://github.com/Ajimaru/Sensor-Box-Enhanced), that device
needs its own client include and a reflash. Both belong to its repository, not to this one.

### How a client finds the bridge

The C3 gets its IP address by DHCP and announces itself via mDNS as `thermal-printer.local`, so a client can
address it by name and a new DHCP address needs no change. `airquality` keeps that address in its own
"Printer Host" entity (its web UI, section "Printer"; IP or host name, default `thermal-printer.local`) and
resolves it on every print. If name resolution ever fails, enter the C3's current IP there instead (router
device list or the C3's web UI); no reflash needed.

### Web UI of the bridge

<http://thermal-printer.local/>, with the `web_server_username` and `web_server_password` from your `secrets.yaml`.
Section "Printer" at the top: status, online, paper out, test page button, job counters and the status LED
controls. Below it the usual ESPHome diagnostics (device info, reset reason, heap, loop time, uptime, WiFi
strength, CPU frequency) and restart, factory reset and safe mode.

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
   "Printer Status" there goes to "Sending", then "Done", and the page prints.

Continue with the [User Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/User-Guide) to operate it, or the
[Development Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Development-Guide) for the protocol details.
