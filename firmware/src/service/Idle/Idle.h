#pragma once

// Idle throttle. After a quiet stretch the panel drops to its Low Power refresh
// mode and the main loop stops busy-spinning; any key restores both. Nothing
// sleeps, so there is no wake-source to arrange and no state to save or reload -
// which is what makes this safe on BOTH boards. (Real light/deep sleep is only
// practical on the matrix-keypad board: a BLE keyboard cannot wake a chip whose
// radio is off, and USB-CDC input does not survive a sleep either.)
//
// Deliberately does NOT change the CPU frequency. Going below 80MHz on the
// ESP32-S3 takes the PLL down with it, which kills USB-CDC - and on the
// Waveshare that is both the serial console AND the keyboard, so the device
// would idle into a state where typing can't wake it.

// Register what to do when entering / leaving the idle state (the panel mode
// switch lives with the driver, so the caller supplies it).
void idle_setup(void (*onEnter)(), void (*onExit)());

// Any user input. Resets the countdown and leaves idle immediately.
void idle_touch();

// Pumped from the render loop.
void idle_loop();

// True while throttled - the main loop yields harder in this state.
bool idle_active();

// Configured timeout in seconds; 0 means never throttle.
int idle_timeout_sec();

// ---- sleep (Tier 2 / Tier 3) -----------------------------------------------
// Both are OFF by default and both are only offered on a board whose keys are
// wired to the chip: a BLE keyboard cannot wake a radio that is off, and USB-CDC
// input does not survive a sleep. On any other board these report 0 and nothing
// ever sleeps.
//
//  light  esp_light_sleep_start(). RAM is retained, so it resumes mid-loop with
//         the document, window and caret exactly as they were.
//  deep   esp_deep_sleep_start(). RAM is gone and it restarts from setup(), so
//         the file is saved first; boot reopens it at the stored caret, which the
//         editor already does on its own (caret_N in config).
int  sleep_light_sec(); // 0 = never
int  sleep_deep_sec();  // 0 = never
bool sleep_supported(); // false when no key can wake this board
