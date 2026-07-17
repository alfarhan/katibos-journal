#pragma once

#include <ArduinoJson.h>

//
#define WIFI_CONFIG_LIST 0
#define WIFI_CONFIG_EDIT_SSID 1
#define WIFI_CONFIG_EDIT_KEY 2
#define WIFI_CONFIG_ENTRY 3 // chosen-entry screen: edit or forget
#define WIFI_CONFIG_SCAN 4  // scan & pick a nearby network

#define WIFI_PER_PAGE 8 // rows per page (saved list and scan list are unbounded)

//
void WifiEntry_setup();
void WifiEntry_keyboard(char key);

// service library to setup wifi and connect/disconnect to wifi
void wifi_config_load();
void wifi_config_save();

// Scan for nearby networks into app["network"]["scan"] = [{ssid, rssi}], deduped
// and sorted by signal (strongest first); returns the count. Platform seam:
// real WiFi.scanNetworks() on the device, faked in the emulator.
int wifi_scan(JsonDocument &app);