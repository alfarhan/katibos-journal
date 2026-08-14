#include "Ai.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Sync/SyncCore.h" // sync_http (the shared TLS/HTTP seam)
#include "service/Editor/Editor.h"
#include "service/Tools/Tools.h"

#ifndef HOST_EMU
#include <WiFi.h>
#include "service/Sync/Sync.h"           // sync_connect_wifi / sync_stop
#include "service/WifiEntry/WifiEntry.h" // wifi_config_load
#endif

static const char *AI_DEFAULT_MODEL = "gemini-2.5-flash";

// The instruction is deliberately narrow. This is a writing device: the model is
// here to fix misspellings, not to edit prose. Arabic is the risky case - models
// love to normalize alef/hamza forms and to add or drop harakat, which silently
// rewrites the author's choices - so that is forbidden explicitly.
static const char *AI_PROMPT =
    "Fix ONLY misspelled words in the text below. Obey every rule:\n"
    "1. Do not rephrase, reorder, translate, summarize or improve anything.\n"
    "2. Keep every line break, blank line, space and punctuation mark as-is.\n"
    "3. For Arabic: never change alef/hamza forms, never add or remove harakat,\n"
    "   never switch a word for a synonym. Fix only genuine spelling errors.\n"
    "4. Keep the original language of every word. Do not translate.\n"
    "5. Reply with the corrected text ONLY - no preamble, no explanation, no\n"
    "   markdown fences, no quotes around it.\n"
    "\n"
    "TEXT:\n";

// ---- progress ---------------------------------------------------------------
// Nothing renders while this pass holds the background core, so each stage marks
// the screen dirty itself; the editor's status bar picks up ai_message.
static void aiStage(const String &msg)
{
    JsonDocument &app = status();
    app["ai_state"] = AI_RUNNING;
    app["ai_message"] = msg;
    app["clear"] = true;
    _log("[ai] %s\n", msg.c_str());
}

static void aiFail(const String &msg)
{
    JsonDocument &app = status();
    app["ai_state"] = AI_ERROR;
    app["ai_message"] = msg;
    app["clear"] = true;
    _log("[ai] FAILED: %s\n", msg.c_str());
}

// ---- config ----------------------------------------------------------------
bool ai_configured(JsonDocument &app)
{
    String k = app["config"]["ai"]["key"].as<String>();
    return !k.isEmpty() && k != "null";
}

static String ai_model(JsonDocument &app)
{
    String m = app["config"]["ai"]["model"].as<String>();
    if (m.isEmpty() || m == "null")
        m = AI_DEFAULT_MODEL;
    return m;
}

// ---- wifi ------------------------------------------------------------------
static bool ai_ensure_wifi(JsonDocument &app)
{
#ifdef HOST_EMU
    (void)app;
    return true;
#else
    if (WiFi.status() == WL_CONNECTED)
        return true;

    wifi_config_load();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("MICROJOURNAL");
    setCpuFrequencyMhz(CPU_FREQUENCY_FULL); // the radio needs >= 80MHz
    delay(2000);

    int n = WiFi.scanNetworks();
    JsonArray saved = app["wifi"]["access_points"].as<JsonArray>();
    for (int i = 0; i < n; i++)
    {
        String ssid = WiFi.SSID(i);
        for (JsonVariant ap : saved)
            if (ap["ssid"].as<String>() == ssid)
            {
                String pw = ap["password"].as<String>();
                if (sync_connect_wifi(app, ssid.c_str(), pw.c_str()))
                    return true;
            }
    }
    return false;
#endif
}

static void ai_wifi_done()
{
#ifndef HOST_EMU
    sync_stop(); // powers the radio down and drops the CPU back to the low clock
#endif
}

// ---- file I/O --------------------------------------------------------------
static bool ai_read_file(const String &path, String &out, long &size)
{
    File f = gfs()->open(path.c_str(), "r");
    if (!f)
        return false;
    size = (long)f.size();
    if (size <= 0 || size > AI_MAX_BYTES)
    {
        f.close();
        return false;
    }
    out = f.readString();
    f.close();
    return (long)out.length() > 0;
}

// Strip what the model adds despite being told not to: a ```-fenced block, and
// surrounding blank lines. Anything else (a preamble sentence) is caught by the
// length guard instead - silently trimming prose would be worse than refusing.
static void ai_strip_fences(String &s)
{
    // Only touch the text if it really IS a fenced block. Trimming
    // unconditionally would silently eat trailing blank lines the writer meant
    // to keep - a spelling pass must not reflow the document.
    String probe = s;
    probe.trim();
    if (!probe.startsWith("```"))
        return;
    s = probe;

    int firstNl = s.indexOf('\n');
    if (firstNl < 0)
        return;

    int end = (int)s.length();
    while (end > 0 && (s[end - 1] == '\n' || s[end - 1] == '\r' || s[end - 1] == ' '))
        end--;
    int fence = end;
    while (fence > 0 && s[fence - 1] == '`')
        fence--;
    if (end - fence < 3 || fence <= firstNl)
        return; // no closing fence - leave it for the length guard to judge

    // Drop the fence lines and exactly the ONE newline each contributes - the
    // newline before the closing fence is syntax, any further blank line is the
    // writer's.
    int inner = fence;
    if (inner > firstNl && s[inner - 1] == '\n')
        inner--;
    if (inner > firstNl && s[inner - 1] == '\r')
        inner--;
    s = s.substring(firstNl + 1, inner);
}

