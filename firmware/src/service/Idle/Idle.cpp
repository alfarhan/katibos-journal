#include "Idle.h"
#include "app/app.h"
#include "service/Sync/Sync.h"
#include "service/Ai/Ai.h"

void idle_sleep_check(unsigned long quietMs); // defined with the sleep stages below

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

    unsigned long quiet = millis() - g_lastActivity;

    if (g_active)
    {
        idle_sleep_check(quiet); // deeper stages, once the panel is already down
        return;
    }

    if (quiet < (unsigned long)timeout * 1000UL)
        return;

    g_active = true;
    if (g_onEnter)
        g_onEnter();
    _log("[idle] throttled after %ds\n", timeout);
}

// ---- sleep (Tier 2 / Tier 3) -----------------------------------------------
#include "keyboard/Keypad/68/keypad_68.h"
#include "service/Editor/Editor.h"

#if defined(BOARD_ESP32_S3) && !defined(USE_SERIAL_KEYBOARD) && !defined(USE_BLE_KEYBOARD_HOST)
#define IDLE_CAN_SLEEP 1
#include <esp_sleep.h>
#endif

bool sleep_supported()
{
#if defined(IDLE_CAN_SLEEP)
    return true;
#elif defined(HOST_EMU)
    // The emulator has no chip to sleep, but the Preferences rows still need to
    // be previewable - this is the only place the rev_8-only UI can be seen.
    return true;
#else
    return false;
#endif
}

// Both default to 0: a device must be told to sleep. Getting a wake source wrong
// looks exactly like a dead device, so this is not something to opt users into.
int sleep_light_sec()
{
    if (!sleep_supported())
        return 0;
    JsonDocument &app = status();
    if (!app["config"]["sleep_light_secs"].is<int>())
        return 0;
    int s = app["config"]["sleep_light_secs"].as<int>();
    return s < 0 ? 0 : s;
}

int sleep_deep_sec()
{
    if (!sleep_supported())
        return 0;
    JsonDocument &app = status();
    if (!app["config"]["sleep_deep_secs"].is<int>())
        return 0;
    int s = app["config"]["sleep_deep_secs"].as<int>();
    return s < 0 ? 0 : s;
}

#ifdef IDLE_CAN_SLEEP
// Tier 2. Returns after the chip wakes, having lost nothing.
static void enterLightSleep()
{
    _log("[idle] light sleep\n");

    keypad_prepare_wake();
    unsigned long long mask = keypad_wake_mask();
    for (int gpio = 0; gpio < 48; gpio++)
        if (mask & (1ULL << gpio))
            gpio_wakeup_enable((gpio_num_t)gpio, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    esp_light_sleep_start(); // both cores stop here; execution resumes below

    // Undo the wake wiring before the scanner touches the pins again, or it reads
    // a matrix that is still being held HIGH by the columns.
    for (int gpio = 0; gpio < 48; gpio++)
        if (mask & (1ULL << gpio))
            gpio_wakeup_disable((gpio_num_t)gpio);
    keypad_resume_scan();

    _log("[idle] woke from light sleep\n");
}

// Tier 3. Does not return - the chip restarts from setup() on the next keypress.
static void enterDeepSleep()
{
    _log("[idle] saving before deep sleep\n");

    // RAM is about to be lost, so the document has to be on disk. The caret is
    // written by saveFile(), and loadFile() restores it on the next boot, so this
    // comes back exactly where the writer left off.
    Editor::getInstance().saveFile();

    keypad_prepare_wake();
    esp_sleep_enable_ext1_wakeup(keypad_wake_mask(), ESP_EXT1_WAKEUP_ANY_HIGH);

    _log("[idle] deep sleep\n");
    delay(50); // let the log drain before the UART dies with the chip
    esp_deep_sleep_start();
}
#endif

// Called from idle_loop() once the panel is already throttled. Staged: the light
// timeout must have passed before the deep one is even considered, so a device
// configured for both naps first and only shuts down after a longer silence.
void idle_sleep_check(unsigned long quietMs)
{
#ifdef IDLE_CAN_SLEEP
    int deep = sleep_deep_sec();
    if (deep > 0 && quietMs >= (unsigned long)deep * 1000UL)
    {
        enterDeepSleep(); // never returns
        return;
    }

    int light = sleep_light_sec();
    if (light > 0 && quietMs >= (unsigned long)light * 1000UL)
    {
        enterLightSleep();
        idle_touch(); // waking IS activity: full refresh, restart the countdown
    }
#else
    (void)quietMs;
#endif
}
