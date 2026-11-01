# QR204 Printer Bridge Development Guide

Implementation notes for the ESP32-C3 bridge: what this printer's firmware actually does, how the bridge is
built, and how the documentation is published.

## Target hardware

- ESP32-C3FH4 (single-core RISC-V), 4 MB embedded flash, native USB — reference board: Tenstar Robot
  ESP32-C3 Super Mini.
- ESPHome with the `esp-idf` framework, `min_version: 2025.5.0`, board `esp32-c3-devkitm-1`.
- QR204 panel printer, firmware `QR701_T1US-N32-2008`, 58 mm paper, 203 dpi, 384 dots per line, 9600 baud.

## Project layout

| Path | Purpose |
| --- | --- |
| [`esphome/thermal-printer.yaml`](https://github.com/Ajimaru/QR204-Printer-Bridge/blob/main/esphome/thermal-printer.yaml) | ESPHome config: entities, web UI, status LED, sun, `print_text` action |
| [`esphome/printer_bridge.h`](https://github.com/Ajimaru/QR204-Printer-Bridge/blob/main/esphome/printer_bridge.h) | TCP 9100 ↔ UART bridge, status polling, test page, printer info |
| [`datasheets/`](https://github.com/Ajimaru/QR204-Printer-Bridge/tree/main/datasheets) | QR204 manual (Chinese): pinout, ESC/POS command set |
| [`Thermal Printer Housing/`](https://github.com/Ajimaru/QR204-Printer-Bridge/tree/main/Thermal%20Printer%20Housing) | Housing and lid STLs |
| [`docs/`](https://github.com/Ajimaru/QR204-Printer-Bridge/tree/main/docs) | Wiki source, see "Documentation and wiki sync" |
| [`.github/workflows/`](https://github.com/Ajimaru/QR204-Printer-Bridge/tree/main/.github/workflows) | `validate` (pull-request status checks), `release` (config bundle and draft release) and `wiki-sync` (publishes `docs/`) |

## Bridge internals

One FreeRTOS task owns both the TCP server and the printer UART, so the ESPHome loop never waits for the
printer — at 9600 baud a 23 KB screen print takes about 24 s on the wire.

- The UART is driven through ESP-IDF directly, not through ESPHome's `uart` component, whose writes would
  block the loop at 9600 baud.
- Sockets use `lwip_*` calls because ESPHome builds without VFS select support.
- The UART TX ring buffer is 64 KB, so a whole screen print fits and the TCP sender is never throttled
  mid-job. With a 16 KB buffer, ESP-IDF 5.5's `uart_write_bytes()` busy-waits once the buffer is full and
  starves the ESPHome loop until the task watchdog fires; the bridge now only writes what fits and sleeps
  otherwise.
- `uart_lock` (a mutex) keeps a TCP job, the `print_text` action and the test page from interleaving on the
  UART.
- The bridge task runs at the same priority as the ESPHome loop task (1), so time slicing still feeds the
  task watchdog should the bridge ever busy-wait.
- `lwip_listen()` has a backlog of 1: one job at a time, further connections wait.
- Status polling pauses while a client is connected, and resumes once the printer has worked through the job.

### The unit used here (self-test page, 2026-09-28)

| Field | Value |
| --- | --- |
| Firmware version | QR701_T1US-N32-2008 (font 1.10) |
| Interface | USB & TTL (not RS232); measured 3.3 V idle on TX and RX |
| Baud rate | 9600 (matches `printer_baud`) |
| Language | PC936 (GB18030), i.e. Chinese mode by default |
| Status query (measured 2026-09-30) | `ESC v 0` doesn't match the manual: it answers `00 00` when ready and `08` without paper (the manual's "above 9.5 V" bit), and never sets "mechanism connected". The standard real-time status `DLE EOT n` works: `DLE EOT 2` answers `0x12` when ready, `0x32` without paper. Opening the lid reads as paper out |
| Self-test command `DC2 T` (tested 2026-09-30) | Ignored; the self-test page only prints with FEED held at power-on |
| Raster images (`GS v 0`, tested 2026-09-30) | The printer holds back a block until it is complete only up to ~1 KB. A larger block starts printing early and then waits for data at 9600 baud; every stop leaves a thin white line in dark areas (1128-byte blocks: line, 564-byte blocks: clean). `airquality` sends blocks of at most 576 bytes |
| Header before an image (tested 2026-09-30) | While the printer prints text lines, image data piles up; it prints that backlog at full speed and stops abruptly when it runs out, leaving a white line ~5 mm into the image. 2500 NUL bytes after the text avoid it (1000 were not enough); `airquality` sends 3000 |
| Printer ID `GS I n` (not in the manual, tested 2026-09-30) | n = 65–68: firmware `QR701_T1US-N32-2009` (the self-test page read 2008), manufacturer `GEZHI`, model `PT2D-V1.0`, serial number `17030800001`; the text answers have no terminator and start with a stray `-` or `_`. n = 1, 2, 3: model ID 0x60, type ID 0x03, ROM version 0x09. n = 69 (fonts): no answer |
| Unsolicited output | When the paper runs out the printer sends text on its TX line: `err loop!`, `no paper!` |
| Heating command `ESC 7 n1 n2 n3` (tested 2026-09-30) | Takes only one parameter; the next two bytes print as text (a stray "P" for the default heating time 80 = `P`). `ESC 7 n` with n = 0, 9, 40, 255 printed equally dark. Not used |
| Density `DC2 # n` (not in the manual, tested 2026-09-30) | Accepted as a command, but n = 0, 15, 31 printed equally dark (n = 31 also swallowed the next byte). Not used |
| Line spacing `ESC 3 n` (tested 2026-09-30) | Works, but n = 16 and n = 60 both gave more space than the default (`ESC 2`), which is already the most compact. Not used |
| Character set (tested 2026-09-30) | Always GB18030: `FS .`, `ESC t n`, `ESC R n` and `ESC 9 n` (UTF-8) are ignored. The font has `°` (tested) and the GB2312 Greek letters (`μ` for µ) but no ä, ö, ü, ß |
| Feed | `ESC d n` works, except right after a raster image (`GS v 0`), where it is ignored; a line feed first makes it work. About 6 lines clear the tear bar |
| Code type | codepage |
| Print mode / speed | S / 1 |
| Print temperature / depth | 20 °C / level 38 |
| Black label / clear buffer | disabled / disabled |

The printer takes every byte >= 0x80 as the start of a GB18030 character, which garbles Latin-1 text such as
"Luftqualität" (and swallows the line break after it). Both the bridge and its clients therefore convert text before
sending it: ä, ö, ü, ß become ae, oe, ue, ss; `°` and `µ` are sent as their GB2312 codes (printed double-width);
² and ³ become 2 and 3; anything else outside ASCII becomes `?`. The screen image itself is a bitmap and keeps
its umlauts.

The TX level is not on the self-test page. Measured on this unit (2026-09-29, printer on 9 V, idle): TX and RX
both at 3.3 V, i.e. 3.3 V TTL. It connects directly to the C3, no divider, no MAX3232.

## What gets sent (for reference)

A screen print from the [`airquality` sensor box](https://github.com/Ajimaru/Sensor-Box-Enhanced), in order — recorded
here because it is what the bridge was sized for, not because the bridge produces it:

1. `ESC @` (initialize)
2. With "Print Header" on: `ESC a 1` (centered), `ESC E 1` (bold), device name, `ESC E 0`, date and time, `ESC a 0`
   (text converted as described below)
3. `ESC J 8` (1 mm gap) and 3000 NUL bytes, both with the header only (see "Header before an image" below)
4. `GS v 0` raster blocks of at most 576 bytes, centered in the 384 dots; each row ends at the image's right
   edge (1.5x: 40 blocks of 12 rows × 47 bytes, 1x: 23 blocks of up to 14 rows × 39 bytes)
5. `LF` (this firmware ignores `ESC d` right after a raster image), `ESC d n` ("Print Feed Lines", default 5)

The bridge forwards jobs unchanged and polls `DLE EOT 2` (plus `DLE EOT 3` after an error) every 10 s while idle.

## Contributing changes

`main` is protected: it takes no direct pushes, and every change arrives through a pull request whose status
checks pass. That applies to the maintainer too — the only exception is the repository's very first commit,
made before the protection was enabled.

```sh
git switch -c <topic>
# edit, commit
git push -u origin <topic>
gh pr create --fill
```

Two workflows gate a pull request:

| Workflow | Job | What it checks |
| --- | --- | --- |
| `validate` | `esphome` | `esphome config thermal-printer.yaml` resolves every substitution, `!secret` and the C++ include, and validates each component's schema. CI writes a placeholder `secrets.yaml`; the real one lives on the Home Assistant host and is never committed |
| `validate` | `docs` | `docs/` has no split markdown links, no dead wiki anchors, no broken table rows, no link to a missing path, and no `Home.md` (which would overwrite the wiki's landing page) |

`esphome config` is a schema check, not a compile: it catches a malformed YAML or an unknown option, but not a
C++ error in `printer_bridge.h`. A full `esphome compile` pulls the ESP-IDF toolchain and takes several
minutes, so it is left to the flash itself.

## Releases

A release is a **config bundle**, not a firmware image. The WiFi credentials and the API key are compiled into
the firmware, so a prebuilt `.bin` would either not connect or carry someone else's secrets. The bundle
`QR204-Printer-Bridge-<version>.zip` holds `thermal-printer.yaml`, `printer_bridge.h`, `secrets.yaml.example`,
`INSTALL.md`, the housing STLs, `LICENSE` and `SECURITY.md`, with a `.sha256` next to it.

The version lives in one place, `esphome.project.version` in `thermal-printer.yaml`. The firmware reports it
to Home Assistant (device info) and announces it over mDNS as `project_version`; the web UI does not show it.
To cut a release:

1. Set `esphome.project.version` to the new version, for example `"0.2.0"`, in a pull request, and merge it.
2. Tag the merged commit on `main` with that version and a leading `v`, and push the tag:

   ```sh
   git tag v0.2.0
   git push origin v0.2.0
   ```

The `release` workflow then:

1. refuses a tag that is not on `main` (the branch is protected, tags are not);
2. refuses a tag that does not equal `esphome.project.version` (`v0.2.0` or `0.2.0` against `"0.2.0"`), so the
   firmware cannot report a different version than the release it came from. A prerelease tag such as
   `v0.2.0-rc1` needs the version `"0.2.0-rc1"` in the file;
3. builds the bundle and proves it installs: it copies `secrets.yaml.example`, swaps in a generated API key and
   runs `esphome config` from the bundle folder, so an example that misses a secret the config needs fails here;
4. creates a **draft** release with generated notes and attaches the zip and checksum.

The release is always a draft. Pushing a tag never publishes on its own: open the draft, read the notes, check
the attached files and publish by hand. A tag with an `a`, `b`, `rc`, `dev`, `alpha` or `beta` segment
(`v0.2.0-rc1`) is marked as a prerelease. Tags are `v*` or a bare version number.

Pull requests and manual runs build and check the same bundle but create no release; the zip is kept as a
workflow artifact for inspection. They also check that the version can be read from `thermal-printer.yaml`, but
do not compare it with a tag. This check is not a required status check.

The notes group merged pull requests by label, according to [`.github/release.yml`](https://github.com/Ajimaru/QR204-Printer-Bridge/blob/main/.github/release.yml).
A pull request lands in the first category whose label it carries, and an unlabelled one still appears under
"Other Changes". Label a pull request `skip-changelog` to leave it out.

| Label | Section |
| --- | --- |
| `breaking`, `breaking-change` | Breaking Changes |
| `security` | Security |
| `enhancement`, `feature` | New Features |
| `bug`, `fix` | Bug Fixes |
| `hardware`, `housing` | Hardware & Housing |
| `documentation`, `docs` | Documentation |
| `dependencies` | Dependencies |
| `ci`, `tooling`, `chore`, `github_actions` | CI / Tooling |

## Documentation and wiki sync

`docs/` is the single source of truth. Every file there is a wiki page and the filename is the page name. The
`wiki-sync` workflow copies `docs/*.md` to the GitHub wiki on every push to `main` that touches them, and can
also be run manually from the Actions tab. **Edits made directly in the wiki are overwritten by the next
sync** — change the file under `docs/` instead, or the change is lost the next time anyone edits documentation.

The wiki's `Home`, `_Sidebar` and `_Footer` pages are not synced and have no counterpart in `docs/`. They are
the landing page and navigation, maintained in the wiki itself; templates for the initial setup are in
[`wiki/`](https://github.com/Ajimaru/QR204-Printer-Bridge/tree/main/wiki).

The sync needs a personal access token with `repo` scope stored as the `WIKI_TOKEN` secret: `GITHUB_TOKEN`
cannot write to a wiki. Without it the workflow fails at the clone step.

The documentation workflow ends here. Return to the
[Features](https://github.com/Ajimaru/QR204-Printer-Bridge/wiki/Features) page for the project overview.