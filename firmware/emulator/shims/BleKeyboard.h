#pragma once
// Host stub for the ESP32 BLE Keyboard library. Provides the KEY_* constants
// referenced by keyboard.cpp (their numeric values just need to be consistent
// and nonzero on host) and a no-op BleKeyboard class. BLE is not emulated.
#include <Arduino.h>

#define KEY_LEFT_CTRL 0x80
#define KEY_LEFT_SHIFT 0x81
#define KEY_LEFT_ALT 0x82
#define KEY_LEFT_GUI 0x83
#define KEY_RIGHT_CTRL 0x84
#define KEY_RIGHT_SHIFT 0x85
#define KEY_RIGHT_ALT 0x86
#define KEY_RIGHT_GUI 0x87

#define KEY_UP_ARROW 0xDA
#define KEY_DOWN_ARROW 0xD9
#define KEY_LEFT_ARROW 0xD8
#define KEY_RIGHT_ARROW 0xD7
#define KEY_BACKSPACE 0xB2
#define KEY_TAB 0xB3
#define KEY_RETURN 0xB0
#define KEY_ESC 0xB1
#define KEY_INSERT 0xD1
#define KEY_DELETE 0xD4
#define KEY_PAGE_UP 0xD3
#define KEY_PAGE_DOWN 0xD6
#define KEY_HOME 0xD2
#define KEY_END 0xD5
#define KEY_CAPS_LOCK 0xC1

#define KEY_F1 0xC2
#define KEY_F2 0xC3
#define KEY_F3 0xC4
#define KEY_F4 0xC5
#define KEY_F5 0xC6
#define KEY_F6 0xC7
#define KEY_F7 0xC8
#define KEY_F8 0xC9
#define KEY_F9 0xCA
#define KEY_F10 0xCB
#define KEY_F11 0xCC
#define KEY_F12 0xCD
#define KEY_F23 0xF0
#define KEY_F24 0xFB
#define KEY_PRTSC 0xCE

#define KEY_NUM_0 0xEA
#define KEY_NUM_1 0xE1
#define KEY_NUM_2 0xE2
#define KEY_NUM_3 0xE3
#define KEY_NUM_4 0xE4
#define KEY_NUM_5 0xE5
#define KEY_NUM_6 0xE6
#define KEY_NUM_7 0xE7
#define KEY_NUM_8 0xE8
#define KEY_NUM_9 0xE9
#define KEY_NUM_SLASH 0xDC
#define KEY_NUM_ASTERISK 0xDD
#define KEY_NUM_MINUS 0xDE
#define KEY_NUM_PLUS 0xDF
#define KEY_NUM_ENTER 0xE0
#define KEY_NUM_PERIOD 0xEB

typedef uint8_t MediaKeyReport[2];

class BleKeyboard
{
public:
    BleKeyboard(const char * = "", const char * = "", uint8_t = 100) {}
    void begin() {}
    void end() {}
    bool isConnected() { return false; }
    void setName(const char *) {}
    void setName(String) {}
    size_t press(uint8_t) { return 1; }
    size_t release(uint8_t) { return 1; }
    size_t write(uint8_t) { return 1; }
    void releaseAll() {}
    void setBatteryLevel(uint8_t) {}
    void setDelay(uint32_t) {}
};
