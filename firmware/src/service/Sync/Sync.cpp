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
#include "service/Clock/Clock.h"
#include "app/Config/Config.h"
#include "service/WifiEntry/WifiEntry.h"
#include <vector>
#include <algorithm>

// Parse an HTTP "Date:" header (RFC 1123, e.g. "Wed, 24 Jun 2026 07:28:00 GMT")
// to a UTC epoch. Returns 0 if it can't be parsed. Fallback date source when
// NTP is unreachable (some networks block its UDP).
static long parseHttpDate(const String &h)
{
    if (h.isEmpty())
        return 0;
    int comma = h.indexOf(',');
    String s = (comma >= 0) ? h.substring(comma + 1) : h;
    s.trim();

    int d, y, hh, mm, ss;
    char mon[4] = {0};
    if (sscanf(s.c_str(), "%d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) != 6)
        return 0;

    const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *p = strstr(months, mon);
    if (!p)
        return 0;
    int m = (int)((p - months) / 3) + 1;

    long days = clock_days_from_civil(y, (unsigned)m, (unsigned)d);
    return days * 86400L + hh * 3600L + mm * 60L + ss;
}

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

// Start Sync Process
// Search for WIFI ACCESS POINTS
void sync_start()
{
    //
    JsonDocument &app = status();

    //
    _log("[sync_start] Sync Start\n");

    // Load saved WiFi credentials so sync works from ANY trigger (editor Ctrl+U
    // / Ctrl+Shift+U too), not just after visiting the WiFi or Sync screen.
    wifi_config_load();

    //
    app["sync_state"] = SYNC_STARTED;
    app["sync_message"] = "Connecting to WiFi";
    app["clear"] = true;

    // full CPU speed while WiFi is in use
    setCpuFrequencyMhz(CPU_FREQUENCY_FULL);

    // Scan for available networks
    WiFi.mode(WIFI_STA);
    {
        String hn = app["config"]["device_name"].as<String>();
        WiFi.setHostname((hn.isEmpty() || hn == "null") ? "MICROJOURNAL" : hn.c_str());
    }

    // wait for decent amount of time before using wifi
    delay(3000);

    //
    int networksFound = WiFi.scanNetworks();
    app["sync_message"] = format("Found %d networks", networksFound);
    app["clear"] = true;

    // reset the network.access_points array
    JsonArray access_points = app["network"]["access_points"].to<JsonArray>();
    access_points.clear();

    // Print information about each network
    for (int i = 0; i < networksFound; ++i)
    {
        //
        String ssid = WiFi.SSID(i);

        // print out the information
        _log("%d: %s (%d dBm)\n", i + 1, ssid.c_str(), WiFi.RSSI(i));

        // add to the access_points list
        access_points.add(ssid);
    }

    //
    // Load available WiFi networks from the app["network"]["access_points"] array
    JsonArray availableNetworks = app["network"]["access_points"].as<JsonArray>();

    // Load saved WiFi connection information from the app["config"]["access_points"] array
    JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();

    // Iterate through each available network
    for (JsonVariant availableNetwork : availableNetworks)
    {
        // available wifi network
        String availableSsid = availableNetwork.as<String>();
        _log("Trying: %s\n", availableSsid.c_str());

        // see if there are any wifi network allowed
        for (JsonVariant savedAccessPoint : savedAccessPoints)
        {
            const char *savedSsid = savedAccessPoint["ssid"];
            const char *savedPassword = savedAccessPoint["password"];

            _log("Trying to connect to :%s\n", savedSsid);

            // Check if the SSID and password match
            if (strcmp(availableSsid.c_str(), savedSsid) == 0)
            {

                // Try to connect to the matching access point
                if (sync_connect_wifi(app, savedSsid, savedPassword))
                {
                    _log("Connected to a matching WiFi network!\n");

                    //
                    app["sync_message"] = format("Connected to: %s\n", savedSsid);
                    app["clear"] = true;
                    _log(app["sync_message"]);
                    delay(1000); // give some delay for the connection to settle

                    // Sync File Start
                    sync_send();

                    return; // Exit the function if successfully connected
                }
            }
        }
    }

    // if it reached this point.
    // it means no network has been connected
    sync_stop();

    //
    app["sync_error"] = "NOT ABLE TO CONNECT TO WIFI";
    app["sync_state"] = SYNC_ERROR;
    app["clear"] = true;
    //
}

void sync_stop()
{
    // power down the WiFi radio entirely
    // WiFi.disconnect alone leaves the radio on in STA mode draining the battery
    // WiFi.begin re-enables STA mode so retrying another network still works
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // back to the battery saving CPU speed
    setCpuFrequencyMhz(CPU_FREQUENCY_LOW);
}

bool sync_connect_wifi(JsonDocument &app, const char *ssid, const char *password)
{
    // Connect to WiFi
    delay(1000);

    //
    String message = format("trying to connect to %s ", ssid);
    app["sync_message"] = message;
    app["clear"] = true;
    _log(message.c_str());

    //
    WiFi.begin(ssid, password);
    delay(1000);

    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 100)
    {
        delay(100);
        message += ".";
        app["sync_message"] = message;
        app["clear"] = true;
        attempt++;
    }

    // Update the network_status: 1
    // meaning it is connected
    if (WiFi.status() == WL_CONNECTED)
    {
        // network status 1
        app["network"]["IP"] = WiFi.localIP().toString();
        app["network"]["ssid"] = ssid;
        app["network"]["status"] = 1;

        // Now that we're online, learn the real date via NTP (UTC; the Clock
        // applies its own KSA offset). Feeds the streak/Today rollover. Best
        // effort - a failure just leaves the clock on its last known value.
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        struct tm ti;
        if (getLocalTime(&ti, 5000))
        {
            time_t now = time(nullptr);
            if (now > 1700000000) // sanity: after 2023-11
                clock_set_epoch((long)now);
        }

        // print info
        app["sync_message"] = format("Connected - %s, %s\n", ssid, WiFi.localIP().toString().c_str());
        app["clear"] = true;

        return true;
    }
    else
    {
        _log("Failed to connect to WiFi. Please check your credentials.\n");

        //
        app["sync_error"] = format("Failed to connect to %s", ssid);
        app["sync_state"] = SYNC_ERROR;
        app["clear"] = true;

        // drop the association only
        // sync_start still retries the remaining saved networks
        // the final sync_stop powers down the radio
        WiFi.disconnect();

        return false; // Failed to connect
    }
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
SyncHttp sync_http(const String &method, const String &url,
                   const std::vector<String> &headers, const String &body)
{
    static WiFiClientSecure client;
    static bool clientInit = false;
    if (!clientInit)
    {
        client.setInsecure();
        clientInit = true;
    }

    HTTPClient http;
    http.begin(client, url);
    http.setReuse(true); // keep the connection open for the next call
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

        // first time online this session, set the clock from the Date header if
        // NTP didn't (the streak/Today rollover needs a confirmed time)
        if (r.code > 0 && r.code < 400 && !clock_confirmed())
        {
            long ep = parseHttpDate(http.header("Date"));
            if (ep > 1700000000) // sanity: after 2023-11
                clock_set_epoch(ep);
        }
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
