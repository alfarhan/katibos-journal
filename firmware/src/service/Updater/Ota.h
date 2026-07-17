#pragma once

#include <ArduinoJson.h>

// Over-the-air update over Wi-Fi. The device fetches a small JSON manifest from
// config["update"]["url"], compares its "version" to KATIBOS_VERSION, and if it
// differs streams the "url" .bin straight into the inactive OTA slot (the 16MB
// partition table has ota_0 + ota_1), then reboots. A failed download leaves the
// running firmware untouched.
//
// Manifest (latest.json) shape:
//   { "version": "1.1", "url": "https://.../firmware_rev_8.bin" }
//
// State lives in app["ota_state"] / app["ota_message"] (+ ota_version, ota_url).
// HOST_EMU builds run the manifest check for real (libcurl) but stub the flash.

#define OTA_IDLE 0
#define OTA_CHECKING 1
#define OTA_UPTODATE 2
#define OTA_AVAILABLE 3
#define OTA_DOWNLOADING 4
#define OTA_DONE 5
#define OTA_ERROR -1

// Connect Wi-Fi (device), fetch the manifest, compare versions. Blocking.
void ota_check();
// Download app["ota_url"] and flash it to the inactive slot. Blocking.
void ota_apply();
// Reboot into the freshly written slot (device only; no-op in the emulator).
void ota_reboot();
