#ifndef keyboard_locale_h
#define keyboard_locale_h

#include <Arduino.h>

// Returns the character for a keycode under the given locale. Widened to int
// so non-Latin locales (e.g. Arabic) can return Unicode code points > 255.
int keyboard_keycode_ascii(String locale, uint8_t keycode, bool shift, bool alt, bool pressed);

// True for any Arabic layout code ("AR" macOS, "ARW" Windows). Used so RTL
// defaults and the language toggle treat both variants as Arabic.
bool keyboard_locale_is_arabic(const String &locale);

// Some layouts have keys that expand to two code points (Windows Arabic lam-alef).
// Returns true and fills c1/c2 when (locale, keycode, shift) is such a key.
bool keyboard_locale_ligature(const String &locale, uint8_t keycode, bool shift, int *c1, int *c2);

//
uint8_t keyboard_precursor_filter(uint8_t ascii);
uint8_t keyboard_caplock_filter(uint8_t ascii);

// translate internation key inputs ` + a = à 
uint8_t keyboard_international(uint8_t precursor, uint8_t ascii);

#endif
