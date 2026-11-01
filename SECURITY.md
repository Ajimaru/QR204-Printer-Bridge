# Security policy

This is a hobby project: an ESPHome configuration plus a C++ include that turn an ESP32-C3 into a network
bridge for a QR204 thermal printer. It is meant to run on a private LAN behind a router, next to a Home
Assistant instance. The notes below describe what it exposes and where its limits are, so you can decide
whether that fits your network.

## Supported versions

Only the current state of this repository is maintained. There are no releases and no backports; fixes land on
the main branch, and you update by re-flashing the device (over the air from ESPHome Device Builder).

## Reporting a vulnerability

Report security problems privately, not as a public issue:

- Preferably through GitHub's **Security → Report a vulnerability** form (private vulnerability reporting), if
  this repository has it enabled.
- Otherwise by email to **ajimaru_gdr@pm.me**, with "QR204 printer bridge" in the subject.

Please include what the issue allows an attacker to do, which file or entity it concerns, and the steps to
reproduce it. This is a spare-time project, so expect a first answer within about two weeks. There is no bug
bounty. If the finding affects ESPHome, ESP-IDF or Home Assistant itself rather than this configuration, report
it to that project instead ([ESPHome security policy](https://github.com/esphome/esphome/security/policy),
[Home Assistant](https://www.home-assistant.io/security/)).

## Threat model and known exposure

The bridge is designed for a trusted LAN. Anyone who can reach the device's IP address is treated as
authorized. Specifically:

### TCP port 9100 has no authentication

`printer_bridge.h` binds port 9100 on all interfaces (`INADDR_ANY`) and accepts one connection at a time.
This is the standard raw-printing protocol (JetDirect / AppSocket), which has no login, no TLS and no access
control — the same as any network receipt printer. Consequences on a reachable network:

- Anyone on the LAN can print arbitrary bytes, including ESC/POS commands, and so waste paper or change
  printer settings that persist until a power cycle.
- Bytes are forwarded to the printer unchanged, by design: the bridge is a pipe, not a filter. It does not
  validate or sanitise job content.
- Bytes the printer sends back are forwarded to the connected client, so a client sees the printer's status
  replies.
- While one client is connected, other connections wait, and the 10-second status polling pauses. A client
  that opens the connection and sends nothing occupies the printer until it disconnects or the device reboots.

**Do not forward port 9100 through your router, and do not expose it to a guest or IoT VLAN you do not
trust.** If you need stricter control, restrict the port with firewall or VLAN rules on your network
equipment; the firmware does not implement an allowlist.

### Web UI and Home Assistant API

- The ESPHome web server on port 80 is protected with HTTP digest authentication
  (`web_server_username` / `web_server_password` from `secrets.yaml`). Digest auth is not encrypted transport:
  the session itself is plain HTTP, so treat it as LAN-only.
- The native API (port 6053) uses ESPHome's Noise encryption with `api_encryption_key`. As shipped, this
  config reuses the key of another ESPHome device on the same host, which means one leaked key covers both.
  Generate a separate key
  (`openssl rand -base64 32`) if you want them isolated.
- `reboot_timeout: 0s` keeps the device running without a connected API client. That is intentional for a
  printer, but it also means a lost Home Assistant connection is not a visible failure.

### OTA updates

`ota: platform: esphome` is configured without an explicit password. ESPHome derives OTA authentication from
the API encryption key, so the key protects updates as well — which is another reason to keep it secret and,
ideally, device-specific. Anyone who obtains it can flash arbitrary firmware onto the device.

### WiFi access point fallback

The config enables an `ap:` fallback and `captive_portal:`. If the configured WiFi is unreachable, the device
opens an access point named `thermal-printer`, protected with the same password as your WiFi network
(`wifi_password`). The captive portal lets whoever joins that AP enter new WiFi credentials. The AP is only
up while the main network is down.

### Logs

`logger: level: DEBUG` is verbose. The log (web UI log panel, USB serial, HA) shows job sizes, printer status
and the text passed to `print_text`, so printed content can appear in logs. It does not print the WiFi
password or the API key. Lower the level to `INFO` or `WARN` if the log is visible to others.

### Secrets in this repository

No credentials are committed. The YAML references `!secret` values that live in `secrets.yaml` on the Home
Assistant host, which is not part of this repository. The sun-position coordinates are also kept out of the
repo — latitude and longitude are entered at runtime in the web UI, and the YAML only holds `0 / 0` as a
placeholder. If you fork this, keep your own `secrets.yaml` out of version control.

## Safety, not security

Two hardware points can destroy parts or cause a burn hazard, and they are covered in detail in the
[Assembly Guide](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Assembly-Guide), steps 1 and 2. In
short: set both buck converters with a multimeter **before** connecting them to the printer or the
ESP32-C3 (a Mini-360 often ships with its output near the input voltage, and 12 V on the C3's 5V pin
destroys the board), keep the printer below 9.0 V, and never wire an RS232-level printer port directly to
the C3. The print head gets hot during long dark prints; the firmware
reports "Error: print head too hot or supply voltage" but does not control heating.