// ---- the pass --------------------------------------------------------------
static void ai_run()
{
    JsonDocument &app = status();

    if (!ai_configured(app))
    {
        aiFail("No AI key in config.json");
        return;
    }

    // The editor buffer is a window; the file on disk is the document. Flush it
    // first so we correct what the writer actually has.
    Editor::getInstance().saveFile();

    int idx = app["config"]["file_index"].as<int>();
    String path = format("/%d.txt", idx);

    String original;
    long size = 0;
    aiStage("Reading document ...");
    if (!ai_read_file(path, original, size))
    {
        File probe = gfs()->open(path.c_str(), "r");
        long sz = probe ? (long)probe.size() : 0;
        if (probe)
            probe.close();
        if (sz > AI_MAX_BYTES)
            aiFail(format("Too long for AI (%ld of %ld bytes)", sz, AI_MAX_BYTES));
        else
            aiFail("Nothing to correct");
        return;
    }

    aiStage("Connecting to WiFi ...");
    if (!ai_ensure_wifi(app))
    {
        aiFail("No WiFi - text unchanged");
        ai_wifi_done();
        return;
    }

    // Request body. ArduinoJson does the escaping, which matters: the document is
    // arbitrary user text full of quotes and newlines.
    aiStage("Asking Gemini ...");
    String payload;
    {
        JsonDocument body;
        JsonObject part = body["contents"][0]["parts"][0].to<JsonObject>();
        part["text"] = String(AI_PROMPT) + original;
        // temperature 0: we want the same correction every time, not creativity.
        body["generationConfig"]["temperature"] = 0;
        serializeJson(body, payload);
    }

    // config.ai.url overrides the endpoint (a local stand-in for testing); the
    // Google endpoint is the default, same shape as update.url.
    String url = app["config"]["ai"]["url"].as<String>();
    if (url.isEmpty() || url == "null")
        url = "https://generativelanguage.googleapis.com/v1beta/models/" +
              ai_model(app) + ":generateContent";
    std::vector<String> headers;
    headers.push_back("Content-Type: application/json");
    headers.push_back("x-goog-api-key: " + app["config"]["ai"]["key"].as<String>());

    SyncHttp r = sync_http("POST", url, headers, payload);
    payload = ""; // free the request before parsing the reply
    ai_wifi_done();

    if (r.code < 200 || r.code >= 400)
    {
        aiFail(format("AI request failed (HTTP %d)", r.code));
        return;
    }

    aiStage("Reading reply ...");
    String corrected;
    {
        JsonDocument reply;
        if (deserializeJson(reply, r.body))
        {
            aiFail("Could not read the AI reply");
            return;
        }
        if (reply["error"].is<JsonObject>())
        {
            aiFail(String("AI: ") + (reply["error"]["message"] | "refused"));
            return;
        }
        corrected = reply["candidates"][0]["content"]["parts"][0]["text"].as<String>();
    }
    r.body = "";

    if (corrected.isEmpty() || corrected == "null")
    {
        aiFail("AI returned nothing - text unchanged");
        return;
    }
    ai_strip_fences(corrected);

    // Length guard. A spelling pass changes a few characters; a reply that is
    // half or double the original means the model rewrote, translated, or
    // prepended commentary. Refusing beats overwriting the writer's work.
    long got = (long)corrected.length();
    if (got < size / 2 || got > size * 2)
    {
        aiFail(format("AI reply looks wrong (%ld vs %ld bytes) - kept yours", got, size));
        return;
    }
    if (corrected == original)
    {
        app["ai_state"] = AI_DONE;
        app["ai_message"] = "No spelling errors found";
        app["clear"] = true;
        return;
    }

    // Write to a sidecar, then swap. The original becomes the backup, which is
    // the only way back: this replace is far too big for the 2KB undo arena.
    aiStage("Saving ...");
    String tmp = path + ".ai.tmp";
    {
        File out = gfs()->open(tmp.c_str(), "w");
        if (!out)
        {
            aiFail("Could not write the corrected file");
            return;
        }
        out.print(corrected);
        out.close();
        delay(50);
    }

    String backup = path + "_backup.txt";
    gfs()->remove(backup.c_str());
    if (!gfs()->rename(path.c_str(), backup.c_str()))
    {
        gfs()->remove(tmp.c_str());
        aiFail("Could not back up the original");
        return;
    }
    if (!gfs()->rename(tmp.c_str(), path.c_str()))
    {
        gfs()->rename(backup.c_str(), path.c_str()); // put the original back
        aiFail("Could not replace the file - original restored");
        return;
    }

    // The file under the editor changed, so its window and cached counts are
    // stale. Ask the editor (running on the other core) to reload the slot; the
    // same hook the sync pull path uses.
    app["config"][format("unsynced_%d", idx)] = true;
    config_save();
    app["ai_reload"] = idx;
    app["ai_state"] = AI_DONE;
    app["ai_message"] = "Corrected - see _backup.txt";
    app["clear"] = true;
}

void ai_loop()
{
    JsonDocument &app = status();
    if (app["task"].as<String>() != "ai_correct")
        return;
    app["task"] = "";
    _log("[ai] picked up correction request\n");
    ai_run();
}

bool ai_request(JsonDocument &app)
{
    if (!ai_configured(app))
    {
        aiFail("No AI key in config.json");
        return false;
    }
    app["ai_state"] = AI_RUNNING;
    app["ai_message"] = "Starting ...";
    app["ai_reload"] = -1;
    app["clear"] = true;
    app["task"] = "ai_correct";
    return true;
}
