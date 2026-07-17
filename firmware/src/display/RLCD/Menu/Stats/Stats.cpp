#include "Stats.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "app/Config/Config.h"
#include "service/Clock/Clock.h"

static const int STATS_MAX_FILES = 100;
static const int GOAL_STEP = 50;
static const int GOAL_MIN = 50;
static const int GOAL_MAX = 5000;

static int goal()
{
    int g = status()["config"]["daily_goal"].as<int>();
    return g > 0 ? g : 500; // configs predating this feature have no goal yet
}

// Sum the full-document word count across every file on disk, from the cached
// config fields (no file reads) — same per-file number the FILES list shows.
static int journalTotal()
{
    JsonDocument &app = status();
    int total = 0;
    for (int i = 0; i < STATS_MAX_FILES; i++)
    {
        if (!gfs()->exists(format("/%d.txt", i).c_str()))
            continue;
        total += app["config"][format("wordcount_file_%d", i)].as<int>() +
                 app["config"][format("wordcount_buffer_%d", i)].as<int>();
    }
    return total;
}

// "28640" -> "28,640" so large totals stay readable on the mono panel.
static String grouped(int n)
{
    String s = String(n);
    int insert = s.length() - 3;
    while (insert > 0)
    {
        s = s.substring(0, insert) + "," + s.substring(insert);
        insert -= 3;
    }
    return s;
}

void Stats_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();
}

void Stats_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();

    // catch a day boundary before drawing, so streak/Today are current
    clock_tick();

    Menu_drawTabs(display, u8, 2); // draws the shared date in the header

    const int xl = 20;  // label left edge
    const int xr = 384; // value right edge
    int today = app["config"]["today_words"].as<int>();
    int g = goal();
    bool goalOn = app["config"]["goal_enabled"] | true; // daily-goal tracking

    u8->setFont(u8g2_font_profont17_tf);

    // ---- Today (vs goal + progress bar when tracking is on) ----
    {
        int y = 70;
        u8->setCursor(xl, y);
        u8->print("Today");

        char v[24];
        if (goalOn)
            snprintf(v, sizeof(v), "%d / %d w", today, g);
        else
            snprintf(v, sizeof(v), "%d w", today);
        u8->setCursor(xr - u8->getUTF8Width(v), y);
        u8->print(v);

        if (goalOn)
        {
            // progress bar: outline + filled portion clamped to [0, full]
            int bx = xl, bw = xr - xl, by = y + 12, bh = 16;
            display->drawRectangle(bx, by, bx + bw, by + bh, 1);
            int fill = g > 0 ? (bw * today) / g : 0;
            if (fill < 0) fill = 0;
            if (fill > bw) fill = bw;
            if (fill > 0)
                display->drawFilledRectangle(bx, by, bx + fill, by + bh, 1);
        }
    }

    // Streak counts consecutive met-goal days. The stored value is days completed
    // up to yesterday; today is added live the moment its goal is met, so hitting
    // the goal rewards you now instead of at the next rollover.
    int streak = app["config"]["streak"].as<int>();
    if (today >= g)
        streak += 1;

    // ---- Streak (only when tracking) / This session / Journal total ----
    int y = 150;
    if (goalOn)
    {
        String sv = String(streak) + (streak == 1 ? " day" : " days");
        u8->setCursor(xl, y);
        u8->print("Streak");
        u8->setCursor(xr - u8->getUTF8Width(sv.c_str()), y);
        u8->print(sv.c_str());
        y += 40;
    }
    struct { const char *label; String value; } rows[] = {
        {"This session", grouped(app["session_words"].as<int>()) + " w"},
        {"Journal total", grouped(journalTotal()) + " w"},
    };
    for (auto &row : rows)
    {
        u8->setCursor(xl, y);
        u8->print(row.label);
        u8->setCursor(xr - u8->getUTF8Width(row.value.c_str()), y);
        u8->print(row.value.c_str());
        y += 40;
    }

    // footer
    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(6, 296);
    u8->print("[+/-] goal  [<-] settings");
}

void Stats_keyboard(int key)
{
    JsonDocument &app = status();

    if (key == '+' || key == '=') // raise goal
    {
        int g = goal() + GOAL_STEP;
        app["config"]["daily_goal"] = g > GOAL_MAX ? GOAL_MAX : g;
        config_save();
        Menu_clear();
        return;
    }
    if (key == '-' || key == '_') // lower goal
    {
        int g = goal() - GOAL_STEP;
        app["config"]["daily_goal"] = g < GOAL_MIN ? GOAL_MIN : g;
        config_save();
        Menu_clear();
        return;
    }

    if (key == 18 || key == 'B' || key == 'b' || key == '\b') // Left / Back -> Settings
    {
        app["menu"]["state"] = MENU_SETTINGS;
        return;
    }
    if (key == 19) // Right -> wrap to Files
    {
        app["menu"]["state"] = MENU_HOME;
        return;
    }
    if (key == 27 || key == MENU) // Esc -> editor
    {
        app["screen"] = WORDPROCESSOR;
        return;
    }
}
