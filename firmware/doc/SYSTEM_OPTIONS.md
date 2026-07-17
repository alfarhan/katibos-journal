# katibOS — System Options / Preferences ideas

Backlog for a future **Preferences** screen. Today's SETTINGS tab is really a
connectivity/launcher menu (Language, Wi-Fi, Sync, BLE, Drive Mode, Help — each
just opens another screen). A real preferences screen would hold device
**behavior toggles**, which don't exist yet.

Tags: **[wired]** = config field already exists, just needs a control ·
**[new]** = new logic · **★** = especially fitting for an Arabic writing device.

## Writing & editor
- Font size S/M/L — **[wired: `editor_font_size`]**
- Status bar shown by default (Ctrl+H is per-session today)
- Focus / typewriter mode (keep the caret line centered)
- Line spacing
- Boot to last doc vs. the file list

## Display & appearance
- Brightness — **[wired: `brightness`]**
- Theme / invert (light ↔ dark) — **[wired: `foreground_color` / `background_color`]**
- Wake-up animation on/off — **[wired: `wakeup_animation_disabled`]**
- Full vs. partial refresh (e-ink ghosting trade-off)

## Power
- Auto-sleep timeout (N minutes)
- Auto power-off
- Front-panel button enable/disable — **[wired: `front_panel_button_disabled`]**

## Time & locale ★
- Time zone offset — currently **hardcoded to KSA**; make it a setting — **[new]**
- 12 / 24-hour clock
- **Date style for Ctrl+D: Gregorian / Hijri / ISO** ★
- Set clock manually (for when there's no Wi-Fi / NTP)

## Writing goals
- Daily word goal value — **[wired: `daily_goal`]**
- Goal on/off; reset streak

## Sync
- Auto-sync (on idle / on save) on/off — pairs with manual Ctrl+U / Ctrl+Shift+U
- Sync URL + token entry on-device (config-file-only today)

## System / maintenance
- About (firmware version, free storage, uptime)
- Device name (Wi-Fi hostname is fixed `MICROJOURNAL`)
- **Factory reset / clear all** — with a hard confirm + "sync first" warning (like the delete flow)
- Firmware update (an Update screen already exists)

## Localization ★ (big, most on-brand)
- Arabic UI: RTL menus + Arabic labels. Largest effort here, but it makes
  katibOS truly Arabic-first.

## Recommended order
1. **Quick wins (mostly [wired], fast):** font size, theme/invert, brightness,
   daily-goal value, auto-sleep.
2. **On-brand standouts:** Hijri date option for Ctrl+D, configurable time zone.
3. **Dream / large:** Arabic UI (separate project).

Status: brainstorm only — none built yet.
