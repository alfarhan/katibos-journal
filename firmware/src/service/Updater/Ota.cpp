#include "Ota.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Sync/SyncCore.h" // sync_http_get (device + emulator transports)

#ifndef HOST_EMU
#include <WiFi.h>
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

void ota_check()
{
    JsonDocument &app = status();

    setState(OTA_CHECKING, "Connecting to WiFi...");
    if (!ota_ensure_wifi())
    {
        setState(OTA_ERROR, "WiFi connection failed");
        return;
    }

    String url = app["config"]["update"]["url"].as<String>();
    if (url.isEmpty() || url == "null")
    {
        setState(OTA_ERROR, "No update URL set in config");
        return;
    }

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
    HTTPClient http;
    http.begin(binUrl);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    int code = http.GET();
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

    WiFiClient *stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();

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
