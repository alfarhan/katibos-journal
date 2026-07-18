#pragma once

// app version
#define VERSION "2.0.0709"

// katibOS layer version (menu/UX redesign on top of the base firmware)
#define KATIBOS_VERSION "1.3.2"

// Official OTA manifest. Used whenever config["update"]["url"] is missing or
// still carries the pre-rename URL that now 404s, so a stale SD-card config
// can't break update checks.
#define KATIBOS_UPDATE_URL "https://raw.githubusercontent.com/alfarhan/katibos/main/firmware/latest.json"

// default utility headers
#include <ArduinoJson.h>
#include "Log/Log.h"
#include "FileSystem/FileSystem.h"
#include "Config/Config.h"
#include "Verification/Verification.h"
#include "service/Tools/Tools.h"
#include <HardwareSerial.h>

#ifdef BOARD_ESP32_S3
// CPU runs at the low frequency to save battery
// switch to full speed during WiFi sync and USB mass storage sessions
// WiFi requires at least 80 MHz
#define CPU_FREQUENCY_LOW 80
#define CPU_FREQUENCY_FULL 240
#endif

//
void app_setup();
void app_loop();

// is app ready?
bool app_ready();

// app status
JsonDocument &status();

// ESP32 has SD or SPIFFS 
// RP2040 has LittleFS
// This is a pattern to hide the implementation of the file system
// and provide a common interface to access the file system
FileSystem *gfs();

