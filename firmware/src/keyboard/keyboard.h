#pragma once

#include <Arduino.h>

void keyboard_setup();
void keyboard_loop();

bool keyboard_capslock();
void keyboard_capslock_toggle();

// Toggle the typing layout in place between Arabic and the last Latin layout
// (US by default), so the user can switch languages mid-document.
void keyboard_toggle_layout();

//
void keyboard_HID2Ascii(uint8_t keycode, uint8_t modifier, bool pressed);