#pragma once
#include <cstdio>
#include "Print.h"

class HardwareSerial : public Print
{
public:
    void begin(unsigned long = 0) {}
    void begin(unsigned long, int) {}
    void end() {}
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }
    void flush() {}
    operator bool() const { return true; }

    size_t write(uint8_t c) override { return fputc(c, stdout) == EOF ? 0 : 1; }
    using Print::write;
};

extern HardwareSerial Serial;
extern HardwareSerial Serial1;
