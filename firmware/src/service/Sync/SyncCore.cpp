#include "SyncCore.h"
#include "Sync.h" // SYNC_* state constants
#include "app/app.h"
#include "display/display.h"
#include "service/Tools/Tools.h"
#include "service/Clock/Clock.h"

static const int SLOT_MAX = 100;

// ---- base64 (portable; compiles on host too) --------------------------------
static const char *B64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static String b64_chunk(const uint8_t *d, size_t n)
{
    String out;
    size_t i = 0;
    for (; i + 3 <= n; i += 3)
    {
        uint32_t v = ((uint32_t)d[i] << 16) | ((uint32_t)d[i + 1] << 8) | d[i + 2];
        out += B64[(v >> 18) & 63];
        out += B64[(v >> 12) & 63];
        out += B64[(v >> 6) & 63];
        out += B64[v & 63];
    }
    size_t rem = n - i;
    if (rem == 1)
    {
        uint32_t v = (uint32_t)d[i] << 16;
        out += B64[(v >> 18) & 63];
        out += B64[(v >> 12) & 63];
        out += "==";
    }
    else if (rem == 2)
    {
        uint32_t v = ((uint32_t)d[i] << 16) | ((uint32_t)d[i + 1] << 8);
        out += B64[(v >> 18) & 63];
        out += B64[(v >> 12) & 63];
        out += B64[(v >> 6) & 63];
        out += "=";
    }
    return out;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1; // '=', whitespace, etc.
}

static void b64_decode_to_file(const String &b64, File &out)
{
    uint32_t val = 0;
    int bits = 0;
    for (int i = 0; i < (int)b64.length(); i++)
    {
        int v = b64_val(b64[i]);
        if (v < 0)
            continue;
        val = (val << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.write((uint8_t)((val >> bits) & 0xFF));
        }
    }
}

