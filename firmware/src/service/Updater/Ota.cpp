#include "Ota.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Sync/SyncCore.h" // sync_http_get (device + emulator transports)

#ifndef HOST_EMU
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include "service/Sync/Sync.h"           // sync_connect_wifi
#include "service/WifiEntry/WifiEntry.h" // wifi_config_load
#endif

static void setState(int s, const String &msg)
{
    JsonDocument &app = status();
    app["ota_state"] = s;
    app["ota_message"] = msg;
    app["clear"] = true;
}

// Bring Wi-Fi up with a saved credential. The emulator's HTTP transport is
// libcurl, so it needs no radio.
static bool ota_ensure_wifi()
{
#ifdef HOST_EMU
    return true;
#else
    JsonDocument &app = status();
    if (WiFi.status() == WL_CONNECTED)
        return true;

    wifi_config_load();
    WiFi.mode(WIFI_STA);
    {
        String hn = app["config"]["device_name"].as<String>();
        WiFi.setHostname((hn.isEmpty() || hn == "null") ? "MICROJOURNAL" : hn.c_str());
    }
    delay(2000);

    int n = WiFi.scanNetworks();
    JsonArray saved = app["wifi"]["access_points"].as<JsonArray>();
    for (int i = 0; i < n; i++)
    {
        String ssid = WiFi.SSID(i);
        for (JsonVariant ap : saved)
        {
            if (ap["ssid"].as<String>() == ssid)
            {
                String pw = ap["password"].as<String>();
                if (sync_connect_wifi(app, ssid.c_str(), pw.c_str()))
                    return true;
            }
        }
    }
    return false;
#endif
}

String ota_update_url()
{
    JsonDocument &app = status();
    String url = app["config"]["update"]["url"].as<String>();
    // Fall back to the built-in channel when the config has no URL, or still
    // carries the pre-rename URL (alfarhan/micro-journal) that now 404s.
    if (url.isEmpty() || url == "null" || url.indexOf("alfarhan/micro-journal") >= 0)
        return KATIBOS_UPDATE_URL;
    return url;
}

void ota_check()
{
    JsonDocument &app = status();

    setState(OTA_CHECKING, "Connecting to WiFi...");
    if (!ota_ensure_wifi())
    {
        setState(OTA_ERROR, "WiFi connection failed");
        return;
    }

    String url = ota_update_url();

    setState(OTA_CHECKING, "Checking for update...");
    SyncHttp r = sync_http_get(url);
    if (r.code != 200)
    {
        setState(OTA_ERROR, format("Check failed (HTTP %d)", r.code));
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, r.body))
    {
        setState(OTA_ERROR, "Bad update manifest");
        return;
    }
    String version = doc["version"].as<String>();
    String binUrl = doc["url"].as<String>();
    if (version.isEmpty() || binUrl.isEmpty())
    {
        setState(OTA_ERROR, "Manifest missing version/url");
        return;
    }

    app["ota_version"] = version;
    app["ota_url"] = binUrl;
    if (version == String(KATIBOS_VERSION))
        setState(OTA_UPTODATE, format("Up to date (%s)", KATIBOS_VERSION));
    else
        setState(OTA_AVAILABLE, format("Update available: %s", version.c_str()));
}

void ota_apply()
{
    JsonDocument &app = status();
    String binUrl = app["ota_url"].as<String>();
    if (binUrl.isEmpty() || binUrl == "null")
    {
        setState(OTA_ERROR, "No firmware URL");
        return;
    }

    setState(OTA_DOWNLOADING, "Downloading firmware...");

#ifdef HOST_EMU
    setState(OTA_DONE, "Updated (simulated)");
#else
    // Free the persistent sync TLS connection first so mbedTLS has enough heap
    // for this download's own secure client (two live TLS contexts starve it).
    sync_http_close();
    _log("[ota] free heap before download: %u\n", (unsigned)ESP.getFreeHeap());

    // The bin is an https:// GitHub release URL that 302-redirects to a second
    // HTTPS host (objects.githubusercontent.com). Following that cross-host
    // redirect in place on a WiFiClientSecure hangs on this SoC, so we follow it
    // manually: read Location and reconnect per hop. No CA bundle on-device, so
    // TLS is insecure (unpinned), same posture as the sync path.
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = binUrl;
    int code = 0;
    for (int hop = 0; hop < 5; hop++)
    {
        http.begin(client, url);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        const char *collect[] = {"Location"};
        http.collectHeaders(collect, 1);
        code = http.GET();
        _log("[ota] hop %d -> HTTP %d\n", hop, code);
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308)
        {
            String loc = http.header("Location");
            http.end();
            client.stop();
            if (loc.isEmpty())
            {
                setState(OTA_ERROR, "Redirect without Location");
                return;
            }
            url = loc;
            continue;
        }
        break; // 200 (or a real error) — stop following
    }

    if (code != 200)
    {
        http.end();
        setState(OTA_ERROR, format("Download failed (HTTP %d)", code));
        return;
    }
    int len = http.getSize();
    if (len <= 0)
    {
        http.end();
        setState(OTA_ERROR, "Unknown firmware size");
        return;
    }
    if (!Update.begin(len))
    {
        http.end();
        setState(OTA_ERROR, "Not enough space for update");
        return;
    }

    Update.onProgress([](size_t done, size_t total) {
        static int lastPct = -1;
        int pct = total ? (int)(done * 100 / total) : 0;
        if (pct != lastPct && pct % 10 == 0)
        {
            _log("[ota] %d%%\n", pct);
            lastPct = pct;
        }
    });

    WiFiClient *stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();
    _log("[ota] wrote %u / %d\n", (unsigned)written, len);

    if (written != (size_t)len)
    {
        Update.abort();
        setState(OTA_ERROR, "Download incomplete");
        return;
    }
    if (!Update.end(true))
    {
        setState(OTA_ERROR, "Install failed");
        return;
    }
    setState(OTA_DONE, "Update installed");
#endif
}

void ota_reboot()
{
#ifndef HOST_EMU
    ESP.restart();
#endif
}
