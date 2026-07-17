// Host stubs for firmware symbols that depend on hardware-only subsystems with
// no emulator equivalent. The WiFi / Sync / Drive-mode / BLE-keyboard screens
// are now compiled from the REAL firmware sources (hardware faked beneath them);
// what remains here is genuinely hardware-only: OTA firmware update and the
// physical keypad matrix scan (SDL drives input through keyboard_HID2Ascii).
#include <Arduino.h>
#include "keyboard/Keypad/68/keypad_68.h"

// ---- OTA firmware update ----
void run_firmare_update(const char *filename) {}

// ---- Keypad matrix (SDL drives input instead) ----
void keyboard_keypad_68_setup() {}
void keyboard_keypad_68_loop() {}
