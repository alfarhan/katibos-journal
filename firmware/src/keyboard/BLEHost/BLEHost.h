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
// The chosen keyboard is remembered in app["config"]["ble_addr"/"ble_type"/"ble_name"].

void blehost_setup();
void blehost_loop();

// Begin a timed discovery scan; results land in app["ble"]["devices"].
void blehost_scan_start();
bool blehost_is_scanning();

// Connect to the device at that index of app["ble"]["devices"] and remember it.
void blehost_connect_index(int index);

// Drop the remembered keyboard and disconnect.
void blehost_forget();

bool blehost_is_connected();
