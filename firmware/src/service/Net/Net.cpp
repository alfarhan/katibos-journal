#include "Net.h"
#include "app/app.h"
#include "app/Config/Config.h"
#include "service/WifiEntry/WifiEntry.h" // wifi_config_load

#ifndef HOST_EMU
#include <WiFi.h>
#endif

static String g_error;
String net_last_error() { return g_error; }

#ifdef HOST_EMU
// The host is already online through libcurl; there is no radio to drive.
NetStatus net_connect(JsonDocument &app, const char *probeHost, NetProgress progress)
{
    (void)app;
    (void)probeHost;
    (void)progress;
    g_error = "";
    return NET_OK;
}
void net_disconnect() {}
#else

// Bounds. Per-attempt was 10s in the old copies, tried serially over every match,
// so three known networks in range could take 30s+ to fail. 6s is ample for an AP
// close enough to be worth joining, and the total ceiling stops a bad spot from
// eating the whole sync.
static const unsigned long NET_ATTEMPT_MS = 6000;
static const unsigned long NET_TOTAL_MS = 24000;
static const unsigned long NET_PROBE_MS = 4000;

static void say(NetProgress progress, const String &msg)
{
    if (progress)
        progress(msg);
    _log("[net] %s\n", msg.c_str());
}


// Password for a saved SSID, or "" when we don't know it.
static String savedPassword(JsonDocument &app, const String &ssid)
{
    JsonArray saved = app["wifi"]["access_points"].as<JsonArray>();
    for (JsonVariant ap : saved)
        if (ap["ssid"].as<String>() == ssid)
            return ap["password"].as<String>();
    return "";
}

// One bounded association attempt. Returns true once the stack reports an IP.
static bool tryJoin(JsonDocument &app, const String &ssid, const String &password,
                    NetProgress progress)
{
    say(progress, String("Connecting to ") + ssid + " ...");

    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start = millis();
    while (millis() - start < NET_ATTEMPT_MS)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            // Association is not readiness: DHCP and DNS land a moment later, and
            // a TLS connect attempted too early fails with start_ssl_client: -1.
            delay(1200);
            app["network"]["IP"] = WiFi.localIP().toString();
            app["network"]["ssid"] = ssid;
            app["network"]["status"] = 1;
            _log("[net] joined %s, IP %s\n", ssid.c_str(),
                 WiFi.localIP().toString().c_str());
            return true;
        }
        delay(100);
    }

    WiFi.disconnect(); // leave the radio in STA so the next candidate can try
    _log("[net] %s did not come up in %lums\n", ssid.c_str(), NET_ATTEMPT_MS);
    return false;
}

// Is anything actually reachable? A plain TCP connect on 443 - no TLS handshake,
// no request - is enough, and it is exactly the thing a restrictive hotspot
// fails: it hands out a DHCP lease and then drops outbound 443. Without this the
// firmware could only report "TLS failed", which reads as a bug in the firmware
// rather than a network that won't carry the traffic.
// Why the last probe failed, so the screen can say which half of "no internet"
// broke - a name that won't resolve is a different problem from a port that
// won't open, and on a phone hotspot it is usually the first.
static const char *g_probeFail = "no internet";

static bool probeReachable(const char *host)
{
    // Twice: a hotspot's resolver is often still coming up a second after the
    // lease lands, and one DNS timeout then looks exactly like a blocked port.
    for (int attempt = 0; attempt < 2; attempt++)
    {
        if (attempt)
            delay(1000);

        IPAddress ip;
        if (!WiFi.hostByName(host, ip))
        {
            g_probeFail = "no DNS";
            _log("[net] probe %s -> DNS failed (try %d)\n", host, attempt + 1);
            continue;
        }

        WiFiClient probe;
        probe.setTimeout(NET_PROBE_MS / 1000);
        bool ok = probe.connect(ip, 443, NET_PROBE_MS);
        probe.stop();
        _log("[net] probe %s (%s):443 -> %s (try %d)\n", host, ip.toString().c_str(),
             ok ? "ok" : "unreachable", attempt + 1);
        if (ok)
            return true;
        g_probeFail = "no internet";
    }
    return false;
}

