# MicroJournal rev_8 — native macOS emulator

Runs the **real** MicroJournal firmware UI (display/RLCD screens, word
processor, menus, Arabic bidi/shaping, locale, editor) natively on macOS in an
SDL2 window. Only the hardware bottom layer is replaced by host shims; all UI
and app logic is the actual firmware code under `../src` and `../lib`.

Emulates the `rev_8` target: reflective mono LCD (ST7306, logical 400×300
landscape) + 68-key matrix keypad + Arabic locale + BiDi.

## Prerequisites

```sh
brew install sdl2
```

## Build & run

```sh
make
./microjournal-emu
```

A 800×600 window (logical 400×300 ×2) opens showing the word processor.

## Keys

| Key | Action |
|-----|--------|
| letters / digits / punctuation | type into the editor |
| Esc | open the MENU |
| Enter | newline |
| Backspace | delete |
| Arrows | move cursor |
| Alt+Left / Alt+Right | jump a word (= the device's Ctrl+Left/Right) |
| Left/Right Shift | shift (uppercase / shifted symbols) |
| Cmd+Q or Ctrl+Q | quit |

> The device uses **Ctrl+Left/Right** for word-jump, but macOS reserves that
> chord for switching Spaces, so it never reaches this window. The emulator
> aliases **Alt(Option)+Left/Right** to it for testing (add Shift to word-select).

Inside the MENU: `L` = Language, `W` = WiFi, `U` = Drive mode, `B` = back. In the
Language screen, `R` selects **Arabic (RTL)**; `U` selects US, etc.

Keyboard layout can also be forced at launch:

```sh
EMU_LAYOUT=AR ./microjournal-emu     # start in Arabic
EMU_LAYOUT=US ./microjournal-emu     # start in US (default)
```

## Headless verification (no window)

`EMU_DUMP=1` runs setup + a few display ticks, writes the framebuffer to
`frame.pgm`, prints the black-pixel count, and exits (0 = something rendered):

```sh
EMU_DUMP=1 ./microjournal-emu
# -> EMU_DUMP: wrote frame.pgm (400x300), black pixels: 1048
```

`EMU_KEYS` injects a comma-separated list of USB-HID usage codes (decimal,
press+release each, with settle time) before the dump — handy for driving to
another screen headlessly. Esc = 41.

```sh
EMU_KEYS=41 EMU_DUMP=1 ./microjournal-emu          # dump the MENU
EMU_KEYS=41,15 EMU_DUMP=1 ./microjournal-emu       # dump the Language menu
EMU_KEYS=41,15,21 EMU_LAYOUT=US EMU_DUMP=1 ./microjournal-emu  # pick Arabic, back to WP
```

Convert the PGM to view it: `sips -s format png frame.pgm --out frame.png`.

## What's emulated vs stubbed

**Real (compiled from firmware):** display/RLCD word processor, Menu + Home /
Clear / Layout(Language) sub-screens, the editor, BiDi + Arabic shaping, all
keyboard locales, the ST7305 geometry (`ST73XX_UI`) and u8g2 fonts, config
load/save, the SD-card-backed filesystem (under `sdcard/`).

**Stubbed / not emulated:** WiFi & cloud Sync, BLE keyboard, USB mass-storage
"drive mode", OTA firmware update, the physical keypad matrix scan (SDL drives
input through the real `keyboard_HID2Ascii` pipeline instead). Those menu
entries show a "not available in emulator" notice.

## How it's wired (for maintainers)

- `shims/` — Arduino/`String`/`Print`/`Stream`/`SPI`/`FS`/HID-keycode headers,
  put first on the include path so they override the Arduino toolchain.
- `host_st7305.cpp` — implements the real `ST7305_4p2_BW_DisplayDriver` API but
  draws into a 400×300 1-byte/pixel framebuffer instead of pushing SPI.
- `host_main.cpp` — SDL window + event loop; maps SDL keys → HID codes →
  `keyboard_HID2Ascii`; pumps the real `display_loop` and blits the framebuffer.
- `host_app.cpp` / `host_fs.cpp` / `host_log.cpp` / `host_runtime.cpp` /
  `host_stubs.cpp` — host replacements for `app.cpp`, the filesystem, logging,
  Arduino timing/globals, and the hardware-only service/screen symbols.

No files under `../src` or `../lib` are modified.

### Notes / gotchas

- `String` (in `shims/WString.h`) is a **fixed-buffer, trivially-copyable**
  type on purpose: firmware passes `String` straight into `printf("%s", str)`,
  and clang inserts a runtime trap for non-trivially-copyable types passed
  through C varargs (the `-Wno-non-pod-varargs` flag only hides the warning, not
  the trap). The inline buffer is `HOST_STRING_CAP` (9000) bytes.
- `u8g2_fonts.c` is compiled as **C**, not C++: its font arrays are
  `const uint8_t foo[] = …`, which is external linkage in C but *internal* in
  C++ (every font symbol would vanish at link time).
- The Makefile has no header-dependency tracking; run `make clean && make`
  after editing a shim header.
