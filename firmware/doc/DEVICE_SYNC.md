# Device ⇆ Google Drive two-way sync — design

Goal: make the **device ⇆ Drive** sync two-way and rename/delete-aware, the way
the Obsidian plugin already is for **Obsidian ⇆ Drive**. Drive is the hub:

```
   device  ⇆  Google Drive (MicroJournal folder)  ⇆  Obsidian vault
```

## Current state (push-only)
- `src/service/Sync/Sync.cpp` uploads **every** local slot on Ctrl+U as
  `"<title>-<index>.txt"` (POST, base64 body), overwriting by **name**. It never
  reads from Drive.
- Identity is the **name**, not a stable id. So a rename done in Drive/Obsidian
  is invisible to the device — next push recreates the file under the device's
  title ⇒ **duplicate** (we hit this: `hi all` vs `hi my all`).
- Per-slot flags already live in `config.json` (`title_N`, `unsynced_N`,
  `edited_N`, `synced_day_N`, `edit_seq`, `caret_N`, word counts).

## What the server already provides (built for the plugin, Version 6)
`install/google/sync.js` already exposes everything a two-way client needs:
- `POST ?name=` (base64 body) → create/overwrite by name, returns `{id,name,modified}`
- `GET ?action=list` → `[{id,name,modified,size}]` (skips Google-native files)
- `GET ?action=get&id=` → base64 content
- `GET ?action=rename&id=&name=`
- `GET ?action=trash&id=` (recoverable)
- Token required on every request; deployment access = **Anyone**.

So this project is **firmware only** (plus one test-harness change). No new
server work for Phase 1.

## Blockers to fix first
1. **Token in device config.** `config.json` `sync.url` currently has no
   `?token=`. The endpoint now requires it, so on real hardware Ctrl+U would
   fail `unauthorized`. Fix = set `sync.url` to `…/exec?token=<secret>`. No
   firmware change ([Sync.cpp:336](../src/service/Sync/Sync.cpp) appends
   `&name=` when a `?` already exists).
2. **The emulator fakes sync.** `emulator/host_fake_sync.cpp` stubs the whole
   engine; the real `Sync.cpp` (HTTPClient/WiFi) is excluded from the emulator
   build. Two-way logic **cannot be tested** until the emulator can do real
   HTTP. This is **Phase 0** below.

## Identity & state model
Per slot `N`, store in `config.json`:
- `drive_id_N` — the Drive file id (stable across renames). The anchor.
- `synced_hash_N` — content hash at last successful sync (change detection).
- `synced_modified_N` — Drive `modified` at last sync (remote-change detection).
- (existing `title_N`, `unsynced_N`, `synced_day_N` stay.)

Change detection avoids the unreliable device clock: **local changed** = current
file hash ≠ `synced_hash_N` (and/or `edit_seq` bump); **remote changed** = Drive
`modified` ≠ `synced_modified_N`. Wall-clock only as a last-resort tiebreak.

## The shaping decision — finite slots
Unlike the vault/Drive (unbounded), the device has a **fixed, small set of
numbered slots**. Drive can hold more notes than the device has room for (you
also create notes in Obsidian). So "pull every new Drive file" can't always fit.

**Recommended default (confirm):** the device syncs **only the notes it already
has a slot for** (matched by `drive_id_N`). New files that appear *only* on Drive
are **not** auto-pulled into the device unless there is a free slot AND a setting
opts in. Rationale: the device is a focused writing tool, not a Drive mirror;
silently filling slots from Drive is surprising and risks evicting local work.
New-Drive-file handling is a Phase 3 opt-in, not a default.

## Conflict policy (matches the plugin)
Edit-on-both-sides since last sync ⇒ **keep both**, never silent overwrite. On
the device that means writing the incoming remote version into a **free slot** as
a conflict copy and leaving the local slot untouched. If no free slot, **skip +
flag** (`sync_error`) rather than overwrite. (Phase 3.)

## Data safety (hard rules — journal is real writing)
- Download to a **temp file**, fsync, then swap into `N.txt` only on full
  success. A dropped WiFi mid-download must never truncate a real note.
- Never delete a slot without the existing `*_backup.txt` mechanism.
- Deletes propagate to **trash** (recoverable), both directions.
- Every phase: build **both** `emulator/Makefile` and `pio run -e rev_8`
  (separate build systems — see CLAUDE/emulator-testing notes), prove in the
  emulator, then STOP for manual testing before the next phase.

## Phased plan
**P0 — real-HTTP test harness — DONE (2026-06-26).** Transport seam in place:
`SyncCore.{h,cpp}` (shared logic), device transport in `Sync.cpp` (HTTPClient),
host transport `emulator/host_sync_http.cpp` (libcurl), `emulator/host_real_sync.cpp`
drives it (replaces the old `host_fake_sync.cpp`). Token added to emulator
`sdcard/config.json`. Behavior unchanged (still push-all) — this only moved the
HTTP behind the seam so the real logic runs in the emulator. Both builds pass
(`make`; `pio run -e rev_8`).

**P1 — IDs + id-targeted push — DONE (2026-06-26).** Fixes the duplicate bug.
- `doPost` now accepts `?id=`: updates that exact file by id (setContent + rename
  to `?name=` if changed); stale/trashed id falls through to by-name create.
- Device stores `drive_id_N` (captured from every POST response) and sends
  `?id=drive_id_N&name=<current>` once known — so a rename done in Drive/Obsidian
  updates the SAME file instead of spawning a duplicate. One request, no name
  ambiguity. Still push-only; device content wins on push.
- Needs the updated `sync.js` redeployed. Both builds pass.