NetStatus net_connect(JsonDocument &app, const char *probeHost, NetProgress progress)
{
    g_error = "";
    unsigned long began = millis();

    // Already up from an earlier task - just confirm it still carries traffic.
    if (WiFi.status() == WL_CONNECTED)
    {
        if (!probeHost || !*probeHost || probeReachable(probeHost))
            return NET_OK;
        // Connected but useless: fall through and try another network.
        _log("[net] current network has no route; looking for another\n");
        WiFi.disconnect();
    }

    wifi_config_load();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("MICROJOURNAL");
    setCpuFrequencyMhz(CPU_FREQUENCY_FULL); // the radio needs >= 80MHz

    JsonArray saved = app["wifi"]["access_points"].as<JsonArray>();
    if (saved.isNull() || saved.size() == 0)
    {
        g_error = "No saved WiFi networks";
        return NET_NO_NETWORK;
    }

    bool joined = false;
    String tried[24];
    int nTried = 0;
    auto alreadyTried = [&](const String &s) {
        for (int i = 0; i < nTried; i++)
            if (tried[i] == s)
                return true;
        return false;
    };
    auto markTried = [&](const String &s) {
        if (nTried < 24)
            tried[nTried++] = s;
    };

    // ---- 1. the one that worked last time, with no scan at all --------------
    // This is the common case - same desk, same router - and skipping the scan
    // takes several seconds off every sync and every proofread.
    String last = app["config"]["wifi_last"].as<String>();
    if (!last.isEmpty() && last != "null")
    {
        bool stillSaved = false;
        for (JsonVariant ap : saved)
            if (ap["ssid"].as<String>() == last)
                stillSaved = true;
        if (stillSaved) // it may have been forgotten on the Wi-Fi screen since
        {
            markTried(last);
            joined = tryJoin(app, last, savedPassword(app, last), progress);
        }
    }

    // ---- 2. saved networks in range, strongest first ------------------------
    if (!joined && millis() - began < NET_TOTAL_MS)
    {
        say(progress, "Looking for networks ...");
        int n = WiFi.scanNetworks();

        // collect scan hits that we have credentials for, with their signal
        struct Cand { String ssid; int rssi; };
        Cand cands[24];
        int nc = 0;
        for (int i = 0; i < n && nc < 24; i++)
        {
            String ssid = WiFi.SSID(i);
            if (ssid.isEmpty() || alreadyTried(ssid))
                continue;
            bool known = false;
            for (JsonVariant ap : saved)
                if (ap["ssid"].as<String>() == ssid)
                    known = true;
            if (!known)
                continue;
            // keep the strongest record per SSID (mesh / multiple APs repeat it)
            int at = -1;
            for (int k = 0; k < nc; k++)
                if (cands[k].ssid == ssid)
                    at = k;
            int rssi = WiFi.RSSI(i);
            if (at >= 0)
            {
                if (rssi > cands[at].rssi)
                    cands[at].rssi = rssi;
            }
            else
                cands[nc++] = {ssid, rssi};
        }
        WiFi.scanDelete();

        // strongest first - the old code took whatever the radio listed first,
        // which could be the weakest of several known networks
        for (int i = 0; i < nc - 1; i++)
            for (int j = i + 1; j < nc; j++)
                if (cands[j].rssi > cands[i].rssi)
                {
                    Cand t = cands[i];
                    cands[i] = cands[j];
                    cands[j] = t;
                }

        for (int i = 0; i < nc && !joined; i++)
        {
            if (millis() - began > NET_TOTAL_MS)
            {
                _log("[net] out of time before trying %s\n", cands[i].ssid.c_str());
                break;
            }
            _log("[net] candidate %s (%d dBm)\n", cands[i].ssid.c_str(), cands[i].rssi);
            markTried(cands[i].ssid);
            joined = tryJoin(app, cands[i].ssid, savedPassword(app, cands[i].ssid), progress);
        }
    }

    // ---- 3. saved networks the scan never showed (hidden SSIDs) -------------
    // A hidden network broadcasts no SSID, so scan-then-match can never find it.
    // Asking for it by name is the only way in.
    if (!joined && millis() - began < NET_TOTAL_MS)
    {
        for (JsonVariant ap : saved)
        {
            if (joined || millis() - began > NET_TOTAL_MS)
                break;
            String ssid = ap["ssid"].as<String>();
            if (ssid.isEmpty() || ssid == "null" || alreadyTried(ssid))
                continue;
            _log("[net] trying %s blind (hidden?)\n", ssid.c_str());
            markTried(ssid);
            joined = tryJoin(app, ssid, ap["password"].as<String>(), progress);
        }
    }

    if (!joined)
    {
        g_error = "Could not join any saved WiFi";
        net_disconnect();
        return NET_NO_NETWORK;
    }

    // remember the winner so the next call can skip straight to it
    String won = app["network"]["ssid"].as<String>();
    if (won.length() && app["config"]["wifi_last"].as<String>() != won)
    {
        app["config"]["wifi_last"] = won;
        config_save();
    }

    // ---- 4. is it actually carrying traffic? --------------------------------
    if (probeHost && *probeHost && !probeReachable(probeHost))
    {
        g_error = String("Joined ") + won + " but " + g_probeFail;
        return NET_NO_INTERNET;
    }

    return NET_OK;
}

void net_disconnect()
{
    // disconnect alone leaves the radio powered in STA mode draining the battery
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    setCpuFrequencyMhz(CPU_FREQUENCY_LOW);
}
#endif // HOST_EMU
