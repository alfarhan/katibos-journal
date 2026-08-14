#pragma once

#include <ArduinoJson.h>
#include <WString.h>

// One place that brings Wi-Fi up for a network task. Sync, OTA and the AI
// proofread all used to carry their own near-identical copy of "scan, match a
// saved SSID, connect" - four copies, which is how the AI path shipped without
// the settle delay the sync path already had. This is that logic, once.
//
// Selection order, best-first rather than whatever the radio reported first:
//   1. the network that worked last time, connected directly with NO scan
//   2. saved networks seen in a scan, strongest signal first
//   3. saved networks NOT in the scan at all (hidden SSIDs)
// Each attempt is bounded, and the whole thing is bounded, so a desk with three
// known-but-unreachable networks can't burn half a minute before giving up.

// Progress callback: each caller renders into its own status slot (sync_message /
// ai_message / ota_message), so the shared code reports through this instead of
// picking one.
typedef void (*NetProgress)(const String &msg);

// Why a connect attempt ended the way it did.
enum NetStatus
{
    NET_OK = 0,        // associated, and (if asked) the internet is reachable
    NET_NO_NETWORK,    // no saved network could be joined
    NET_NO_INTERNET,   // joined fine, but nothing upstream answers
};

// Bring Wi-Fi up. `probeHost` non-empty also checks that host is reachable on
// 443, which is what separates "no Wi-Fi" from "Wi-Fi with no usable internet" -
// a distinction a phone hotspot makes you care about. Remembers the SSID that
// worked so the next call can skip the scan.
NetStatus net_connect(JsonDocument &app, const char *probeHost = nullptr,
                      NetProgress progress = nullptr);

// Power the radio down and drop back to the battery-saving clock.
void net_disconnect();

// Human-readable reason for the last net_connect result, for a status line.
String net_last_error();
