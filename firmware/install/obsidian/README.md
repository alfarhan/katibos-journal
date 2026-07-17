# MicroJournal Sync (Obsidian plugin)

Two-way sync between a remote MicroJournal store (where the device uploads via
Ctrl+U) and a vault folder. Remote `.txt` files appear as `.md` notes; edits in
Obsidian push back to the same remote file.

- **Two backends** — pick **Google Drive** (Apps Script Web App) or **GitHub**
  (a repo, e.g. `mj`) in settings, matching whichever the device syncs to.
- **One file per note** — remote `Title-0.txt` ↔ vault `MicroJournal/Title-0.md`.
  Identity is the **remote id** — the Drive file ID, or the repo path on GitHub
  (which moves on rename; the plugin re-keys its state so no duplicate is made).
- **Manual sync** — ribbon button or command **"Sync MicroJournal now"**.
- **Conflicts keep both** — if a note was edited on *both* sides since the last
  sync, neither edit is lost: the local note stays as-is and the remote version
  is saved beside it as `<name> (conflict).md` for you to merge. (If the two
  edits happen to be identical, no copy is made.) A per-note state cache means
  unchanged files are skipped, so normal syncs only move what changed.
- **Renames sync both ways** — rename in Drive → the vault note is renamed to
  match; rename in Obsidian → the Drive file is renamed. No duplicate is made.
  (Obsidian renames are tracked via a `vault.on("rename")` listener, so rename
  the note *while the plugin is loaded*. A rename **plus** an edit done entirely
  offline can still fall back to delete+create.)
- **Deletes propagate** — delete on one side → the other follows. Drive moves to
  trash and the vault to system trash (recoverable). **GitHub has no trash**, so
  a delete is a real commit removing the file — still recoverable from the repo's
  history, but not from a trash bin. Edit-beats-delete: if the other side was
  edited since the last sync, the file is re-created instead of removed.
- **GitHub renames keep history** — a vault rename pushes a single commit that
  git detects as a rename (`status=renamed`), so the file's history stays
  continuous. A rename done *on the remote/device side* is seen by the plugin as
  delete-old + add-new and is re-pulled under the new name (content preserved,
  but vault links to the old name break) — same limitation as the firmware.

## Setup

### Provider

Choose **Google Drive** or **GitHub** in the plugin settings; switching providers
**resets the sync state** (Drive IDs and git paths are different id namespaces).

1. **Apps Script** — paste the updated [`../google/sync.js`](../google/sync.js)
   into your Web App project and **redeploy** (it adds `doGet` list/get + token).
   Set `_TOKEN` to a long random string if you want the endpoint locked.
2. **Device** (only if you set a token) — in the device `config.json`, change
   `sync.url` to `…/exec?token=YOUR_SECRET`. No firmware change needed; the
   device appends `&name=` automatically.
3. **Obsidian** — enable *MicroJournal Sync* in Settings → Community plugins,
   pick the **Sync provider**, then fill in:
   - *Drive:* **Web App URL** (the `…/exec` URL, no `?token`) + **Token** (same
     `_TOKEN` as the script; empty if open).
   - *GitHub:* **Owner**, **Repository** (e.g. `mj`), **Branch** (`main`),
     **Path** (sub-folder, empty for root), and a **Token** — a fine-grained PAT
     with *Contents: Read and write* on that repo.
   - **Vault folder** — default `MicroJournal`.
4. **Test connection**, then run **"Sync MicroJournal now"**.

> The GitHub token is stored locally in the plugin's `data.json` (not committed).
> Steps 1–2 (Apps Script) are only needed for the Drive provider.

## Develop

```
npm install
npm run dev     # watch + rebuild main.js
npm run build   # type-check + production bundle
```

The plugin folder is symlinked into the vault at
`<vault>/.obsidian/plugins/microjournal-sync`.
