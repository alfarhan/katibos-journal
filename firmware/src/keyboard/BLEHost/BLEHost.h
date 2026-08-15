#pragma once

// BLE keyboard HOST (central): connect a Bluetooth-LE keyboard to this device as
// an input, feeding its HID reports into the shared keyboard_HID2Ascii() pipeline
// (same path the USB-host boards use), so all locale/Arabic/shortcut handling is
// reused unchanged.
//
// Enabled with -D USE_BLE_KEYBOARD_HOST. When the flag is off every function
// below is a no-op stub, so the UI (BLUETOOTH menu) still links on the emulator.
//
// UI-facing state is published into the shared app JSON so the menu can render it:
//   app["ble"]["devices"]   array of {name, addr} found while scanning
//   app["ble"]["scanning"]  bool
//   app["ble"]["connected"] bool
//   app["ble"]["peer"]      connected/remembered keyboard label
//   app["ble"]["status"]    short human status line
//   app["ble"]["saved"]     array of {name, addr} - the keyboards we know
// Up to three keyboards are remembered in app["config"]["ble_saved"], any of
// which reconnects on sight; ble_addr/ble_type/ble_name track the most recent
// one, which is also what an older build reads. Three is NimBLE's own bond
// limit, so a fourth would silently evict a key we still list.

void blehost_setup();
void blehost_loop();

// Begin a timed discovery scan; results land in app["ble"]["devices"].
void blehost_scan_start();
bool blehost_is_scanning();

// Connect to the device at that index of app["ble"]["devices"] and remember it.
void blehost_connect_index(int index);

// Connect to a known address (a row of app["ble"]["saved"], say).
void blehost_connect_addr(const char *addr, int type, const char *name);

// Forget one keyboard: our note of it, its pairing key, and the link if current.
void blehost_forget_addr(const char *addr, int type);

// Drop the most recent keyboard and disconnect.
void blehost_forget();

bool blehost_is_connected();
