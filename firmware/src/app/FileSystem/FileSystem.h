#pragma once

//
#include <Arduino.h>
#include <FS.h>

#ifdef BOARD_ESP32_S3
#include <SD.h>
#endif

class FileSystem {
public:
    virtual bool begin() = 0;
    virtual void end() {};
    virtual File open(const char* path, const char* mode) = 0;
    virtual bool exists(const char* path) = 0;
    virtual bool remove(const char* path) = 0;
    virtual bool rename(const char* pathFrom, const char* pathTo) = 0;
    // Partition size / usage, for the Storage screen. Backends that can't report
    // it return 0 and the screen omits the gauge.
    virtual size_t totalBytes() { return 0; }
    virtual size_t usedBytes() { return 0; }
    virtual ~FileSystem() = default;
};