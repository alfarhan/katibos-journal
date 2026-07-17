#pragma once

#include <Arduino.h>

// Standard Arabic keyboard layout ("Arabic 101", the Windows/PC layout, KBDA1).
// Maps a USB HID keycode to an Arabic Unicode code point (> 255), or 0 for keys
// it does not handle. Differs from the macOS layout (ar.cpp) mainly in the shift
// layer (harakat, hamza forms, punctuation) and a few letter keys. AltGr carries
// the same bidi/joining controls as the macOS layout.
int keyboard_keycode_ascii_ar_win(uint8_t keycode, bool shift, bool alt, bool pressed);

// Windows Arabic has dedicated lam-alef keys that expand to TWO code points
// (e.g. ل + ا). The single-code-point return of keyboard_keycode_ascii_ar_win
// can't carry that, so callers ask here first. Returns true and fills c1/c2 when
// (keycode, shift) is a ligature key; false otherwise.
bool keyboard_ar_win_ligature(uint8_t keycode, bool shift, int *c1, int *c2);
