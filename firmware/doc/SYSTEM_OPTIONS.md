# katibOS — System Options / Preferences ideas

Backlog for a future **Preferences** screen. Today's SETTINGS tab is really a
connectivity/launcher menu (Language, Wi-Fi, Sync, BLE, Drive Mode, Help — each
just opens another screen). A real preferences screen would hold device
**behavior toggles**, which don't exist yet.

Tags: **[wired]** = config field already exists, just needs a control ·
**[new]** = new logic · **★** = especially fitting for an Arabic writing device.

> **Every "[wired]" tag below is stale — treat them all as [new].** Checked
> 2026-08-16: `editor_font_size`, `brightness`, `foreground_color`,
> `background_color`, `wakeup_animation_disabled`, `front_panel_button_disabled`,
> `daily_goal` and `device_name` have **zero references in `src/`**. The
> clock/date, daily-goal, wake-up-animation and device-name features were removed
> from the firmware (034dc3f) and the config fields went with them, so nothing
> here is "a control away" — building any of it means writing it from scratch.
> A Preferences screen now exists (`Menu/Preferences/`) with theme, font, Arabic
> font, line spacing, text flow, status bar, Power save and Screensaver.

## Writing & editor
- Font size S/M/L — **[wired: `editor_font_size`]**
- ~~Status bar shown by default~~ — **built**: Preferences → Status bar, and Ctrl+H writes the same `statusbar_hidden` key (one setting, persisted)
- Focus / typewriter mode (keep the caret line centered)
- Line spacing
- Boot to last doc vs. the file list

## Display & appearance
- Brightness — **[wired: `brightness`]**
- Theme / invert (light ↔ dark) — **[wired: `foreground_color` / `background_color`]**
- Wake-up animation on/off — **[wired: `wakeup_animation_disabled`]**
- Full vs. partial refresh (e-ink ghosting trade-off)

## Power
- Auto-sleep timeout (N minutes) — **tried and withdrawn.** Shipped as Options →
  Sleep / Shut down, removed in the build after 1.14.6: on rev_8 the chip slept and no keypress
  ever woke it. See the sleep note in `CLAUDE.md` before proposing it again.
- Auto power-off — same wake problem, same prerequisite.
- Front-panel button enable/disable — **[wired: `front_panel_button_disabled`]**

## Time & locale ★
- Time zone offset — currently **hardcoded to KSA**; make it a setting — **[new]**
- 12 / 24-hour clock
- **Date insert + style: Gregorian / Hijri / ISO** ★ — needs a chord; the old Ctrl+D emitted an action code nothing handled and was removed in 1.5.0
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
2. **On-brand standouts:** Hijri date insert, configurable time zone.
3. **Dream / large:** Arabic UI (separate project).

Status: brainstorm only — none built yet.