// ---- helpers ---------------------------------------------------------------
static String urlEncode(const String &s)
{
    static const char *hex = "0123456789ABCDEF";
    String out;
    for (int i = 0; i < (int)s.length(); i++)
    {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

static String append_param(const String &url, const String &kv)
{
    return url + (url.indexOf('?') >= 0 ? "&" : "?") + kv;
}

// djb2 over the file bytes + length — detects local edits between syncs.
static String hash_file(const String &path)
{
    File f = gfs()->open(path.c_str(), "r");
    if (!f)
        return "";
    unsigned long h = 5381;
    long n = 0;
    while (f.available())
    {
        uint8_t b[512];
        size_t r = f.read(b, sizeof(b));
        for (size_t i = 0; i < r; i++)
            h = ((h << 5) + h) + b[i];
        n += (long)r;
    }
    f.close();
    char buf[40];
    snprintf(buf, sizeof(buf), "%lx:%ld", h & 0xffffffffUL, n);
    return String(buf);
}

// Drive filename == title + ".txt". Recover the title by stripping ".txt" only —
// any disambiguation suffix is baked into the title itself, so it round-trips.
static String title_from_name(const String &name)
{
    String s = name;
    if (s.endsWith(".txt"))
        s = s.substring(0, s.length() - 4);
    return s;
}

// Is `nameTxt` already used by a DIFFERENT Drive file (in the listing or pushed
// earlier this run)? Used to disambiguate same-titled notes on push.
static JsonDocument g_remote;       // {status, files:[{id,name,modified,size}]}; set by sync_begin
static bool g_haveRemote = false;   // true once a listing has actually been fetched
static String g_pushedNames[SLOT_MAX];
static int g_pushedN = 0;
static bool name_taken(const String &nameTxt, const String &selfId)
{
    if (g_haveRemote)
        for (JsonVariant v : g_remote["files"].as<JsonArray>())
            if (v["name"].as<String>() == nameTxt && v["id"].as<String>() != selfId)
                return true;
    for (int i = 0; i < g_pushedN; i++)
        if (g_pushedNames[i] == nameTxt)
            return true;
    return false;
}

// ---- push one slot ----------------------------------------------------------
bool sync_upload_index(JsonDocument &app, int index, const String &baseUrl)
{
    String path = format("/%d.txt", index);

    File inputFile = gfs()->open(path.c_str(), "r");
    if (!inputFile)
        return false;
    if (inputFile.size() == 0) // never upload an empty file
    {
        inputFile.close();
        return false;
    }

    String base64Filename = path + ".base64";
    File outputFile = gfs()->open(base64Filename.c_str(), "w");
    if (!outputFile)
    {
        inputFile.close();
        return false;
    }
    const int bufferSize = 900; // multiple of 3 → clean chunk concatenation
    uint8_t buffer[bufferSize];
    while (inputFile.available())
    {
        size_t bytesRead = inputFile.read(buffer, bufferSize);
        if (bytesRead > 0)
            outputFile.print(b64_chunk(buffer, bytesRead));
    }
    inputFile.close();
    delay(100);
    outputFile.close();
    delay(100);

    String url = baseUrl;

    // Target the tracked Drive file by id so a rename done anywhere updates the
    // SAME file instead of creating a duplicate. (P1)
    String driveId = app["config"][format("drive_id_%d", index)].as<String>();
    if (!driveId.isEmpty() && driveId != "null")
        url = append_param(url, "id=" + urlEncode(driveId));

    String title = app["config"][format("title_%d", index)].as<String>();
    title.replace("/", " ");
    title.replace("\\", " ");
    title.trim();
    if (!title.isEmpty() && title != "null")
    {
        String nameTxt = title + ".txt";
        if (name_taken(nameTxt, driveId))
        {
            // a different note already uses this name → disambiguate, baking the
            // suffix into the title so device/Drive/vault all match.
            title = title + "-" + String(index);
            app["config"][format("title_%d", index)] = title;
            app["config"][format("title_manual_%d", index)] = true;
            nameTxt = title + ".txt";
        }
        if (g_pushedN < SLOT_MAX)
            g_pushedNames[g_pushedN++] = nameTxt;
        url = append_param(url, "name=" + urlEncode(nameTxt));
    }

    SyncHttp resp = sync_http_post_file(url, base64Filename);
    bool ok = (resp.code > 0 && resp.code < 400);

    gfs()->remove(base64Filename.c_str());
    delay(50);

    if (ok)
    {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, resp.body);
        if (!err && doc["status"] == "OK")
        {
            const char *id = doc["id"] | "";
            if (id && *id)
                app["config"][format("drive_id_%d", index)] = String(id);
            long long modified = doc["modified"] | 0LL;
            if (modified > 0)
                app["config"][format("synced_modified_%d", index)] = modified;
        }
        app["config"][format("synced_hash_%d", index)] = hash_file(path);
        app["config"][format("synced_title_%d", index)] = title;
        app["config"][format("unsynced_%d", index)] = false;
        int day = clock_localday();
        if (day > 0)
            app["config"][format("synced_day_%d", index)] = day;
    }
    return ok;
}

// ---- pull one Drive file into a slot (atomic temp-swap) ----------------------
static String g_base; // full /exec URL incl. ?token, set by sync_begin

static bool do_pull(JsonDocument &app, int slot, const String &id,
                    const String &remoteName, long long remoteModified)
{
    String url = append_param(g_base, "action=get");
    url = append_param(url, "id=" + urlEncode(id));
    SyncHttp r = sync_http_get(url);
    if (r.code <= 0 || r.code >= 400)
        return false;

    JsonDocument doc;
    if (deserializeJson(doc, r.body) || doc["status"] != "OK")
        return false;
    String content = doc["content"].as<String>(); // base64 of raw bytes

    String path = format("/%d.txt", slot);
    String tmp = path + ".tmp";
    File out = gfs()->open(tmp.c_str(), "w");
    if (!out)
        return false;
    b64_decode_to_file(content, out);
    out.close();
    delay(50);

    gfs()->remove(path.c_str());
    gfs()->rename(tmp.c_str(), path.c_str());

    String title = title_from_name(remoteName);
    if (title.length())
    {
        app["config"][format("title_%d", slot)] = title;
        app["config"][format("title_manual_%d", slot)] = true;
    }
    app["config"][format("drive_id_%d", slot)] = id;
    app["config"][format("synced_modified_%d", slot)] = remoteModified;
    app["config"][format("synced_hash_%d", slot)] = hash_file(path);
    app["config"][format("synced_title_%d", slot)] = title;
    app["config"][format("unsynced_%d", slot)] = false;
    return true;
}

// ---- engine state ----------------------------------------------------------
static int g_local[SLOT_MAX];
static int g_localN = 0;
static int g_curA = 0; // local-slot cursor
static int g_curB = 0; // remote-file cursor (new-file pull phase)
static int g_curT = 0; // trash cursor (device-deleted files to remove on Drive)
static bool g_scopeOne = false;     // sync only one slot (Ctrl+U "this file")
static bool g_allowOpenPull = false; // permit pulling the currently-open file
static String g_trashed[SLOT_MAX]; // ids trashed this run (don't re-pull from the stale listing)
static int g_trashedN = 0;

static bool was_trashed(const String &id)
{
    for (int i = 0; i < g_trashedN; i++)
        if (g_trashed[i] == id)
            return true;
    return false;
}
static bool g_active = false;
static bool g_isGit = false; // backend for the in-flight pass (set by sync_begin)
static int g_ok = 0, g_conflicts = 0;

static int slot_for_id(JsonDocument &app, const String &id)
{
    for (int i = 0; i < SLOT_MAX; i++)
        if (app["config"][format("drive_id_%d", i)].as<String>() == id)
            return i;
    return -1;
}

static int free_slot()
{
    for (int i = 0; i < SLOT_MAX; i++)
        if (!gfs()->exists(format("/%d.txt", i).c_str()))
            return i;
    return -1;
}

static bool find_remote(const String &id, String &name, long long &modified)
{
    if (!g_haveRemote)
        return false;
    for (JsonVariant v : g_remote["files"].as<JsonArray>())
    {
        if (v["id"].as<String>() == id)
        {
            name = v["name"].as<String>();
            modified = v["modified"] | 0LL;
            return true;
        }
    }
    return false;
}

// Move a slot's file aside to a recoverable backup and clear its sync state.
static void delete_local_slot(JsonDocument &app, int idx)
{
    String path = format("/%d.txt", idx);
    String backup = path + "_backup.txt";
    gfs()->remove(backup.c_str());
    gfs()->rename(path.c_str(), backup.c_str()); // keep a recoverable copy
    app["config"].remove(format("drive_id_%d", idx));
    app["config"].remove(format("synced_modified_%d", idx));
    app["config"].remove(format("synced_hash_%d", idx));
    app["config"].remove(format("synced_title_%d", idx));
    app["config"].remove(format("title_%d", idx));
    app["config"].remove(format("title_manual_%d", idx));
    app["config"][format("unsynced_%d", idx)] = false;
}

static bool drive_trash(const String &id)
{
    String u = append_param(g_base, "action=trash");
    u = append_param(u, "id=" + urlEncode(id));
    SyncHttp r = sync_http_get(u);
    return r.code > 0 && r.code < 400;
}

// reconcile a single local slot (pull/push/adopt-rename/conflict/delete)
static void reconcile_slot(JsonDocument &app, int idx)
{
    String idKey = format("drive_id_%d", idx);
    String driveId = app["config"][idKey].as<String>();
    bool haveId = !driveId.isEmpty() && driveId != "null";

    String path = format("/%d.txt", idx);
    String localHash = hash_file(path);
    String syncedHash = app["config"][format("synced_hash_%d", idx)].as<String>();
    bool localChanged = syncedHash.isEmpty() || syncedHash == "null" || localHash != syncedHash;

    int fileIndex = app["config"]["file_index"].as<int>();
    bool isOpen = (idx == fileIndex);

    String rname;
    long long rmod = 0;
    bool haveRemote = haveId && find_remote(driveId, rname, rmod);

    if (haveRemote)
    {
        long long syncedMod = app["config"][format("synced_modified_%d", idx)].as<long long>();
        bool remoteChanged = (syncedMod <= 0) || (rmod != syncedMod);

        // No baseline yet (slot synced under P1, before P2 tracked content
        // state). Establish one by pushing the local copy — content is in-sync
        // from the push-only era; this records synced_hash/modified/title.
        bool hasBaseline = (!syncedHash.isEmpty() && syncedHash != "null") || syncedMod > 0;
        if (!hasBaseline)
        {
            if (sync_upload_index(app, idx, g_base))
                g_ok++;
            return;
        }

        // Rename direction: compare both the local title and the remote name to
        // the title we last synced. Lets the device ORIGINATE a rename instead of
        // always conforming to Drive.
        String rtitle = title_from_name(rname);
        String title = app["config"][format("title_%d", idx)].as<String>();
        bool haveSyncedTitle = app["config"][format("synced_title_%d", idx)].is<const char *>();
        String syncedTitle = haveSyncedTitle
                                 ? app["config"][format("synced_title_%d", idx)].as<String>()
                                 : title; // legacy: assume no local rename
        bool localRenamed = (title != syncedTitle);
        bool remoteRenamed = (rtitle != syncedTitle);

        // Local rename (Drive not renamed) → push it (renames the Drive file).
        if (localRenamed && !remoteRenamed)
        {
            if (sync_upload_index(app, idx, g_base))
                g_ok++;
            return;
        }
        // Remote rename → adopt it (remote wins if both renamed).
        if (remoteRenamed)
        {
            if (localRenamed)
                g_conflicts++;
            app["config"][format("title_%d", idx)] = rtitle;
            app["config"][format("title_manual_%d", idx)] = true;
            // Record the adopted title now so a deferred (open-file) content pull
            // doesn't leave synced_title stale and trigger a false rename next run.
            app["config"][format("synced_title_%d", idx)] = rtitle;
        }

        if (localChanged && remoteChanged)
        {
            // keep BOTH: copy the remote version into a free slot as its own
            // note, keep local canonical, push local so this pair converges.
            int freeS = free_slot();
            if (freeS >= 0 && do_pull(app, freeS, driveId, rname, rmod))
            {
                app["config"][format("title_%d", freeS)] = title_from_name(rname) + " (conflict)";
                app["config"][format("title_manual_%d", freeS)] = true;
                app["config"].remove(format("drive_id_%d", freeS)); // its own file next sync
                app["config"].remove(format("synced_modified_%d", freeS));
                app["config"].remove(format("synced_hash_%d", freeS));
                app["config"].remove(format("synced_title_%d", freeS));
                app["config"][format("unsynced_%d", freeS)] = true;
            }
            if (sync_upload_index(app, idx, g_base))
                g_ok++;
            g_conflicts++;
        }
        else if (remoteChanged)
        {
            // Normally we defer pulling the OPEN file (don't clobber the live
            // buffer). When the user explicitly syncs this file (Ctrl+U), pull
            // it and flag a reload so the editor refreshes.
            if (isOpen && !g_allowOpenPull)
                return;
            if (do_pull(app, idx, driveId, rname, rmod))
            {
                g_ok++;
                if (isOpen)
                    app["sync_reload"] = idx;
            }
        }
        else if (localChanged)
        {
            if (sync_upload_index(app, idx, g_base))
                g_ok++;
        }
        else
        {
            app["config"][format("synced_modified_%d", idx)] = rmod;
            app["config"][format("synced_title_%d", idx)] = title_from_name(rname);
        }
    }
    else
    {
        // Mapped id absent from a FRESH listing → deleted on Drive. Only honor a
        // delete when we actually have a listing (g_haveRemote); never delete on
        // a failed list fetch (that would wipe everything).
        bool driveDeleted = haveId && g_haveRemote;

        if (driveDeleted && !localChanged)
        {
            if (!isOpen)
                delete_local_slot(app, idx); // honor remote delete (recoverable)
            // open + unchanged: skip, handled when not open
        }
        else if (driveDeleted && localChanged)
        {
            // local edited since last sync → edit beats delete: recreate on Drive
            app["config"].remove(idKey);
            app["config"].remove(format("synced_modified_%d", idx));
            if (sync_upload_index(app, idx, g_base))
                g_ok++;
        }
        else
        {
            // never synced, or no fresh listing → safe push (never blind-delete)
            if (sync_upload_index(app, idx, g_base))
                g_ok++;
        }
    }
}

static void fetch_list(const String &baseUrl)
{
    g_base = baseUrl;
    g_ok = 0;
    g_conflicts = 0;
    g_curA = 0;
    g_curB = 0;
    g_curT = 0;
    g_trashedN = 0;
    g_pushedN = 0;
    g_haveRemote = false;
    g_remote.clear();

    SyncHttp r = sync_http_get(append_param(baseUrl, "action=list"));
    if (r.code > 0 && r.code < 400)
    {
        DeserializationError e = deserializeJson(g_remote, r.body);
        if (!e && g_remote["status"] == "OK")
            g_haveRemote = true;
    }
}

// ============================================================================
// GitHub backend (Contents API). A second provider alongside Drive, selected by
// config.sync.provider == "git". Reuses the engine's helpers (hash_file, base64,
// free_slot) and stepwise globals; only the remote I/O + reconcile decisions
// differ. Per-slot remote state lives in gh_path_N (remote path) + gh_sha_N
// (blob sha at last sync), separate from Drive's drive_id_N.
//
// Milestone A: push only (create / update / rename). Two-way pull + conflict
// resolution land in Milestone B — marked [MB] below.
// ============================================================================

static const long GH_MAX_BYTES = 64 * 1024; // RAM guard: file+base64 held inline

static String gh_basename(const String &path); // defined below; used by gh_flush

struct GhCfg
{
    String owner, repo, branch, dir, token;
};

// Staged push set for one-commit-per-sync. Each changed slot uploads its blob
// then records an entry here; gh_flush() turns the whole set into ONE commit at
// the end of the sync, and only THEN applies each entry's sync state — so a
// failed flush leaves everything untouched and the next sync just retries.
struct GhPending
{
    int slot;
    String newPath; // path to write
    String blobSha; // its uploaded blob
    String oldPath; // path to delete (rename source), or ""
    String title;   // title to record
    String hash;    // local content hash to record as synced_hash
};
static GhPending g_pend[SLOT_MAX];
static int g_pendN = 0;

static GhCfg gh_load(JsonDocument &app)
{
    GhCfg c;
    JsonVariant g = app["config"]["sync"]["git"];
    c.owner = g["owner"] | "";
    c.repo = g["repo"] | "";
    c.branch = g["branch"] | "main";
    c.dir = g["path"] | "";
    c.token = g["token"] | "";
    if (c.branch.isEmpty() || c.branch == "null")
        c.branch = "main";
    while (c.dir.startsWith("/"))
        c.dir = c.dir.substring(1);
    while (c.dir.endsWith("/"))
        c.dir = c.dir.substring(0, c.dir.length() - 1);
    if (c.dir == "null")
        c.dir = "";
    return c;
}

bool sync_provider_is_git(JsonDocument &app)
{
    return app["config"]["sync"]["provider"].as<String>() == "git";
}

bool sync_git_config_valid(JsonDocument &app, String &err)
{
    GhCfg c = gh_load(app);
    if (c.owner.isEmpty() || c.owner == "null") { err = "GIT: owner missing"; return false; }
    if (c.repo.isEmpty() || c.repo == "null") { err = "GIT: repo missing"; return false; }
    if (c.token.isEmpty() || c.token == "null") { err = "GIT: token missing"; return false; }
    return true;
}

// urlEncode each path segment but keep the '/' separators.
static String gh_encode_path(const String &path)
{
    String out;
    int start = 0;
    for (int i = 0; i <= (int)path.length(); i++)
    {
        if (i == (int)path.length() || path[i] == '/')
        {
            out += urlEncode(path.substring(start, i));
            if (i < (int)path.length())
                out += '/';
            start = i + 1;
        }
    }
    return out;
}

static String gh_url(const GhCfg &c, const String &path)
{
    return "https://api.github.com/repos/" + c.owner + "/" + c.repo +
           "/contents/" + gh_encode_path(path);
}

static std::vector<String> gh_headers(const GhCfg &c)
{
    std::vector<String> h;
    h.push_back("Authorization: Bearer " + c.token);
    h.push_back("User-Agent: katibOS");
    h.push_back("Accept: application/vnd.github+json");
    h.push_back("X-GitHub-Api-Version: 2022-11-28");
    h.push_back("Content-Type: application/json");
    return h;
}

static String gh_path_for_title(const GhCfg &c, const String &title)
{
    String name = title + ".txt";
    return c.dir.isEmpty() ? name : c.dir + "/" + name;
}

// List the repo dir into g_remote = {files:[{path,sha,name}]}. A 404 (empty
// repo / dir not created yet) is a VALID empty remote. Auth/other errors leave
// g_haveRemote false and set sync_error.
static void gh_fetch_list(JsonDocument &app)
{
    g_ok = 0;
    g_conflicts = 0;
    g_curA = 0;
    g_curB = 0;
    g_curT = 0;
    g_trashedN = 0;
    g_pushedN = 0;
    g_pendN = 0; // staged push set for the one-commit-per-sync flush
    g_haveRemote = false;
    g_remote.clear();
    g_remote["files"].to<JsonArray>();

    GhCfg c = gh_load(app);
    String url = gh_url(c, c.dir) + "?ref=" + urlEncode(c.branch);
    SyncHttp r = sync_http("GET", url, gh_headers(c), "");

    if (r.code == 404)
    {
        g_haveRemote = true; // empty but valid
        return;
    }
    if (r.code > 0 && r.code < 400)
    {
        JsonDocument arr;
        if (!deserializeJson(arr, r.body) && arr.is<JsonArray>())
        {
            JsonArray files = g_remote["files"].as<JsonArray>();
            for (JsonVariant v : arr.as<JsonArray>())
            {
                if (String(v["type"] | "") != "file")
                    continue;
                String p = v["path"].as<String>();
                if (!p.endsWith(".txt"))
                    continue;
                JsonObject o = files.add<JsonObject>();
                o["path"] = p;
                o["sha"] = v["sha"].as<String>();
                o["name"] = v["name"].as<String>();
            }
            g_haveRemote = true;
        }
    }
    else
    {
        app["sync_error"] = format("GIT list failed (HTTP %d)", r.code);
    }
}

static bool gh_find(const String &path, String &sha)
{
    if (!g_haveRemote)
        return false;
    for (JsonVariant v : g_remote["files"].as<JsonArray>())
        if (v["path"].as<String>() == path)
        {
            sha = v["sha"].as<String>();
            return true;
        }
    return false;
}

// Is `path` already held by a DIFFERENT file (remote listing or pushed earlier
// this run)? selfPath is the slot's own tracked path, which doesn't count.
static bool gh_path_taken(const String &path, const String &selfPath)
{
    if (path == selfPath)
        return false;
    if (g_haveRemote)
        for (JsonVariant v : g_remote["files"].as<JsonArray>())
            if (v["path"].as<String>() == path)
                return true;
    for (int i = 0; i < g_pushedN; i++)
        if (g_pushedNames[i] == path)
            return true;
    return false;
}

// base64 the whole slot file into memory. false if missing/empty/too-large.
static bool gh_b64_file(const String &path, String &out, long &size)
{
    File f = gfs()->open(path.c_str(), "r");
    if (!f)
        return false;
    size = (long)f.size();
    if (size == 0 || size > GH_MAX_BYTES)
    {
        f.close();
        return false;
    }
    out = "";
    const int bufferSize = 900; // multiple of 3 → clean chunk concatenation
    uint8_t buffer[bufferSize];
    while (f.available())
    {
        size_t n = f.read(buffer, bufferSize);
        if (n > 0)
            out += b64_chunk(buffer, n);
    }
    f.close();
    return true;
}

// Upload one blob, return its git sha (or "" on failure). Used when staging a
// changed file for the batched per-sync commit (gh_flush).
static String gh_create_blob(const GhCfg &c, const String &b64)
{
    JsonDocument body;
    body["content"] = b64;
    body["encoding"] = "base64";
    String payload;
    serializeJson(body, payload);
    String api = "https://api.github.com/repos/" + c.owner + "/" + c.repo + "/git/blobs";
    SyncHttp r = sync_http("POST", api, gh_headers(c), payload);
    if (r.code < 200 || r.code >= 400)
        return "";
    JsonDocument d;
    if (deserializeJson(d, r.body))
        return "";
    return d["sha"].as<String>();
}

static int gh_get(const GhCfg &c, const String &path, String &contentB64, String &sha)
{
    String url = gh_url(c, path) + "?ref=" + urlEncode(c.branch);
    SyncHttp r = sync_http("GET", url, gh_headers(c), "");
    if (r.code > 0 && r.code < 400)
    {
        JsonDocument d;
        if (!deserializeJson(d, r.body))
        {
            contentB64 = d["content"].as<String>(); // base64, may contain newlines
            sha = d["sha"].as<String>();
        }
    }
    return r.code;
}

// Move a slot aside to a recoverable backup and clear its git sync state.
static void gh_delete_local_slot(JsonDocument &app, int idx)
{
    String path = format("/%d.txt", idx);
    String backup = path + "_backup.txt";
    gfs()->remove(backup.c_str());
    gfs()->rename(path.c_str(), backup.c_str());
    app["config"].remove(format("gh_path_%d", idx));
    app["config"].remove(format("gh_sha_%d", idx));
    app["config"].remove(format("synced_hash_%d", idx));
    app["config"].remove(format("synced_title_%d", idx));
    app["config"].remove(format("title_%d", idx));
    app["config"].remove(format("title_manual_%d", idx));
    app["config"][format("unsynced_%d", idx)] = false;
}

// Single-commit rename via the Git Data API (blob -> tree -> commit -> ref), so
// git rename-detection links old->new and the file's history stays continuous.
// Adds newPath with the given content and (if deleteOld) removes oldPath in the
// SAME commit. Returns the new blob sha, or "" on any failure.
// ---- batched push: one commit per sync -------------------------------------
// Apply one staged entry's sync state (called after the commit succeeds).
static void gh_apply_pending(JsonDocument &app, const GhPending &p)
{
    app["config"][format("gh_path_%d", p.slot)] = p.newPath;
    app["config"][format("gh_sha_%d", p.slot)] = p.blobSha;
    app["config"][format("synced_hash_%d", p.slot)] = p.hash;
    app["config"][format("synced_title_%d", p.slot)] = p.title;
    app["config"][format("unsynced_%d", p.slot)] = false;
    int day = clock_localday();
    if (day > 0)
        app["config"][format("synced_day_%d", p.slot)] = day;
}

// Commit every staged change in ONE commit (blobs are already uploaded). Builds
// a tree off the current head (adds + sha:null deletes), commits, advances the
// ref. Handles an empty repo (no branch yet) by creating the initial ref.
static bool gh_flush(JsonDocument &app)
{
    if (g_pendN == 0)
        return true;

    GhCfg c = gh_load(app);
    String api = "https://api.github.com/repos/" + c.owner + "/" + c.repo + "/git/";
    std::vector<String> H = gh_headers(c);

    // current head + base tree (absent on a repo with no commits yet)
    String headSha, baseTree;
    SyncHttp r = sync_http("GET", api + "ref/heads/" + urlEncode(c.branch), H, "");
    bool emptyRepo = (r.code == 404 || r.code == 409);
    if (!emptyRepo)
    {
        if (r.code < 200 || r.code >= 400)
        {
            app["sync_error"] = format("GIT commit failed (ref %d)", r.code);
            return false;
        }
        JsonDocument dref;
        if (deserializeJson(dref, r.body))
            return false;
        headSha = dref["object"]["sha"].as<String>();
        r = sync_http("GET", api + "commits/" + headSha, H, "");
        if (r.code < 200 || r.code >= 400)
            return false;
        JsonDocument dcm;
        if (deserializeJson(dcm, r.body))
            return false;
        baseTree = dcm["tree"]["sha"].as<String>();
    }

    // one tree: every staged add, plus rename-source deletes (sha:null)
    String treeSha;
    {
        JsonDocument t;
        if (!emptyRepo && !baseTree.isEmpty())
            t["base_tree"] = baseTree;
        JsonArray arr = t["tree"].to<JsonArray>();
        for (int i = 0; i < g_pendN; i++)
        {
            if (!g_pend[i].newPath.isEmpty()) // delete-only entry has no add
            {
                JsonObject add = arr.add<JsonObject>();
                add["path"] = g_pend[i].newPath;
                add["mode"] = "100644";
                add["type"] = "blob";
                add["sha"] = g_pend[i].blobSha;
            }
            if (!g_pend[i].oldPath.isEmpty())
            {
                JsonObject del = arr.add<JsonObject>();
                del["path"] = g_pend[i].oldPath;
                del["mode"] = "100644";
                del["type"] = "blob";
                del["sha"] = static_cast<const char *>(nullptr); // null => delete
            }
        }
        String body;
        serializeJson(t, body);
        r = sync_http("POST", api + "trees", H, body);
    }
    if (r.code < 200 || r.code >= 400)
    {
        app["sync_error"] = format("GIT commit failed (tree %d)", r.code);
        return false;
    }
    JsonDocument dtr;
    if (deserializeJson(dtr, r.body))
        return false;
    treeSha = dtr["sha"].as<String>();
    if (treeSha.isEmpty())
        return false;

    // one commit
    String commitSha;
    {
        JsonDocument cm;
        cm["message"] = (g_pendN == 1)
                            ? (g_pend[0].newPath.isEmpty()
                                   ? format("katibOS: delete %s", gh_basename(g_pend[0].oldPath).c_str())
                                   : format("katibOS: %s.txt", g_pend[0].title.c_str()))
                            : format("katibOS: sync %d notes", g_pendN);
        cm["tree"] = treeSha;
        if (!emptyRepo && !headSha.isEmpty())
        {
            JsonArray par = cm["parents"].to<JsonArray>();
            par.add(headSha);
        }
        String body;
        serializeJson(cm, body);
        r = sync_http("POST", api + "commits", H, body);
    }
    if (r.code < 200 || r.code >= 400)
    {
        app["sync_error"] = format("GIT commit failed (commit %d)", r.code);
        return false;
    }
    JsonDocument dco;
    if (deserializeJson(dco, r.body))
        return false;
    commitSha = dco["sha"].as<String>();
    if (commitSha.isEmpty())
        return false;

    // advance (or create) the branch ref
    if (emptyRepo)
    {
        JsonDocument u;
        u["ref"] = "refs/heads/" + c.branch;
        u["sha"] = commitSha;
        String body;
        serializeJson(u, body);
        r = sync_http("POST", api + "refs", H, body);
    }
    else
    {
        JsonDocument u;
        u["sha"] = commitSha;
        u["force"] = false;
        String body;
        serializeJson(u, body);
        r = sync_http("PATCH", api + "refs/heads/" + urlEncode(c.branch), H, body);
    }
    if (r.code < 200 || r.code >= 400)
    {
        app["sync_error"] = format("GIT commit failed (ref-update %d)", r.code);
        return false;
    }

    // commit landed — now it's safe to record the new sync state
    for (int i = 0; i < g_pendN; i++)
        if (g_pend[i].slot >= 0) // delete-only entries map to no slot
            gh_apply_pending(app, g_pend[i]);
    return true;
}

// Push one slot to GitHub. A plain create/update goes via the Contents API (one
// commit); a rename goes via gh_rename_commit (one commit, history-preserving).
static bool gh_push_slot(JsonDocument &app, int idx)
{
    GhCfg c = gh_load(app);
    String path = format("/%d.txt", idx);

    String b64;
    long sz = 0;
    if (!gh_b64_file(path, b64, sz))
    {
        if (sz > GH_MAX_BYTES)
            app["sync_error"] = format("File %d too large for git (%ld bytes)", idx, sz);
        return false;
    }

    String title = app["config"][format("title_%d", idx)].as<String>();
    title.replace("/", " ");
    title.replace("\\", " ");
    title.trim();
    if (title.isEmpty() || title == "null")
        title = String(idx);

    String oldPath = app["config"][format("gh_path_%d", idx)].as<String>();
    bool haveOld = !oldPath.isEmpty() && oldPath != "null";
    String newPath = gh_path_for_title(c, title);

    // a different note already holds this path → disambiguate, baking the suffix
    // into the title so device/repo/vault all agree (mirrors Drive).
    if (gh_path_taken(newPath, haveOld ? oldPath : String("")))
    {
        title = title + "-" + String(idx);
        app["config"][format("title_%d", idx)] = title;
        app["config"][format("title_manual_%d", idx)] = true;
        newPath = gh_path_for_title(c, title);
    }
    if (g_pushedN < SLOT_MAX)
        g_pushedNames[g_pushedN++] = newPath;

    bool isRename = haveOld && oldPath != newPath;

    // Blob for the new path: a PURE rename (body unchanged since last sync) reuses
    // the existing blob — no upload; otherwise upload the current content.
    String localHash = hash_file(path);
    String syncedHash = app["config"][format("synced_hash_%d", idx)].as<String>();
    String prevBlob = app["config"][format("gh_sha_%d", idx)].as<String>();
    String blobSha;
    if (!syncedHash.isEmpty() && localHash == syncedHash &&
        !prevBlob.isEmpty() && prevBlob != "null")
    {
        blobSha = prevBlob;
    }
    else
    {
        blobSha = gh_create_blob(c, b64);
        if (blobSha.isEmpty())
        {
            app["sync_error"] = format("GIT stage slot %d failed", idx);
            return false;
        }
    }

    // on a rename, delete the old path in the same commit (only if it still exists)
    String oldDel;
    if (isRename)
    {
        String s;
        if (gh_find(oldPath, s))
            oldDel = oldPath;
    }

    // stage it — gh_flush() commits the whole set in ONE commit at end of sync.
    // State (gh_path/gh_sha/...) is recorded only after that commit lands.
    if (g_pendN < SLOT_MAX)
    {
        g_pend[g_pendN] = {idx, newPath, blobSha, oldDel, title, localHash};
        g_pendN++;
    }
    return true;
}

static String gh_basename(const String &path)
{
    int s = path.lastIndexOf('/');
    return (s >= 0) ? path.substring(s + 1) : path;
}

static int slot_for_gh_path(JsonDocument &app, const String &path)
{
    for (int i = 0; i < SLOT_MAX; i++)
        if (app["config"][format("gh_path_%d", i)].as<String>() == path)
            return i;
    return -1;
}

// Pull a repo file into a slot (atomic temp-swap), recording git sync state.
static bool gh_pull(JsonDocument &app, int slot, const String &path)
{
    GhCfg c = gh_load(app);
    String b64, sha;
    if (gh_get(c, path, b64, sha) >= 400 || b64.isEmpty())
        return false;

    String local = format("/%d.txt", slot);
    String tmp = local + ".tmp";
    File out = gfs()->open(tmp.c_str(), "w");
    if (!out)
        return false;
    b64_decode_to_file(b64, out);
    out.close();
    delay(50);

    gfs()->remove(local.c_str());
    gfs()->rename(tmp.c_str(), local.c_str());

    String title = title_from_name(gh_basename(path));
    if (title.length())
    {
        app["config"][format("title_%d", slot)] = title;
        app["config"][format("title_manual_%d", slot)] = true;
    }
    app["config"][format("gh_path_%d", slot)] = path;
    app["config"][format("gh_sha_%d", slot)] = sha;
    app["config"][format("synced_hash_%d", slot)] = hash_file(local);
    app["config"][format("synced_title_%d", slot)] = title;
    app["config"][format("unsynced_%d", slot)] = false;
    return true;
}

// reconcile one slot against GitHub: push / pull / rename / keep-both conflict /
// honor remote delete. Remote-change is detected by blob sha.
static void gh_reconcile_slot(JsonDocument &app, int idx)
{
    String path = format("/%d.txt", idx);
    String localHash = hash_file(path);
    String syncedHash = app["config"][format("synced_hash_%d", idx)].as<String>();
    bool localChanged = syncedHash.isEmpty() || syncedHash == "null" || localHash != syncedHash;

    String title = app["config"][format("title_%d", idx)].as<String>();
    bool haveSyncedTitle = app["config"][format("synced_title_%d", idx)].is<const char *>();
    String syncedTitle = haveSyncedTitle
                             ? app["config"][format("synced_title_%d", idx)].as<String>()
                             : title;
    bool localRenamed = (title != syncedTitle);

    String storedPath = app["config"][format("gh_path_%d", idx)].as<String>();
    bool haveStored = !storedPath.isEmpty() && storedPath != "null";

    if (!haveStored)
    {
        if (gh_push_slot(app, idx))
            g_ok++;
        return;
    }

    int fileIndex = app["config"]["file_index"].as<int>();
    bool isOpen = (idx == fileIndex);

    String curSha;
    bool remoteExists = gh_find(storedPath, curSha);

    if (remoteExists)
    {
        String storedSha = app["config"][format("gh_sha_%d", idx)].as<String>();
        bool remoteChanged = (curSha != storedSha);

        if (localChanged && remoteChanged)
        {
            // keep BOTH (mirror Drive): copy the remote version into a free slot
            // as its own note, keep local canonical, push local.
            int freeS = free_slot();
            if (freeS >= 0 && gh_pull(app, freeS, storedPath))
            {
                app["config"][format("title_%d", freeS)] =
                    title_from_name(gh_basename(storedPath)) + " (conflict)";
                app["config"][format("title_manual_%d", freeS)] = true;
                app["config"].remove(format("gh_path_%d", freeS)); // its own file next sync
                app["config"].remove(format("gh_sha_%d", freeS));
                app["config"][format("unsynced_%d", freeS)] = true;
            }
            if (gh_push_slot(app, idx))
                g_ok++;
            g_conflicts++;
        }
        else if (remoteChanged)
        {
            // defer pulling the OPEN file unless this is an explicit Ctrl+U.
            if (isOpen && !g_allowOpenPull)
                return;
            if (gh_pull(app, idx, storedPath))
            {
                g_ok++;
                if (isOpen)
                    app["sync_reload"] = idx;
            }
        }
        else if (localChanged || localRenamed)
        {
            if (gh_push_slot(app, idx))
                g_ok++;
        }
        // else: in sync, nothing to do.
    }
    else
    {
        // stored path absent from a FRESH listing → deleted (or renamed) on the
        // remote. Honor a delete only when we actually have a listing.
        bool remoteDeleted = g_haveRemote;
        if (remoteDeleted && !localChanged)
        {
            if (!isOpen)
                gh_delete_local_slot(app, idx); // recoverable backup
        }
        else if (remoteDeleted && localChanged)
        {
            // local edited since last sync → edit beats delete: recreate.
            app["config"].remove(format("gh_path_%d", idx));
            app["config"].remove(format("gh_sha_%d", idx));
            if (gh_push_slot(app, idx))
                g_ok++;
        }
        else
        {
            // no fresh listing → safe push (never blind-delete)
            if (gh_push_slot(app, idx))
                g_ok++;
        }
    }
}

void sync_begin(JsonDocument &app, const String &baseUrl)
{
    g_scopeOne = false;
    g_allowOpenPull = false;
    g_isGit = sync_provider_is_git(app);
    if (g_isGit)
        gh_fetch_list(app);
    else
        fetch_list(baseUrl);

    g_localN = 0;
    for (int i = 0; i < SLOT_MAX; i++)
        if (gfs()->exists(format("/%d.txt", i).c_str()))
            g_local[g_localN++] = i;

    g_active = true;
}

void sync_begin_one(JsonDocument &app, const String &baseUrl, int slot)
{
    g_scopeOne = true;
    g_allowOpenPull = true; // this IS the file the user is syncing
    g_isGit = sync_provider_is_git(app);
    if (g_isGit)
        gh_fetch_list(app);
    else
        fetch_list(baseUrl);

    g_localN = 0;
    if (slot >= 0 && gfs()->exists(format("/%d.txt", slot).c_str()))
        g_local[g_localN++] = slot;

    g_active = true;
}

bool sync_step(JsonDocument &app)
{
    if (!g_active)
        return false;

    // GitHub backend: reconcile each local slot, then pull repo files not mapped
    // to any slot (skipped for a single-file Ctrl+U sync). No trash phase — git
    // honors deletes via the missing-path branch in gh_reconcile_slot.
    if (g_isGit)
    {
        // phase T(git): delete files the device removed (tombstones left by
        // Clear). Each becomes a delete-only entry in the batched commit, and
        // its path is recorded so the pull phase below won't re-pull it. Gated
        // on a fresh listing so a failed fetch never drops the tombstones.
        // Skipped for a single-file sync (that's full-sync housekeeping).
        if (!g_scopeOne && g_haveRemote)
        {
            JsonArray tr = app["config"]["sync_trash_git"].as<JsonArray>();
            if (!tr.isNull())
            {
                int n = (int)tr.size();
                if (g_curT < n)
                {
                    app["sync_message"] = "Removing deleted ...";
                    app["sync_state"] = SYNC_PROGRESS;
                    app["clear"] = true;
                    String p = tr[g_curT].as<String>();
                    String sha;
                    if (!p.isEmpty() && p != "null" && gh_find(p, sha))
                    {
                        if (g_pendN < SLOT_MAX)
                            g_pend[g_pendN++] = {-1, "", "", p, "", ""};
                        if (g_trashedN < SLOT_MAX)
                            g_trashed[g_trashedN++] = p; // don't re-pull below
                    }
                    g_curT++;
                    if (g_curT >= n)
                        app["config"].remove("sync_trash_git"); // all processed
                    return true;
                }
            }
        }

        if (g_curA < g_localN)
        {
            app["sync_message"] = format("Sync %d/%d ...", g_curA + 1, g_localN);
            app["sync_state"] = SYNC_PROGRESS;
            app["clear"] = true;
            gh_reconcile_slot(app, g_local[g_curA]);
            g_curA++;
            return true;
        }

        if (g_haveRemote && !g_scopeOne)
        {
            JsonArray files = g_remote["files"].as<JsonArray>();
            int n = (int)files.size();
            while (g_curB < n)
            {
                String p = files[g_curB++]["path"].as<String>();
                if (slot_for_gh_path(app, p) >= 0 || was_trashed(p))
                    continue; // already a local slot, or just trashed this run
                int slot = free_slot();
                if (slot < 0)
                {
                    app["sync_error"] = "Device full - some repo notes not pulled";
                    continue;
                }
                app["sync_message"] = "Pulling new file ...";
                app["sync_state"] = SYNC_PROGRESS;
                app["clear"] = true;
                gh_pull(app, slot, p);
                return true; // one per step
            }
        }

        // all slots reconciled — commit every staged change in ONE commit.
        if (g_pendN > 0)
        {
            app["sync_message"] = format("Saving %d change(s) ...", g_pendN);
            app["sync_state"] = SYNC_PROGRESS;
            app["clear"] = true;
            gh_flush(app);
            g_pendN = 0; // consumed (success applied state; failure set sync_error)
        }

        g_active = false;
        return false;
    }

    // phase T: trash files the device deleted (tombstones left by Clear).
    // Skipped for a single-file sync (that's full-sync housekeeping).
    if (!g_scopeOne)
    {
        JsonArray tr = app["config"]["sync_trash"].as<JsonArray>();
        if (!tr.isNull())
        {
            int n = (int)tr.size();
            if (g_curT < n)
            {
                app["sync_message"] = "Removing deleted ...";
                app["sync_state"] = SYNC_PROGRESS;
                app["clear"] = true;
                String id = tr[g_curT].as<String>();
                if (!id.isEmpty() && id != "null")
                {
                    drive_trash(id);
                    if (g_trashedN < SLOT_MAX)
                        g_trashed[g_trashedN++] = id; // don't re-pull it below
                }
                g_curT++;
                if (g_curT >= n)
                    app["config"].remove("sync_trash"); // all processed
                return true;
            }
        }
    }

    if (g_curA < g_localN)
    {
        app["sync_message"] = format("Sync %d/%d ...", g_curA + 1, g_localN);
        app["sync_state"] = SYNC_PROGRESS;
        app["clear"] = true;
        reconcile_slot(app, g_local[g_curA]);
        g_curA++;
        return true;
    }

    // pull NEW Drive files (not mapped to any slot) into free slots.
    // Skipped for a single-file sync — that only touches the one slot.
    if (g_haveRemote && !g_scopeOne)
    {
        JsonArray files = g_remote["files"].as<JsonArray>();
        int n = (int)files.size();
        while (g_curB < n)
        {
            JsonVariant v = files[g_curB++];
            String id = v["id"].as<String>();
            if (slot_for_id(app, id) >= 0 || was_trashed(id))
                continue; // already a local slot, or just trashed this run
            int slot = free_slot();
            if (slot < 0)
            {
                app["sync_error"] = "Device full - some Drive notes not pulled";
                continue;
            }
            app["sync_message"] = "Pulling new file ...";
            app["sync_state"] = SYNC_PROGRESS;
            app["clear"] = true;
            do_pull(app, slot, id, v["name"].as<String>(), v["modified"] | 0LL);
            return true; // one per step
        }
    }

    g_active = false;
    return false;
}

void sync_result(int &ok, int &total, int &conflicts)
{
    ok = g_ok;
    total = g_localN;
    conflicts = g_conflicts;
}

// device convenience: run the whole pass in one (blocking) call
bool sync_reconcile(JsonDocument &app, const String &baseUrl, int &ok, int &total)
{
    sync_begin(app, baseUrl);
    while (sync_step(app))
    {
    }
    int conflicts = 0;
    sync_result(ok, total, conflicts);
    return true;
}

// device convenience: sync only one slot (blocking).
bool sync_reconcile_one(JsonDocument &app, const String &baseUrl, int slot, int &ok, int &total)
{
    sync_begin_one(app, baseUrl, slot);
    while (sync_step(app))
    {
    }
    int conflicts = 0;
    sync_result(ok, total, conflicts);
    return true;
}
