// Host implementations of Arduino timing + the global Serial/SPI/ESP objects.
#include <Arduino.h>
#include <SPI.h>
#include <chrono>
#include <thread>

static std::chrono::steady_clock::time_point _t0 = std::chrono::steady_clock::now();

unsigned long millis()
{
    auto now = std::chrono::steady_clock::now();
    return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(now - _t0).count();
}

unsigned long micros()
{
    auto now = std::chrono::steady_clock::now();
    return (unsigned long)std::chrono::duration_cast<std::chrono::microseconds>(now - _t0).count();
}

void delay(unsigned long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
void delayMicroseconds(unsigned int us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
void yield() {}

HardwareSerial Serial;
HardwareSerial Serial1;
SPIClass SPI;
_ESPClass ESP;