**P2 — pull (two-way) — DONE (2026-06-26).** `SyncCore` is now a stepwise
reconcile engine (`sync_begin`/`sync_step`, host steps one unit/frame; device
runs all via `sync_reconcile`). Per tracked slot: adopt a Drive/Obsidian
**rename** into `title_N` (fixes the revert); **pull** edited content when only
the remote changed (atomic temp→rename); **push** when only local changed;
**both changed → conflict**, left untouched (counted, P3 = keep-both). New
content state per slot: `synced_hash_N`, `synced_modified_N` (no-baseline slots
from P1 get a baseline push on first P2 sync). **New Drive-only files pull into
the lowest free slot** (device-full → flag). Adds base64 *decode* + file hash.
Safety: the **currently-open** slot only adopts a rename — its content is not
clobbered mid-edit (pulled later when not open). Remote deletes are NOT yet
propagated (a Drive-deleted note is re-pushed) — that's P3.

**P3 — full reconcile — DONE (2026-06-26).**
- **Keep-both conflicts:** both-sides-edited → the remote version is pulled into
  a free slot as "<title> (conflict)" (its own Drive file next sync), local stays
  canonical and is pushed. No silent overwrite.
- **Delete both ways (recoverable):** device delete (Clear) leaves a `sync_trash`
  tombstone (the Drive id) + clears the slot mapping → next sync trashes the Drive
  copy. A Drive-deleted file (mapped id absent from a FRESH listing) is removed on
  the device via a `_backup.txt` swap. Edit-beats-delete both directions.
- **Critical guard:** deletes are only honored when a listing was actually
  fetched (`g_haveRemote`) — a failed list never triggers mass deletion.
- The open slot defers content-pull and delete (rename still adopts).

All phases (P0–P3) complete; both builds pass. Remaining caution: device-side
two-way is **emulator-verified only** until hardware exists (device `config.json`
needs `?token=`).

## Decisions (locked 2026-06-26)
1. **Test harness (P0): real libcurl HTTP** in the emulator, hitting the live
   endpoint. Run the *real* shared sync logic, not a fake.
2. **New Drive-only notes: pull into free slots.** The device DOES download
   notes that exist only on Drive (e.g. created in Obsidian) into any free slot.
   When no slot is free, skip + flag (don't evict local work). So this overrides
   the earlier "don't auto-pull" lean — pulling new files is in scope.
3. **Trigger: manual Ctrl+U only.** No background/scheduled sync.

## Architecture seam (enables P0 testing)
Split the engine so the *logic* is shared and only the *transport* is per-platform:
- `SyncCore.*` (shared, compiled on device AND emulator) — file enumeration,
  reconcile, upload/download, state in `config.json`. Calls the transport seam +
  `gfs()`.
- Transport seam `sync_http_get(url)` / `sync_http_post(url, filePath)`:
  - device impl → HTTPClient (in `Sync.cpp`)
  - host impl → **libcurl** (`emulator/host_sync_http.cpp`)
- Device `Sync.cpp` keeps WiFi scan/connect, then calls the shared reconcile.
  Emulator host skips WiFi (already online) and calls the same reconcile.

## Scenario matrix (rev8 ⇆ Drive ⇆ Obsidian)

Drive is the hub; rev8 (`Ctrl+U`) and Obsidian (`Sync`) are independent **manual**
clients that reconcile against Drive by **file-id**. A change reaches all three
after the source side syncs *and* the other side syncs.

### Single-side actions

| # | Action | On | rev8 `Ctrl+U` does | Obsidian `Sync` does | End state |
|---|---|---|---|---|---|
| 1 | Create note | rev8 | push new → Drive (captures id) | pull new → vault `.md` | all 3 in sync |
| 2 | Create note | Obsidian | pull new → free slot | push new → Drive | all 3 in sync |
| 3 | Add `.txt` | Drive | pull → free slot | pull → vault | all 3 in sync |
| 4 | Edit | rev8 | push → Drive | pull → vault | all 3 in sync |
| 5 | Edit | Obsidian | pull → slot (if not the open file) | push → Drive | all 3; open file pulls after you switch away |
| 6 | Edit | Drive | pull → slot | pull → vault | all 3 in sync |
| 7 | Rename | rev8 | renames same Drive file (id) | renames vault note | no duplicate |
| 8 | Rename | Obsidian | adopts new title | renames Drive file | no dup, no revert |
| 9 | Rename | Drive | adopts new title | renames vault note | no dup |
| 10 | Delete | rev8 | tombstone → trashes Drive file | removes vault note (→ trash) | recoverable: device `_backup.txt`, Drive trash, OS trash |
| 11 | Delete | Obsidian | deletes slot (→ `_backup.txt`) | trashes Drive file | recoverable |
| 12 | Delete | Drive | deletes slot (→ `_backup.txt`) | removes vault note (→ trash) | recoverable |

### Conflicts & edge cases

| Situation | Outcome |
|---|---|
| Same note edited on rev8 AND Obsidian before either syncs | Keep-both — later-syncing side keeps its copy + writes `<title> (conflict)`. Nothing lost. |
| Edited on one side, deleted on the other | Edit wins — recreated on the deleting side (both directions). |
| Renamed differently on two sides | Drive (remote) wins on each client; converges to one name. |
| Note open on rev8 while edited elsewhere | rev8 adopts the rename now, defers content pull until you leave that file. |
| New Drive note but rev8 has no free slot | Skipped + "Device full" flag; pulls when a slot frees. |
| Sync list fetch fails (WiFi drop) | No deletes honored — safe, never wipes. |
| Untitled note on rev8 | Pushed under a timestamp name; tracked by id after first sync. |
| Rename AND edit done with Obsidian closed | Obsidian side may create a duplicate (rename tracking is live-only) — known limit. |
| Empty file on rev8 | Never uploaded (skipped). |

Both clients are manual; every destructive op lands in a recoverable bin (never a hard delete).
