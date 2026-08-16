#pragma once

// Idle throttle. After a quiet stretch the main loop stops busy-spinning; any
// key restores it. Nothing sleeps at this stage, so there is no wake-source to
// arrange and no state to save or reload.
//
// Deliberately does NOT change the CPU frequency. Going below 80MHz on the
// ESP32-S3 takes the PLL down with it, which kills USB-CDC - and with it the
// serial console the device is debugged through.

// Register what to do when entering / leaving the idle state, and what to draw
// just before the chip sleeps (the panel and its drawing live with the driver, so
// the caller supplies all three). onSleep is passed true for a shut down, false
// for a nap, so the two can say different things.
void idle_setup(void (*onEnter)(), void (*onExit)(), void (*onSleep)(bool deep) = nullptr);

// Any user input. Resets the countdown and leaves idle immediately.
void idle_touch();

// Pumped from the render loop.
void idle_loop();

// True while throttled - the main loop yields harder in this state.
bool idle_active();

// Configured timeout in seconds; 0 means never throttle.
int idle_timeout_sec();

// ---- sleep (Tier 2 / Tier 3) -----------------------------------------------
// Both are OFF by default, and both need the matrix wired to the chip so a
// keypress can wake it. Where it isn't (the host emulator) these report 0 and
// nothing ever sleeps.
//
//  light  esp_light_sleep_start(). RAM is retained, so it resumes mid-loop with
//         the document, window and caret exactly as they were.
//  deep   esp_deep_sleep_start(). RAM is gone and it restarts from setup(), so
//         the file is saved first; boot reopens it at the stored caret, which the
//         editor already does on its own (caret_N in config).
int  sleep_light_sec(); // 0 = never
int  sleep_deep_sec();  // 0 = never
bool sleep_supported(); // false when no key can wake this build
