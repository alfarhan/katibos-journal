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
- **AI proofread** (`service/Ai/`) — **Ctrl+G** in the editor sends the OPEN FILE to Gemini for a proofread (grammar, spelling incl. hamza + taa-marbuta/haa, punctuation) and replaces it. The prompt is Fouad's own wording, kept **verbatim** in `AI_PROMPT` — it *is* the spec, so don't paraphrase it; note it asks for hamza/taa corrections and forbids rephrasing or word substitution. Prompt and document go as two separate `parts`, so no in-band marker can collide with the text. Key/model/endpoint in `config.json` under `ai` (`key`, optional `model`, optional `url` for a local stand-in). Works on the file, not the editor buffer — the buffer is only a sliding window (`loadWindow`), so the document isn't in RAM. Runs as an `app["task"]="ai_correct"` pass pumped by `ai_loop()` on the background core, like sync; each stage marks the screen dirty since nothing renders while it blocks. Three guards, all learned the hard way: the reply is **refused** if it's under half or over double the original length (a model that rewrites or prepends commentary), the original is moved to `<file>_backup.txt` before the swap (the replace is far too big for the 2KB undo arena, so **Ctrl+Z cannot undo it** — the backup is the way back), and fence-stripping trims *exactly* the fence newlines rather than calling `trim()`, or it silently eats trailing blank lines the writer meant to keep. Capped at `AI_MAX_BYTES` (24KB) since request + reply are both in RAM.
- **Wi-Fi selection** (`service/Net/`) — `net_connect()` is the ONE place that brings the radio up; sync, OTA and AI proofread all call it (there used to be four copies, which is how the AI path shipped missing the post-association settle delay the sync path already had). Order: the SSID that worked last (`config.wifi_last`, **no scan** — the common case, ~2s), then saved networks seen in a scan **strongest-RSSI first**, then saved networks absent from the scan (hidden SSIDs, which scan-then-match can never find). Bounded: 6s per attempt, 24s total. A plain TCP connect to the provider's :443 then separates "no Wi-Fi" from "joined but no internet" — a restrictive hotspot leases an IP and drops 443, and without the probe that surfaced only as `start_ssl_client: -1`, which reads like a firmware bug.
- **Idle throttle** (`service/Idle/`) — after `config.idle_secs` (default 60, Preferences → Power save) with no keypress, the panel drops to `Low_Power_Mode()` and the main loop `delay(30)`s instead of spinning; any key restores both via `idle_touch()` in `display_keyboard()` (the single input funnel). It deliberately does **not** touch the CPU frequency: below 80MHz the ESP32-S3 takes the PLL down and kills USB-CDC, which on the Waveshare is both the console *and* the keyboard — the device would idle into a state typing can't wake. Two traps in the "is a task running" guard, both of which made it look permanently busy: a missing `app["task"]` stringifies to `"null"`, not `""`, and `SYNC_START` is **0**, which is also what an absent `sync_state` reads as. **Sleep (rev_8 only)** rides on the same module: `Sleep` = `esp_light_sleep_start()` (RAM kept, resumes mid-loop) and `Shut down` = `esp_deep_sleep_start()` (saves first, restarts from `setup()` and reopens the file at `caret_N`). Both stage off the idle timer, both default to **0 = off**, and both Preferences rows are hidden unless `sleep_supported()` — a BLE keyboard can't wake a radio that's off and USB-CDC input doesn't survive a sleep, so on the Waveshare there is nothing to wake with. Wake works by inverting the matrix (`keypad_prepare_wake()`): columns held HIGH as outputs, rows as `INPUT_PULLDOWN`, so any key pulls a row HIGH — chosen because `ESP_EXT1_WAKEUP_ANY_HIGH` is the one deep-sleep mode every ESP32 variant supports, unlike the low-side scan's ANY_LOW. rev_8 rows are all GPIO ≤21, i.e. RTC-capable, which is what ext1 requires. **None of the sleep path has been run on hardware** — there is no rev_8 here to test it on.
- **Sync** (`service/Sync/`) — Drive (Apps Script) or git provider (`config.sync.git`, token on-device). **OTA** (`service/Updater/Ota.cpp`) — Wi-Fi manifest at `KATIBOS_UPDATE_URL` (board-specific, set per-env in `platformio.ini`: microjournal → `latest.json`, waveshare → `latest_waveshare.json`, both in this repo); the bin download follows GitHub's cross-host redirect manually with a `setInsecure()` TLS client. Each manifest points at that board's bin on the `alfarhan/katibos` releases. The whole flow runs unattended (Connecting → Redirecting → Preparing storage → Downloading N% → Installing → Update installed), with Enter only at the end to reboot. Two traps behind that: **`Menu_render` only runs a sub-screen when something marked the menu dirty**, so `Ota_render` calls `Menu_clear()` while it has deferred work pending — without it the screen sat on "Connecting..." until an unrelated keypress happened to trigger the next render. And since nothing renders while `ota_apply()` holds the loop, every stage label pushes its own repaint (`setStage` → `g_redraw`). Download and flash are one phase, not two — `Update.writeStream` writes each chunk as it arrives — so the percentage is labelled "Downloading" and only `Update.end` (verify + switch boot partition) is "Installing".
- **UI kit** (`display/RLCD/display_RLCD.h`) — the shared drawing vocabulary; use these instead of hand-rolling a screen. `RLCD_drawTitleBar` (striped bar, title in a cleared tab — `Menu_drawHeader`/`Menu_drawTabs` call it, so *no screen draws its own header*), `RLCD_drawWindow` (shadow + frame + optional title bar: dialogs and info panels), `RLCD_drawHintBar` (key in plain text + verb on an inverted chip), `RLCD_drawScrollbar`, `RLCD_drawShapedLabel` / `RLCD_shapedLabelWidth` (Arabic-safe label, measure-then-place). Conventions: verbs uppercase, `^` = Ctrl/Fn, no brackets around keys, no screen draws a divider under the header. Hint bars: confirm dialogs put theirs under their own window; a screen whose keys exist *nowhere else* (Sync, Update, Wi-Fi, Keyboard) pins a footer instead — divider at y=276, bar centered at y=296, list scrollbars ending at 266 to clear it. Screens whose list fills the height (Language, Help, Settings) carry the key in each row and get no footer.
- Design/context docs: `firmware/doc/KATIBOS.md`, `SYSTEM_OPTIONS.md`, `DEVICE_SYNC.md`.

