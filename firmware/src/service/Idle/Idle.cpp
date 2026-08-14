#include "Idle.h"
#include "app/app.h"
#include "service/Sync/Sync.h"
#include "service/Ai/Ai.h"

static void (*g_onEnter)() = nullptr;
static void (*g_onExit)() = nullptr;
static unsigned long g_lastActivity = 0;
static bool g_active = false;

// Default: a minute of stillness. Long enough not to fire between sentences
// while you think, short enough to matter over an afternoon.
static const int IDLE_DEFAULT_SEC = 60;

void idle_setup(void (*onEnter)(), void (*onExit)())
{
    g_onEnter = onEnter;
    g_onExit = onExit;
    g_lastActivity = millis();
    g_active = false;
}

int idle_timeout_sec()
{
    JsonDocument &app = status();
    if (!app["config"]["idle_secs"].is<int>())
        return IDLE_DEFAULT_SEC;
    int s = app["config"]["idle_secs"].as<int>();
    return s < 0 ? 0 : s;
}

bool idle_active() { return g_active; }

static void leaveIdle()
{
    if (!g_active)
        return;
    g_active = false;
    if (g_onExit)
        g_onExit();
    _log("[idle] awake\n");
}

void idle_touch()
{
    g_lastActivity = millis();
    leaveIdle();
}

// A network task owns the radio and blocks for seconds at a time without any
// keypresses; throttling the panel mid-sync would only add a repaint to work
// that is already waiting on Wi-Fi.
static bool busyWithTask(JsonDocument &app)
{
    // A missing key stringifies to "null", not "" - the idiom used throughout
    // this codebase. Testing only for length made every boot look busy.
    String task = app["task"].as<String>();
    if (task.length() && task != "null")
        return true;

    // SYNC_START is 0, which is also what an absent sync_state reads as, so the
    // key has to be checked for existence first or a device that has never synced
    // looks permanently mid-sync.
    if (app["sync_state"].is<int>())
    {
        int ss = app["sync_state"].as<int>();
        if (ss == SYNC_START || ss == SYNC_STARTED || ss == SYNC_PROGRESS)
            return true;
    }

    return (app["ai_state"] | AI_IDLE) == AI_RUNNING;
}

void idle_loop()
{
    JsonDocument &app = status();

    int timeout = idle_timeout_sec();
    if (timeout <= 0)
    {
        leaveIdle(); // switched off in Preferences while already throttled
        return;
    }

    if (busyWithTask(app))
    {
        idle_touch(); // keep it awake, and restart the countdown afterwards
        return;
    }

    if (g_active)
        return;

    if (millis() - g_lastActivity < (unsigned long)timeout * 1000UL)
        return;

    g_active = true;
    if (g_onEnter)
        g_onEnter();
    _log("[idle] throttled after %ds\n", timeout);
}
