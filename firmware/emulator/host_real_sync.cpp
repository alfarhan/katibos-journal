// Real host sync driver: runs the shared SyncCore two-way engine (over the
// libcurl transport in host_sync_http.cpp). Skips WiFi (the Mac is online).
//
// Steps ONE unit of work per frame: each libcurl call blocks and the host
// render loop only repaints between frames, so stepping keeps the progress
// message updating instead of the window appearing frozen.
#include <ArduinoJson.h>
#include <Arduino.h>
#include "app/app.h"
#include "app/Config/Config.h"
#include "service/Sync/Sync.h"
#include "service/Sync/SyncCore.h"
#include "service/Tools/Tools.h"
#include "service/Clock/Clock.h"
#include <ctime>

static bool g_running = false;

// Fake scan results (the Mac build has no radio) so the scan/pick UI is testable.
int wifi_scan(JsonDocument &app)
{
    static const char *names[] = {"Home-WiFi", "CoffeeShop", "Office-5G", "Neighbour"};
    static const int rssis[] = {-43, -58, -71, -84};
    JsonArray arr = app["network"]["scan"].to<JsonArray>();
    for (int i = 0; i < 4; i++)
    {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = names[i];
        o["rssi"] = rssis[i];
    }
    return 4;
}

void sync_init()
{
    JsonDocument &app = status();
    app["sync_state"] = SYNC_START;
    app["sync_message"] = "";
    app["sync_error"] = "";
    g_running = false;
}

void sync_start_request()
{
    JsonDocument &app = status();
    if (app["task"].as<String>() != "sync_start")
        app["task"] = "sync_start";
}

static void finish(JsonDocument &app)
{
    g_running = false;
    config_save();
    int ok = 0, total = 0, conflicts = 0;
    sync_result(ok, total, conflicts);
    const char *dest = sync_provider_is_git(app) ? "GitHub" : "Google Drive";
    String msg = format("Synced %d/%d to %s", ok, total, dest);
    if (conflicts > 0)
        msg += format(", %d conflict(s)", conflicts);
    app["sync_message"] = msg;
    app["sync_state"] = SYNC_COMPLETED;
    app["clear"] = true;
}

void sync_loop()
{
    JsonDocument &app = status();

    if (!g_running)
    {
        if (app["task"].as<String>() != "sync_start")
            return;
        app["task"] = "";

        String url;
        if (sync_provider_is_git(app))
        {
            String err;
            if (!sync_git_config_valid(app, err))
            {
                app["sync_error"] = err;
                app["sync_state"] = SYNC_ERROR;
                app["clear"] = true;
                return;
            }
        }
        else
        {
            url = app["config"]["sync"]["url"].as<String>();
            if (url.isEmpty() || url == "null")
            {
                app["sync_error"] = "SYNC URL NOT FOUND";
                app["sync_state"] = SYNC_ERROR;
                app["clear"] = true;
                return;
            }
        }

        app["sync_state"] = SYNC_STARTED;
        app["sync_message"] = "Syncing...";
        app["clear"] = true;
        // Mirror the device: it learns the real time on connect (NTP / Date
        // header). The Mac is already online, so use its wall clock.
        clock_set_epoch((long)time(nullptr));
        if (app["sync_scope"].as<String>() == "one")
            sync_begin_one(app, url, app["sync_one"].as<int>());
        else
            sync_begin(app, url);
        g_running = true;
        return;
    }

    if (sync_step(app))
        return; // more work; let this frame render
    finish(app);
}

// Device-only symbols (declared in Sync.h) — defined for link completeness.
void sync_start()
{
    JsonDocument &app = status();
    String url = app["config"]["sync"]["url"].as<String>();
    int ok = 0, total = 0;
    sync_reconcile(app, url, ok, total);
}
bool sync_connect_wifi(JsonDocument &, const char *, const char *) { return true; }
void sync_stop() {}
void sync_send()
{
    JsonDocument &app = status();
    String url = app["config"]["sync"]["url"].as<String>();
    int ok = 0, total = 0;
    sync_reconcile(app, url, ok, total);
}
