# katibOS — rev8 menu/UX redesign + file titles

Branch: **`katibOS`** (off `arabic-rev8`). Design reference: the `rev8-ux.html` artifact.

## Context

The rev8 menu UX grew ad-hoc: every screen invents its own keys (`B` vs `Esc` vs "any key"), files are bare numbered slots `[0]`–`[9]` with no titles, no current state is shown (language, battery, sync, active file), editor shortcuts are undiscoverable, and the picker is hard-capped at 10. katibOS makes it one coherent system:

1. Files have **titles** (auto from first line, manually overridable).
2. **Paged, cursor-driven** file list — scales past 10.
3. One **key contract** everywhere (`↑↓` move, `Enter` open, `Esc` back, number = jump).
4. Real state surfaced (battery, locale, **sync chip**) + a **Help** screen.

**Constraints:** 400×300 mono reflective LCD (ST7305/ST7306), ~46×16 chars in `u8g2_font_profont17_tf`; keyboard-only (no knob); RTL-aware. Target front-end: **RLCD** only. `Editor` is shared across drivers → its changes stay driver-agnostic.

**Data-safety (non-negotiable):** on-disk files stay `/<index>.txt`; splice-save + `.tmp` + `.recovery` does NOT change. Titles/sync state are config metadata only. Prove everything in the emulator.

## Locked decisions

- Full redesign (tabs, cursor nav, Help) + naming/rename/sync-chip.
- Unlimited files, paged; numbered `/<index>.txt` on disk (uncapped), enumerated via directory listing; "new file" = next free index.
- Title: auto from first non-empty line (computed at save, cached in config); manual `Rename` overrides; blank reverts to auto.
- Sync uploads rename to `<title>.txt` (Phase 6 caveat); USB stays numbered (deferred).
- Cursor-on-open: unchanged, not in scope.

---

## Testing strategy (TDD)

Pure logic is extracted into **dependency-free translation units** (only the `String` shim / ArduinoJson, no `app.h`, no hardware) and tested first, red→green, before wiring into firmware. Rendering / key-routing / screen flow get integration coverage in the emulator.

- **Unit harness:** `tests/` (firmware project) — header-only `microtest.h` (no gtest dependency, instant), built by `tests/Makefile`. Run: `cd tests && make`. Links the pure units + `emulator/shims` + ArduinoJson.
- **Integration harness:** the SDL emulator, headless — `EMU_KEYS=<hid,codes> EMU_DUMP=1 ./microjournal-emu` → `frame.pgm` + black-pixel count.
- **Rule:** every phase adds unit tests for its pure helpers *first*, then implementation, then an emulator check. `make clean && make` after any header edit.

Pure units under test:

| Unit | File | Tests |
|---|---|---|
| `deriveTitle()` | `src/service/Tools/TextUtil.cpp` | first non-empty line, trim, cap, blank/whitespace-only, CRLF, RTL bytes |
| `sanitizeFilename()` | `src/service/Tools/TextUtil.cpp` | strip illegal chars, collapse, empty→`untitled`, length cap |
| pagination | `src/display/RLCD/Menu/FileList/Pagination.h` | page count, row→abs index, clamp, page-relative jump, empty/overflow |
| unsynced count | `src/service/Tools/TextUtil.cpp` (or SyncState util) | count over a JSON config, none/all/mixed |

---

## Phased implementation

Order puts the safe data layer first; each phase is independently testable. Ship 1→2→3 for a usable naming feature even if 4–6 slip.

### Phase 1 — Title + sync-flag data layer
- **New pure unit** `src/service/Tools/TextUtil.{h,cpp}` (deps: `String` only): `deriveTitle(text, maxLen=28)` and `sanitizeFilename(in)`. **Unit-tested first.**
- **`Editor.cpp` `saveFile()` (~386–389):** after `this->saved=true`, set `app["config"][format("title_%d", i)]` to `deriveTitle(buffer)` **unless** `title_manual_%d` is set; set `unsynced_%d = true`. Reuse existing `config_save()` at 389.
- **`Editor.cpp` `clearFile()` / new-file:** clear `title_*`, `title_manual_*`, `unsynced_*` for that index.
- Verify: type → save → `EMU_DUMP`/dump `config.json` shows correct keys.

### Phase 2 — Show titles (list + status bar)
- `Home.cpp` `Home_render()` (56–63): `slot · title · words` from cached `title_%d` (no file reads in render). Empty → `— empty —`.
- `WordProcessor.cpp` status bar (~607–627): title after file index (truncate ~30), keep words/locale/battery/clock.

### Phase 3 — Rename screen (`MENU_RENAME`)
- New `src/display/RLCD/Menu/Rename/Rename.{h,cpp}` cloned from `Wifi.cpp` edit flow; reuses `BufferService`. `\n` save / `\b` delete / `27` cancel / printable append.
- Blank → clear manual flag (auto); non-blank → set `title_%d` + `title_manual_%d`. Wire into `Menu.cpp`; `R` on focused row in FILES.

