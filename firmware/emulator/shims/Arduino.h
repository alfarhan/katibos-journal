#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <string>
#include <chrono>
#include <thread>

#include "WString.h"
#include "Print.h"

// Arduino flash-string + Printable bits ArduinoJson expects to exist.
class __FlashStringHelper;
class Printable
{
public:
    virtual size_t printTo(Print &p) const = 0;
    virtual ~Printable() {}
};

typedef unsigned int uint;
typedef uint8_t byte;
typedef bool boolean;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define LSBFIRST 0
#define MSBFIRST 1
#define DEC 10
#define HEX 16

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

#define PROGMEM
#define PGM_P const char *
#ifndef F
#define F(x) (x)
#endif
#define PSTR(x) (x)
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define pgm_read_ptr(addr) (*(void *const *)(addr))

#ifndef _min
#define _min(a, b) ((a) < (b) ? (a) : (b))
#define _max(a, b) ((a) > (b) ? (a) : (b))
#endif

// Real templates (not macros) so they coexist with std::min/std::max, which
// ESP32 firmware code also uses. Unqualified min(a,b)/max(a,b) still resolve.
template <typename T, typename U> static inline auto min(T a, U b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template <typename T, typename U> static inline auto max(T a, U b) -> decltype(a > b ? a : b) { return a > b ? a : b; }
#ifndef constrain
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif
#ifndef abs
#define abs(x) ((x) > 0 ? (x) : -(x))
#endif
#define map(x, in_min, in_max, out_min, out_max) \
    (((x) - (in_min)) * ((out_max) - (out_min)) / ((in_max) - (in_min)) + (out_min))

// timing -----------------------------------------------------------------
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield();

// GPIO no-ops ------------------------------------------------------------
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }
inline int analogRead(int) { return 0; }
inline void analogWrite(int, int) {}
inline long random(long max) { return max > 0 ? (rand() % max) : 0; }
inline long random(long min, long max) { return (max > min) ? (min + rand() % (max - min)) : min; }
inline void randomSeed(unsigned long) {}

inline bool isPrintable(int c) { return c >= 0x20 && c < 0x7f; }
inline bool isDigit(int c) { return c >= '0' && c <= '9'; }
inline bool isAlpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline bool isAlphaNumeric(int c) { return isAlpha(c) || isDigit(c); }
inline bool isSpace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
inline bool isWhitespace(int c) { return c == ' ' || c == '\t'; }
inline bool isUpperCase(int c) { return c >= 'A' && c <= 'Z'; }
inline bool isLowerCase(int c) { return c >= 'a' && c <= 'z'; }
inline bool isPunct(int c) { return ispunct(c); }
inline bool isHexadecimalDigit(int c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// ESP stub ---------------------------------------------------------------
struct _ESPClass
{
    void restart() { fflush(stdout); }
    uint32_t getFreeHeap() { return 1000000; }
    uint32_t getFreePsram() { return 4000000; }
    void deepSleep(uint64_t) {}
};
extern _ESPClass ESP;

#include "HardwareSerial.h"

// keyboard.cpp uses the KEY_* HID constants unconditionally (the on-device
// build gets them transitively); expose them on the host build too.
#include "BleKeyboard.h"
