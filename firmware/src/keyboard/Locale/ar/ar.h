#pragma once

#include <Arduino.h>

// Arabic keyboard layout. Maps a USB HID keycode to an Arabic Unicode code
// point (> 255), following the macOS Arabic layout. Returns the code point, or
// 0 for keys it does not handle. The shift layer carries harakat (diacritics),
// hamza forms and Arabic punctuation; AltGr carries bidi/joining controls.
int keyboard_keycode_ascii_ar(uint8_t keycode, bool shift, bool alt, bool pressed);
