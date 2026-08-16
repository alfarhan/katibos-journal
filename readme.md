# katibOS — MicroJournal rev_8

**katibOS** (كاتب — "writer") is an Arabic-first writing device OS built on the
**MicroJournal rev_8** hardware. A distraction-free digital typewriter: turn it on, write,
sync. Originally forked from [unkyulee/micro-journal](https://github.com/unkyulee/micro-journal)
and trimmed to the rev_8 OS only.

## Highlights

- Arabic input — RTL/BiDi + pixel-accurate contextual shaping (line wrapping included)
- Arabic-first menu/UX: file titles, rename, file browser
- Editor Options (Ctrl+,): text flow, line spacing, text align (incl. justify), theme, status bar
- Distraction-free hidden status bar (reclaims a text line; top-left toasts)
- Classic-Mac menu UI: striped title bars, dialog windows, an icon grid for Settings,
  and shortcuts shown as inverted keycaps (Ctrl+. jumps straight there)
- Google Drive two-way sync, and **OTA Wi-Fi updates** (Settings → Check for Update)

## Layout

| | |
| --- | --- |
| [`firmware/`](firmware) | The OS — PlatformIO project (ESP32-S3). Build envs: **`microjournal`** (default) and the `_type1`/`_type2` panel variants. |
| [`firmware/emulator/`](firmware/emulator) | SDL desktop emulator (build & run the OS with no hardware). |
| [`firmware/tests/`](firmware/tests) | Host unit tests. |
| [`firmware/doc/KATIBOS.md`](firmware/doc/KATIBOS.md) | Detailed design / feature doc. |

Version: `KATIBOS_VERSION` in [`firmware/src/app/app.h`](firmware/src/app/app.h).

Hardware (enclosure STLs, build guide) lives upstream:
[unkyulee/micro-journal › micro-journal-rev-8](https://github.com/unkyulee/micro-journal/tree/main/micro-journal-rev-8).

## Build & run

```sh
cd firmware

# Device (ESP32-S3)
pio run -e microjournal              # build
pio run -e microjournal -t upload    # flash over USB

# SDL emulator (desktop, no hardware)
cd emulator && make && ./microjournal-emu
```

## Releases / OTA

Firmware is published as GitHub Releases with a `firmware_microjournal.bin` asset (the name the
device's SD-card updater also expects). The device reads a manifest to find newer builds:

1. Bump `KATIBOS_VERSION` in `app.h`.
2. `pio run -e microjournal` → `.pio/build/microjournal/firmware.bin`.
3. `gh release create vX.Y --target main --title "katibOS X.Y" firmware_microjournal.bin`.
4. Update [`firmware/latest.json`](firmware/latest.json) (`version` + asset `url`) and push.

Device `config.json` points at the raw manifest:

```json
"update": { "url": "https://raw.githubusercontent.com/alfarhan/katibos/main/firmware/latest.json" }
```

First install must be over USB/SD (OTA can't bootstrap itself); after that, updates are over the air.

---

Based on [unkyulee/micro-journal](https://github.com/unkyulee/micro-journal).
Licensed under [LICENSE.txt](LICENSE.txt).
