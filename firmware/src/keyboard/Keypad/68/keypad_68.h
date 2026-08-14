#pragma once

//
#include <Adafruit_Keypad.h>

void keyboard_keypad_68_setup();

///
void keyboard_keypad_68_loop();

// 
int keyboard_keypad_68_get_key(keypadEvent e);



// ---- sleep wake support (matrix keypad boards only) -------------------------
// Reconfigure the matrix so that ANY keypress drives one of the row lines HIGH:
// every column is held HIGH as an output and every row becomes an input with a
// pull-DOWN. That shape is deliberate - "any pin HIGH" is the one ext1 deep-sleep
// wake mode every ESP32 variant supports, whereas the natural low-side scan would
// need ANY_LOW, which is not universal.
//
// Only meaningful where the keys are wired to the chip. A BLE keyboard cannot
// wake a radio that is off, and USB-CDC input does not survive a sleep, so these
// compile to nothing on those builds.
void keypad_prepare_wake();

// Hand the pins back to the scanner after a light sleep.
void keypad_resume_scan();

// Row GPIO bitmask, for esp_sleep_enable_ext1_wakeup().
unsigned long long keypad_wake_mask();
