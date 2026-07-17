#pragma once
#include <cstdint>
#include <cstddef>

class SPISettings
{
public:
    SPISettings() {}
    SPISettings(uint32_t, uint8_t, uint8_t) {}
};

class SPIClass
{
public:
    void begin(int = -1, int = -1, int = -1, int = -1) {}
    void end() {}
    void setFrequency(uint32_t) {}
    void beginTransaction(SPISettings) {}
    void endTransaction() {}
    uint8_t transfer(uint8_t d) { return d; }
    uint16_t transfer16(uint16_t d) { return d; }
    void transfer(void *, size_t) {}
    void write(uint8_t) {}
    void write16(uint16_t) {}
    void writeBytes(const uint8_t *, size_t) {}
    void setBitOrder(uint8_t) {}
    void setDataMode(uint8_t) {}
    void setClockDivider(uint8_t) {}
};

extern SPIClass SPI;
