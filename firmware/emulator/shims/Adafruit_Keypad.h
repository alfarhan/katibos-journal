#pragma once
// Stub of Adafruit_Keypad sufficient to compile Keypad_68.cpp. The matrix is
// never scanned on host (tick/available do nothing); SDL drives input instead.
#include <Arduino.h>

#define KEY_JUST_PRESSED 1
#define KEY_JUST_RELEASED 2
#define KEY_DEBOUNCE_INTERVAL 10

typedef struct
{
    union
    {
        struct
        {
            uint8_t KEY;
            uint8_t ROW;
            uint8_t COL;
            uint8_t EVENT;
        } bit;
        uint32_t reg;
    };
} keypadEvent;

#define makeKeymap(x) ((char *)(x))

class Adafruit_Keypad
{
public:
    Adafruit_Keypad(char *, byte *, byte *, int, int) {}
    void begin() {}
    void tick() {}
    bool available() { return false; }
    keypadEvent read()
    {
        keypadEvent e;
        e.reg = 0;
        return e;
    }
};
