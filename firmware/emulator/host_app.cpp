// Host replacement for src/app/app.cpp. Provides the app singleton (status doc,
// gfs, ready flag) and a lightweight setup/loop that avoids the ESP32-S3
// dual-core / PSRAM / MassStorage / Sync machinery the real app.cpp pulls in.
#include <ArduinoJson.h>
#include "app/app.h"
#include "display/display.h"
#include "host_fs.h"
#include "service/Clock/Clock.h"
#include <cstdlib>
#include <cstring>

static bool g_ready = false;

JsonDocument &status()
{
    static JsonDocument doc;
    return doc;
}

bool app_ready() { return g_ready; }

FileSystem *gfs() { return host_fs_instance(); }

void app_setup()
{
    JsonDocument &app = status();

    // file system
    gfs()->begin();

    // load config.json (real firmware routine) — creates a default if missing
    config_load();

    // keyboard layout: honour EMU_LAYOUT env (e.g. "US" or "AR"), else keep
    // whatever config.json had, else default to US.
    const char *envLayout = getenv("EMU_LAYOUT");
    if (envLayout && *envLayout)
        app["config"]["keyboard_layout"] = envLayout;
    else if (!app["config"]["keyboard_layout"].is<const char *>())
        app["config"]["keyboard_layout"] = "US";

    // defaults the RLCD screens read
    if (!app["config"]["file_index"].is<int>())
        app["config"]["file_index"] = 0;
    app["config"]["wakeup_animation_disabled"] = true; // skip wake animation
    app["config"]["UsbKeyboard"] = false;              // not the BLE keyboard screen

    // EMU_EPOCH: confirm the software clock at a fixed UTC epoch (seconds) so
    // headless dumps can exercise clock-gated UI (date/time header, last-synced
    // ages) without a real NTP/sync round-trip. Emulator-only.
    const char *envEpoch = getenv("EMU_EPOCH");
    if (envEpoch && *envEpoch)
        clock_set_epoch(atol(envEpoch));

    // start on the word processor
    app["screen"] = WORDPROCESSOR;
    app["screen_prev"] = -1;

    g_ready = true;
}

// Fake sync engine (host_fake_sync.cpp) — pumped here so the Sync screen
// progresses over time the way the real background sync task would on-device.
void sync_loop();

void app_loop()
{
    // Drive the fake sync progression; no battery / dual-core tasks on host.
    sync_loop();
}
