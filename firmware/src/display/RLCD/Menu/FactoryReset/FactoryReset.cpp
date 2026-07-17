#include "FactoryReset.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Editor/Editor.h"

// Erase all notes + reset preferences. Connectivity (Wi-Fi creds, sync URL/git,
// keyboard layout, time zone, device name) is KEPT so the device stays usable.
// Two-stage confirm: first Y arms it, second Y erases — guards a hard slip.
static const int SLOT_MAX = 100;
static bool g_armed = false;

void FactoryReset_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    g_armed = false;
    Menu_clear();
}

void FactoryReset_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_drawHeader(display, u8, "FACTORY RESET");

    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(10, 70);
    u8->print("Erase ALL notes and reset settings?");
    u8->setCursor(10, 100);
    u8->print("Wi-Fi & sync stay configured.");

    u8->setCursor(10, 150);
    u8->print("This cannot be undone on the device.");
    u8->setCursor(10, 172);
    u8->print("Sync first to keep a safe copy.");

    if (g_armed)
    {
        display->drawFilledRectangle(8, 196, 392, 222, 1);
        u8->setForegroundColor(ST7305_COLOR_WHITE);
        u8->setBackgroundColor(ST7305_COLOR_BLACK);
        u8->setCursor(16, 215);
        u8->print("Press Y again to ERASE EVERYTHING");
        u8->setForegroundColor(ST7305_COLOR_BLACK);
        u8->setBackgroundColor(ST7305_COLOR_WHITE);
    }

    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(10, 296);
    u8->print(g_armed ? "[Y] ERASE      [B] cancel" : "[Y] continue   [B] back");
}

static void wipe(JsonDocument &app)
{
    for (int i = 0; i < SLOT_MAX; i++)
    {
        String p = format("/%d.txt", i);
        gfs()->remove(p.c_str());
        gfs()->remove((p + "_backup.txt").c_str());
        gfs()->remove((p + ".base64").c_str());
        const char *perSlot[] = {"title_%d", "title_manual_%d", "drive_id_%d",
                                 "gh_path_%d", "gh_sha_%d", "synced_modified_%d",
                                 "synced_hash_%d", "synced_title_%d", "synced_day_%d",
                                 "unsynced_%d"};
        for (auto k : perSlot)
            app["config"].remove(format(k, i));
    }

    // writing stats + tombstones
    const char *named[] = {"streak", "today_words", "session_words", "last_day", "sync_trash"};
    for (auto k : named)
        app["config"].remove(k);

    // preferences → remove so each falls back to its default
    const char *prefs[] = {"theme_dark", "line_spacing", "scroll_mode", "text_align",
                           "statusbar_hidden", "clock_24h", "goal_enabled", "daily_goal",
                           "wakeup_animation_disabled"};
    for (auto k : prefs)
        app["config"].remove(k);

    // a fresh empty first note
    app["config"]["file_index"] = 0;
    File f = gfs()->open("/0.txt", "w");
    if (f)
        f.close();
    config_save();
    Editor::getInstance().loadFile("/0.txt");
}

void FactoryReset_keyboard(int key)
{
    JsonDocument &app = status();

    if (key == 'Y' || key == 'y')
    {
        if (!g_armed)
        {
            g_armed = true; // arm; require a second Y
            Menu_clear();
            return;
        }
        wipe(app);
        g_armed = false;
        app["screen"] = WORDPROCESSOR; // drop into the fresh editor
        return;
    }

    // anything else (B / Esc / etc.) cancels
    g_armed = false;
    app["menu"]["state"] = app["menu"]["return"] | MENU_PREFS;
}
