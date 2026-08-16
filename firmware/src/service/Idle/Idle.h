#pragma once

// Idle throttle. After a quiet stretch the main loop stops busy-spinning; any
// key restores it. Nothing sleeps, so there is no wake-source to arrange and no
// state to save or reload.
//
// There were two deeper tiers here once - a light sleep and a deep-sleep "Shut
// down", both waking on the matrix through esp_sleep_enable_ext1_wakeup(). They
// were removed after 1.14.6, on their first run on real hardware: the chip slept
// and no keypress ever brought it back, so the only way out was pulling the
// cable. That is the worst possible failure for a device you are meant to trust
// with your writing, and the tier that failed was also the one with the least to
// give - the panel's low-power mode only ever saved the booster, and the real
// power saving here is the main loop's delay(30). If they come back, they need a
// working serial console first: this was debugged blind because Serial does not
// reach the USB port on this build, and every theory cost a flash and a two
// minute wait to test.
//
// Deliberately does NOT change the CPU frequency. Going below 80MHz on the
// ESP32-S3 takes the PLL down with it, which kills USB-CDC - and with it the
// serial console the device is debugged through.

// Register what to do when entering / leaving the idle state (the panel and its
// drawing live with the driver, so the caller supplies both).
void idle_setup(void (*onEnter)(), void (*onExit)());

// Any user input. Resets the countdown and leaves idle immediately.
void idle_touch();

// Pumped from the render loop.
void idle_loop();

// True while throttled - the main loop yields harder in this state.
bool idle_active();

// Configured timeout in seconds; 0 means never throttle.
int idle_timeout_sec();
