#include "Sync.h"
#include "SyncCore.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Editor/Editor.h"

//
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <time.h>
#include "app/Config/Config.h"
#include "service/WifiEntry/WifiEntry.h"
#include "service/Net/Net.h"
#include <vector>
#include <algorithm>

// Scan nearby networks (device: real radio). Dedupes SSIDs keeping the strongest
// signal, drops hidden (empty) SSIDs, sorts by RSSI desc.
int wifi_scan(JsonDocument &app)
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int n = WiFi.scanNetworks();

    std::vector<std::pair<String, int>> nets;
    for (int i = 0; i < n; i++)
    {
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty())
            continue; // hidden network
        int rssi = WiFi.RSSI(i);
        bool dup = false;
        for (auto &e : nets)
            if (e.first == ssid)
            {
                dup = true;
                if (rssi > e.second)
                    e.second = rssi;
                break;
            }
        if (!dup)
            nets.push_back({ssid, rssi});
    }
    WiFi.scanDelete();

    std::sort(nets.begin(), nets.end(),
              [](const std::pair<String, int> &a, const std::pair<String, int> &b)
              { return a.second > b.second; });

    JsonArray arr = app["network"]["scan"].to<JsonArray>();
    for (auto &e : nets)
    {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = e.first;
        o["rssi"] = e.second;
    }
    return (int)nets.size();
}

// Reset all the sync related flags
void sync_init()
{
    // reset the sync state
    // update app sync state
    JsonDocument &app = status();
    app["sync_state"] = SYNC_START;
    app["sync_error"] = "";
    app["sync_message"] = "";

    _log("[sync_init] sync_init\n");
}

// request background service to pick up the request
void sync_start_request()
{
    JsonDocument &app = status();
    String task = app["task"].as<String>();
    //
    if (task != "sync_start")
    {
        app["task"] = "sync_start";
        _log("[sync_start_request] Sync Start Requested\n");
    }
}

//
void sync_loop()
{
    static unsigned int last = 0;
    if (millis() - last > 1000)
    {
        last = millis();

        //
        JsonDocument &app = status();
        String task = app["task"].as<String>();

        if (task == "sync_start")
        {
            //
            app["task"] = "";

            //
            _log("[sync_loop] Picked up Sync Start Request\n");

            //
            sync_start();
        }
    }
}

// Progress from the shared connect path goes to the sync status line.
static void syncNetProgress(const String &msg)
{
    JsonDocument &app = status();
    app["sync_message"] = msg;
    app["clear"] = true;
}

// Start Sync Process. Network selection lives in service/Net now - this used to
// carry its own scan-and-match copy, one of four.
void sync_start()
{
    JsonDocument &app = status();
    _log("[sync_start] Sync Start\n");

    app["sync_state"] = SYNC_STARTED;
    app["sync_message"] = "Connecting to WiFi";
    app["clear"] = true;

    // The Drive backend talks to script.google.com; the git backend to
    // api.github.com. Probing the one we are about to use turns "TLS failed" into
    // "joined X but no internet" on a network that won't carry the traffic.
    const char *probe = sync_provider_is_git(app) ? "api.github.com"
                                                  : "script.google.com";
    NetStatus net = net_connect(app, probe, syncNetProgress);
    if (net != NET_OK)
    {
        sync_stop();
        app["sync_error"] = net_last_error();
        app["sync_state"] = SYNC_ERROR;
        app["clear"] = true;
        return;
    }

    app["sync_message"] = format("Connected to: %s", app["network"]["ssid"].as<String>().c_str());
    app["clear"] = true;

    sync_send();
}

void sync_stop()
{
    net_disconnect(); // one implementation of "radio off, clock back down"
}


// ---- device HTTP transport (the SyncCore seam) ------------------------------
// Real device implementation over HTTPClient. The emulator provides its own
// libcurl-backed version of these two functions.

SyncHttp sync_http_get(const String &url)
{
    HTTPClient http;
    http.begin(url);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    SyncHttp r;
    r.code = http.GET();
    r.body = (r.code > 0) ? http.getString() : String("");
    http.end();
    return r;
}

