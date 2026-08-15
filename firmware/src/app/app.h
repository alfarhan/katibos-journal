#pragma once

// app version
#define VERSION "2.0.0709"

// katibOS layer version (menu/UX redesign on top of the base firmware)
#define KATIBOS_VERSION "1.10.0"

// Human-readable hardware target name; overridden per-env in platformio.ini
#ifndef BOARD_NAME
#define BOARD_NAME "katibOS"
#endif

// Official OTA manifest. Used whenever config["update"]["url"] is missing or
// still carries a dead URL, so a stale SD-card config can't break update
// checks. Per-board default is set in platformio.ini (microjournal -> latest.json,
// waveshare -> latest_waveshare.json); this is the fallback if an env omits it.
#ifndef KATIBOS_UPDATE_URL
#define KATIBOS_UPDATE_URL "https://raw.githubusercontent.com/alfarhan/katibos/main/firmware/latest.json"
#endif

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

