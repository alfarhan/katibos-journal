# katibOS

Arabic-first (RTL) distraction-free writing firmware for ESP32-S3 e-ink/reflective-LCD typewriters. C++ / Arduino / PlatformIO. Code is in `firmware/`.

## One codebase, two boards

This repo builds two hardware targets from **one shared tree**. All board differences are `#ifdef`-driven — code for one board compiles (inert) on the other, so a change to shared code must keep both working.

| Env | Board | Input |
|-----|-------|-------|
| `microjournal` (default), `microjournal_type1`, `microjournal_type2` | MicroJournal rev_8 | physical **matrix keypad** (`KEYPAD_68`) |
| `waveshare` | Waveshare ESP32-S3-RLCD-4.2 | **USB serial** (`USE_SERIAL_KEYBOARD`) + **BLE-HID host** (`USE_BLE_KEYBOARD_HOST`) |

Each env sets `BOARD_NAME` (shown at boot in serial and on the About screen).

Same 400×300 ST7305/ST7306 reflective LCD on both (different pins per env). `type1/type2` set `RLCD_TYPE` for panel supplier variants.

## Build / flash (from `firmware/`)

```
pio run -e microjournal                                # microjournal
pio run -e waveshare                                   # waveshare
pio run -e <env> -t upload --upload-port <PORT>         # flash (e.g. /dev/cu.usbmodem1101)
pio device monitor -e <env> --port <PORT>               # 115200, exception_decoder on
```
Waveshare enumerates as native USB-CDC (`ARDUINO_USB_CDC_ON_BOOT`); it's BLE-LE only (no Classic). Upload fails "port busy" if a monitor is open — close it first.

## Where things live

- **Editor core** (`service/Editor/`) — driver-agnostic; buffer, undo, selection, wrapping. Shared by all fronts, so keep changes driver-neutral.
- **Renderer** (`display/RLCD/WordProcessor/`) — draws the editor. `display_RLCD_core()` returns 0 → render runs on core 0 with input (avoids a cross-core buffer race that panicked on fast typing).
- **Input** (`keyboard/`): `keyboard.cpp` HID/dispatch; `Keypad/68/Keypad_68.cpp` has the matrix path (`#ifndef USE_SERIAL_KEYBOARD`) AND the serial path (`#ifdef USE_SERIAL_KEYBOARD`); `BLEHost/` is the BLE keyboard host; `Locale/` layout tables (`keyboard_us_equivalent` reverses a localized letter to its US base for layout-independent menu shortcuts).
- **Editor commands** map to shared action codes (`SEL_*`, `COPY`, `SELECTALL`, …) in `display/display.h`; matrix/BLE deliver them via HID, serial via `Keypad_68.cpp` shims.
- **Sync** (`service/Sync/`) — Drive (Apps Script) or git provider (`config.sync.git`, token on-device). **OTA** (`service/Updater/Ota.cpp`) — Wi-Fi manifest at `KATIBOS_UPDATE_URL` (board-specific, set per-env in `platformio.ini`: microjournal → `latest.json`, waveshare → `latest_waveshare.json`, both in this repo); the bin download follows GitHub's cross-host redirect manually with a `setInsecure()` TLS client. Each manifest points at that board's bin on the `alfarhan/katibos` releases.
- Design/context docs: `firmware/doc/KATIBOS.md`, `SYSTEM_OPTIONS.md`, `DEVICE_SYNC.md`.

## Notes

- Version string: `firmware/src/app/app.h` (`KATIBOS_VERSION`) — bump when cutting a release.
- Both boards' OTA is served from this repo now (`latest.json` + `latest_waveshare.json`); the deleted `katibos-waveshare` repo is gone. A device can still override the manifest via `config.json` `update.url`; a stale `katibos-waveshare`/`micro-journal` URL is treated as dead and falls back to the built-in board default.
- `~/projects/waveshare` was a fork that has been folded into this repo — don't recreate it.
- Global prefs: RTL-first (logical CSS props), Arabic when writing Arabic, terse output, comments only where the *why* is non-obvious.