### Phase 4 — FILES / SETTINGS tabs + cursor + paging
- Shared cursor + `Esc`-back + persistent header/footer.
- `Home.cpp` → FILES: paged cursor list. **Unlimited model:** add `listTextFiles()` enumeration to `FileSystem.h`/`FileSystemFAT.h` (confirm FS dir iteration). 10/page; `↑↓` move, `PgUp/PgDn` page, `0-9` jump (page-relative), `Enter` open, `R` rename, `^D` delete, `N` new, `→` Settings, `Esc` editor. **Pagination math is a unit-tested pure header.**
- New `Settings/` (`MENU_SETTINGS`): Language, Wi-Fi, Sync, BLE, Drive, Help, About — inline current values; cursor + `Enter`; `←` Files.
- `Layout.cpp`: cursor + current `✓` (keep letter fast-path).
- New `Help/` (`MENU_HELP`): static cheat-sheet.
- Consistency pass: `Clear.cpp` (name the file), `Storage.cpp`, `Sync.cpp` render → header/footer + `Esc`.

### Phase 5 — Sync chip
- Wi-Fi is off while writing (`Sync.cpp:165/180`), so show a **sync chip** from the per-file `unsynced_%d` boolean: `✓ synced` or `⟳ N to sync`. **Count is unit-tested.** Clear `unsynced_%d` at `SYNC_COMPLETED` (`Sync.cpp:351`).

### Phase 6 — Sync rename-on-upload — DONE (emulator-verified 2026-06-26)
- `SyncCore.cpp::sync_upload_index` appends `id=<drive_id_N>` (line 190) and `name=<title>.txt` (line 210) to the upload URL; reconcile pushes on local rename (`title_N != synced_title_N`, line 410). The Apps Script ([sync.js](../install/google/sync.js)) prefers `getFileById(id).setName(name)` (line 43) so the SAME Drive file is renamed, never duplicated; duplicate titles disambiguate by slot index (baked into the title). USB stays numbered (raw FAT, deferred).
- **Verified end-to-end** against the live endpoint via `EMU_KEYS="c24" EMU_SYNC=1`: renamed slot 0 → re-synced → Drive kept one file, same id, new name (then reverted). `name`/`id` slashes are stripped client-side; `.txt` appended by the caller.

---

## Files touched

| Area | Path | Change |
|---|---|---|
| Pure utils | `src/service/Tools/TextUtil.{h,cpp}` *(new)* | `deriveTitle`, `sanitizeFilename` (unit-tested) |
| Pagination | `src/display/RLCD/Menu/FileList/Pagination.h` *(new)* | pure page math (unit-tested) |
| State ids | `src/display/display.h` | `MENU_SETTINGS/RENAME/HELP` (16–18) |
| Dispatcher | `src/display/RLCD/Menu/Menu.cpp` | wire new states |
| Files tab | `src/display/RLCD/Menu/Home/Home.cpp` | cursor + paging + titles + R/Del/N |
| Settings | `src/display/RLCD/Menu/Settings/` *(new)* | moved actions, cursor, inline state |
| Rename | `src/display/RLCD/Menu/Rename/` *(new)* | text-entry (reuses Buffer) |
| Help | `src/display/RLCD/Menu/Help/` *(new)* | cheat-sheet |
| Language | `src/display/RLCD/Menu/Layout/Layout.cpp` | cursor + current `✓` |
| Delete | `src/display/RLCD/Menu/Clear/Clear.cpp` | name the target |
| Editor | `src/service/Editor/Editor.cpp` | title + `unsynced` at save; cleanup on clear |
| Status bar | `src/display/RLCD/WordProcessor/WordProcessor.cpp` | title + sync chip |
| FileSystem | `src/app/FileSystem/FileSystem.h`, `FileSystemFAT.h` | `listTextFiles()` |
| Sync | `src/service/Sync/Sync.cpp` | clear `unsynced`; filename param (P6) |
| Tests | `tests/` *(new)* | `microtest.h`, `Makefile`, `test_*.cpp` |

Reused: `BufferService.*`, `WifiEntry.cpp` (pattern), `format()`, `config_save/load`, Wifi module template.

## Verification

Per memory `emulator-testing-workflow`. `make` from emulator dir; `make clean && make` after header edits.

1. **Unit:** `cd tests && make` green after every phase that adds a pure helper.
2. **P1:** type+save → dump `config.json` shows `title_N`/`title_manual_N`/`unsynced_N`.
3. **P2:** `EMU_KEYS` to menu → titles in list + status bar.
4. **P3:** Rename → manual title shows; blank → reverts to auto.
5. **P4:** >10 files: page, jump, new, delete (metadata cleared), `Esc` back from every screen; Arabic RTL list renders.
6. **P5:** edit → `⟳ 1 to sync`; mock sync → `✓ synced`.
7. **Regression:** interrupt a save → crash-recovery still works (`.tmp`/`.recovery` untouched).

## Risks

- Cursor key codes reaching `Menu_keyboard` — confirm in P4 (reuse editor's keypad constants).
- FS directory enumeration support — confirm/extend in P4.
- Arabic in filenames — display keeps Unicode; disk/sync name sanitized ASCII (strip).
- `config.json` growth with many files — keys are tiny; watch save-path size.
- Sync is single-file today — `unsynced`/rename assume that.
