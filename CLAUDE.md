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
- **Battery** (`service/Battery/`) — Waveshare only (`-D BATTERY` in that env): the 18650 on **GPIO4 = ADC1_CH3** behind the board's **3× divider**, sampled every 10s and mapped to a percent via a Li-ion discharge table; shown in the editor status bar. rev_8 has no sense line (cell → charger/step-up module → ESP32, nothing on an ADC), so the module compiles inert there and `battery_percent()` returns -1, which the status bar omits. ADC1 is deliberate — ADC2 is unusable with Wi-Fi on.
- **Sync** (`service/Sync/`) — Drive (Apps Script) or git provider (`config.sync.git`, token on-device). **OTA** (`service/Updater/Ota.cpp`) — Wi-Fi manifest at `KATIBOS_UPDATE_URL` (board-specific, set per-env in `platformio.ini`: microjournal → `latest.json`, waveshare → `latest_waveshare.json`, both in this repo); the bin download follows GitHub's cross-host redirect manually with a `setInsecure()` TLS client. Each manifest points at that board's bin on the `alfarhan/katibos` releases.
- **UI kit** (`display/RLCD/display_RLCD.h`) — the shared drawing vocabulary; use these instead of hand-rolling a screen. `RLCD_drawTitleBar` (striped bar, title in a cleared tab — `Menu_drawHeader`/`Menu_drawTabs` call it, so *no screen draws its own header*), `RLCD_drawWindow` (shadow + frame + optional title bar: dialogs and info panels), `RLCD_drawHintBar` (key in plain text + verb on an inverted chip), `RLCD_drawScrollbar`, `RLCD_drawShapedLabel` / `RLCD_shapedLabelWidth` (Arabic-safe label, measure-then-place). Conventions: verbs uppercase, `^` = Ctrl/Fn, no brackets around keys, no screen draws a divider under the header, and only the four confirm dialogs keep a hint bar (sat under their window, not pinned to the screen bottom).
- Design/context docs: `firmware/doc/KATIBOS.md`, `SYSTEM_OPTIONS.md`, `DEVICE_SYNC.md`.

## Menu keys

One letter per destination, the same everywhere it's handled (file list, Settings grid, Help): **P** Preferences · **L** Language · **W** Wi-Fi · **S** Sync · **D** USB Drive · **U** Update · **H** Help · **K** Keyboard (BLE builds only). File list: `ENT` open · `R` rename · `X` delete · `N` new · `←/→` switch tab. From the editor, **Ctrl+,** opens Preferences and **Ctrl+.** the Settings tab (both via a one-shot `menu.goto` that `Menu_setup` consumes).

Two traps this key map already walked into: a screen's own back key shadows a list letter (`B` used to eat `[B] Belgian` in Language — Esc/Left are the back keys there now), and the file list's `X`-for-delete exists so `D` can stay the global USB Drive jump. Keep `Help.cpp`'s tables in sync with the handlers — they are the only user-facing documentation of these keys.

## Cutting a release

`ota_check()` compares the manifest's `version` to `KATIBOS_VERSION` as a plain string — not newer/older — so the manifest must always name the version that's actually in the release, and the bins must be built *after* the bump.

1. Bump `KATIBOS_VERSION` in `firmware/src/app/app.h`.
2. `pio run -e waveshare && pio run -e microjournal`.
3. `gh release create vX.Y.Z --title "katibOS X.Y.Z" --notes "…"` with both bins, renamed to `firmware_waveshare.bin` / `firmware_microjournal.bin` (the manifests point at those exact asset names).
4. Point `firmware/latest.json` + `latest_waveshare.json` at the new version + asset URLs, commit, **push to `main`** — devices fetch them from `raw.githubusercontent.com/.../main/`, so an unpushed manifest ships nothing.

A device only offers an update when its own version string differs, so to test the OTA path flash the *previous* version over USB first, then release the new one.

## Notes

- Version string: `firmware/src/app/app.h` (`KATIBOS_VERSION`) — bump when cutting a release.
- The Waveshare's serial **does** stream `_log()` output to the Mac (`cat /dev/cu.usbmodem1101`, or `pio device monitor`) — useful for watching an OTA live. Boot prints `katibOS <version> on <board>`, which is the quickest way to confirm which build is running. Panic text, however, is lost on this board: the console is native USB-CDC and the panic handler can't drive TinyUSB while faulting, so a crash shows only the ROM banner (`rst:0xc`). `rst:0xc (RTC_SW_CPU_RST)` on its own is *not* evidence of a crash — a normal `ESP.restart()` prints exactly that, and `esp_core_dump_flash: No core dump partition found!` prints on **every** boot.
- Both boards' OTA is served from this repo now (`latest.json` + `latest_waveshare.json`); the deleted `katibos-waveshare` repo is gone. A device can still override the manifest via `config.json` `update.url`; a stale `katibos-waveshare`/`micro-journal` URL is treated as dead and falls back to the built-in board default.
- `~/projects/waveshare` was a fork that has been folded into this repo — don't recreate it.
- Global prefs: RTL-first (logical CSS props), Arabic when writing Arabic, terse output, comments only where the *why* is non-obvious.