// General request (the GitHub backend's seam): any method + custom headers +
// in-memory body. Drive keeps using the two specialized helpers above.
//
// Keep-alive: GitHub sync is chatty (a history-preserving rename is 5-6 API
// calls), so we hold ONE TLS connection across the whole sync instead of a fresh
// handshake per call. A static WiFiClientSecure persists the connection; a fresh
// HTTPClient per call (no header build-up) reuses it via setReuse(true). The
// client is insecure (no CA bundle on-device) — transport is still TLS, we just
// don't pin the cert, same posture the plan anticipated for api.github.com.
// The reused TLS client persists across calls (setReuse) so back-to-back sync
// requests skip the handshake. Its mbedTLS context holds ~40KB though, so the
// OTA download frees it first (sync_http_close) to leave room for its own
// secure connection.
static WiFiClientSecure s_syncClient;
static bool s_syncClientInit = false;

void sync_http_close()
{
    s_syncClient.stop();
}

SyncHttp sync_http(const String &method, const String &url,
                   const std::vector<String> &headers, const String &body,
                   unsigned long timeoutMs)
{
    if (!s_syncClientInit)
    {
        s_syncClient.setInsecure();
        s_syncClientInit = true;
    }

    HTTPClient http;
    http.begin(s_syncClient, url);
    http.setReuse(true); // keep the connection open for the next call
    if (timeoutMs)
    {
        // The default 5s read timeout is right for API calls that answer at once
        // and wrong for one that generates text - it returned -11 (READ_TIMEOUT)
        // while Gemini was still composing the reply.
        http.setTimeout(timeoutMs);
        http.setConnectTimeout(timeoutMs);
        s_syncClient.setTimeout(timeoutMs / 1000);
    }
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    for (const String &h : headers)
    {
        int c = h.indexOf(':');
        if (c < 0)
            continue;
        String name = h.substring(0, c);
        String val = h.substring(c + 1);
        val.trim();
        http.addHeader(name, val);
    }

    SyncHttp r;
    r.code = http.sendRequest(method.c_str(), (uint8_t *)body.c_str(), body.length());
    r.body = (r.code > 0) ? http.getString() : String("");
    http.end(); // with setReuse(true) this does NOT drop the TLS connection
    return r;
}

SyncHttp sync_http_post_file(const String &url, const String &filePath)
{
    HTTPClient http;
    http.begin(url);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    const char *collect[] = {"Date"};
    http.collectHeaders(collect, 1);

    SyncHttp r;
    r.code = -1;
    File file = gfs()->open(filePath.c_str(), "r");
    if (file)
    {
        r.code = http.sendRequest("POST", &file, file.size());
        if (r.code > 0)
            r.body = http.getString();
        file.close();
        delay(100);
    }
    http.end();
    return r;
}

// Sync EVERY file on the device to Drive (shared SyncCore logic). Runs while
// WiFi is already connected (sync_start).
void sync_send()
{
    _log("[sync_send] Sync Send (all files)\n");
    JsonDocument &app = status();

    // baseUrl is the Drive /exec URL; unused by the git backend (it reads
    // config.sync.git itself). Validate the relevant config per provider.
    String baseUrl;
    if (sync_provider_is_git(app))
    {
        String err;
        if (!sync_git_config_valid(app, err))
        {
            app["sync_error"] = err + "\n";
            app["sync_state"] = SYNC_ERROR;
            app["clear"] = true;
            _log(app["sync_error"]);
            sync_stop();
            return;
        }
    }
    else
    {
        baseUrl = app["config"]["sync"]["url"].as<String>();
        if (baseUrl.isEmpty() || baseUrl == "null")
        {
            app["sync_error"] = "SYNC URL NOT FOUND\n";
            app["sync_state"] = SYNC_ERROR;
            app["clear"] = true;
            _log(app["sync_error"]);
            sync_stop();
            return;
        }
    }

    int ok = 0, total = 0;
    if (app["sync_scope"].as<String>() == "one")
        sync_reconcile_one(app, baseUrl, app["sync_one"].as<int>(), ok, total);
    else
        sync_reconcile(app, baseUrl, ok, total);

    config_save(); // persist the cleared unsynced flags
    sync_stop();

    if (total == 0)
    {
        app["sync_error"] = "No files to sync.\n";
        app["sync_state"] = SYNC_ERROR;
    }
    else
    {
        const char *dest = sync_provider_is_git(app) ? "GitHub" : "Google Drive";
        app["sync_message"] = (ok == total)
                                  ? format("Synced %d file(s) to %s.", ok, dest)
                                  : format("Synced %d of %d to %s.", ok, total, dest);
        app["sync_state"] = SYNC_COMPLETED;
    }
    app["clear"] = true;
    _log("Sync State: SYNC_COMPLETED (%d/%d)\n", ok, total);
}
