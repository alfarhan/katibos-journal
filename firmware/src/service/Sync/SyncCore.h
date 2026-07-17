#pragma once

#include <ArduinoJson.h>
#include <WString.h>
#include <vector>

// Shared (device + emulator) cloud-sync logic. The HTTP transport is the only
// per-platform piece — it's the seam below, implemented with HTTPClient on the
// device (Sync.cpp) and libcurl in the emulator (host_sync_http.cpp).

struct SyncHttp
{
    int code;    // HTTP status (or <0 on transport error)
    String body; // response body
};

// GET a URL (follows redirects). The transport handles the Apps-Script
// 302 → googleusercontent hop.
SyncHttp sync_http_get(const String &url);

// POST the raw bytes of a local file as the request body (base64 upload
// sidecar). Follows redirects.
SyncHttp sync_http_post_file(const String &url, const String &filePath);

// General request used by the GitHub backend: arbitrary method, custom headers
// (each "Name: value"), and an in-memory body. Follows redirects.
SyncHttp sync_http(const String &method, const String &url,
                   const std::vector<String> &headers, const String &body);

// ---- backend selection -----------------------------------------------------
// True when config.sync.provider == "git" (default "drive").
bool sync_provider_is_git(JsonDocument &app);
// Validate config.sync.git has owner/repo/token; err set on failure.
bool sync_git_config_valid(JsonDocument &app, String &err);

// ---- two-way reconcile -----------------------------------------------------
// Run the whole pass in one call (device: a background task, so blocking is
// fine). Fills ok (slots handled) / total (local slots).
bool sync_reconcile(JsonDocument &app, const String &baseUrl, int &ok, int &total);

// Sync ONLY the given slot (Ctrl+U "sync this file"): allows pulling the open
// file and sets app["sync_reload"]=slot when it does, so the editor can refresh.
bool sync_reconcile_one(JsonDocument &app, const String &baseUrl, int slot, int &ok, int &total);

// Stepwise variant so the emulator can do one unit of work per frame and keep
// the UI repainting. begin() lists Drive + enumerates slots; step() does one
// slot (or one new-file pull) and returns true while work remains.
void sync_begin(JsonDocument &app, const String &baseUrl);
void sync_begin_one(JsonDocument &app, const String &baseUrl, int slot);
bool sync_step(JsonDocument &app);
void sync_result(int &ok, int &total, int &conflicts);

// Upload a single slot to Drive (push path; also used internally by reconcile).
bool sync_upload_index(JsonDocument &app, int index, const String &baseUrl);