## Menu keys

One letter per destination, the same everywhere it's handled (file list, Settings grid, Help): **P** Preferences · **L** Language · **W** Wi-Fi · **S** Sync · **D** USB Drive · **U** Update · **H** Help · **A** About · **K** Keyboard (BLE builds only). File list: `ENT` open · `R` rename · `X` delete · `N` new · `←/→` switch tab. From the editor, **Ctrl+,** opens Preferences and **Ctrl+.** the Settings tab (both via a one-shot `menu.goto` that `Menu_setup` consumes).

Two traps this key map already walked into: a screen's own back key shadows a list letter (`B` used to eat `[B] Belgian` in Language — Esc/Left are the back keys there now), and the file list's `X`-for-delete exists so `D` can stay the global USB Drive jump. Keep `Help.cpp`'s tables in sync with the handlers — they are the only user-facing documentation of these keys, and its row pitch is set by the JUMP column clearing the callout box at y=228 (10 rows on a BLE build), so adding a destination means re-checking that.

The Settings grid shows each card's key by **underlining that letter in the card's own name** (`drawKeyedLabel`) instead of printing the key separately — so every card's key must actually occur in its label, matched case-insensitively on the first hit (`D` → "USB **D**rive", not the U). Language, Time zone and Sync provider are rows inside Preferences; About is a card of its own on the grid, since it's a destination rather than a setting.

## Cutting a release

`ota_check()` compares the manifest's `version` to `KATIBOS_VERSION` as a plain string — not newer/older — so the manifest must always name the version that's actually in the release, and the bins must be built *after* the bump.

1. Bump `KATIBOS_VERSION` in `firmware/src/app/app.h`.
2. `pio run -e waveshare && pio run -e microjournal`.
3. `gh release create vX.Y.Z --title "katibOS X.Y.Z" --notes "…"` with both bins, renamed to `firmware_waveshare.bin` / `firmware_microjournal.bin` (the manifests point at those exact asset names).
4. Point `firmware/latest.json` + `latest_waveshare.json` at the new version + asset URLs, commit, **push to `main`** — devices fetch them from `raw.githubusercontent.com/.../main/`, so an unpushed manifest ships nothing.

A device only offers an update when its own version string differs, so to test the OTA path flash the *previous* version over USB first, then release the new one.

## Notes

- `platformio.ini`'s `build_src_filter` is an explicit **allowlist** — a new `src/service/<X>/` directory does not compile until `+<service/X/*>` is added, and the failure is a link error, not a missing-file one. The emulator's `FW_CPP` list in `emulator/Makefile` is the same trap.
- Version string: `firmware/src/app/app.h` (`KATIBOS_VERSION`) — bump when cutting a release.
- The Waveshare's serial **does** stream `_log()` output to the Mac (`cat /dev/cu.usbmodem1101`, or `pio device monitor`) — useful for watching an OTA live. Boot prints `katibOS <version> on <board>`, which is the quickest way to confirm which build is running. Panic text, however, is lost on this board: the console is native USB-CDC and the panic handler can't drive TinyUSB while faulting, so a crash shows only the ROM banner (`rst:0xc`). `rst:0xc (RTC_SW_CPU_RST)` on its own is *not* evidence of a crash — a normal `ESP.restart()` prints exactly that, and `esp_core_dump_flash: No core dump partition found!` prints on **every** boot.
- Both boards' OTA is served from this repo now (`latest.json` + `latest_waveshare.json`); the deleted `katibos-waveshare` repo is gone. A device can still override the manifest via `config.json` `update.url`; a stale `katibos-waveshare`/`micro-journal` URL is treated as dead and falls back to the built-in board default.
- `~/projects/waveshare` was a fork that has been folded into this repo — don't recreate it.
- Global prefs: RTL-first (logical CSS props), Arabic when writing Arabic, terse output, comments only where the *why* is non-obvious.
