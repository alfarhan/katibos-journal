#include "Settings.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/Menu/FileList/Pagination.h"
#include "../Timezone/Timezone.h" // tz_label

enum
{
    ACT_PREFS,
    ACT_LANGUAGE,
    ACT_TIMEZONE,
    ACT_WIFI,
    ACT_SYNC,
    ACT_SYNCPROV,
    ACT_BLE,
    ACT_DRIVE,
    ACT_UPDATE,
    ACT_HELP,
    ACT_ABOUT
};

static int g_cursor = 0;

// Build the visible action list (Sync only appears when a sync URL is set), so
// render and keyboard agree on what each cursor row means.
static int buildList(int *ids)
{
    JsonDocument &app = status();
    int n = 0;
    // Language, Time zone, Sync provider and About now live in Preferences.
    ids[n++] = ACT_PREFS;
    ids[n++] = ACT_WIFI;
    if (!app["config"]["sync"]["url"].as<String>().isEmpty())
        ids[n++] = ACT_SYNC;
    ids[n++] = ACT_BLE;
    ids[n++] = ACT_DRIVE;
    ids[n++] = ACT_UPDATE; // always available via built-in fallback URL
    ids[n++] = ACT_HELP;
    return n;
}

static const char *actionLabel(int act)
{
    switch (act)
    {
    case ACT_PREFS: return "Preferences";
    case ACT_LANGUAGE: return "Language";
    case ACT_TIMEZONE: return "Time zone";
    case ACT_WIFI: return "Wi-Fi";
    case ACT_SYNC: return "Sync & Backup";
    case ACT_SYNCPROV: return "Sync provider";
    case ACT_BLE: return "BLE Keyboard";
    case ACT_DRIVE: return "Drive Mode (USB)";
    case ACT_UPDATE: return "Check for Update";
    case ACT_HELP: return "Help";
    case ACT_ABOUT: return "About";
    }
    return "";
}

// Right-pointing chevron drawn from two short strokes (font-independent, crisper
// than a '>' glyph). `color` lets the caller invert it on a focused row.
static void drawChevron(ST7305_4p2_BW_DisplayDriver *display, int xr, int y, uint16_t color)
{
    display->drawLine(xr - 8, y - 9, xr - 1, y - 2, color);
    display->drawLine(xr - 1, y - 2, xr - 8, y + 5, color);
}

static void dispatch(int act)
{
    JsonDocument &app = status();
    // sub-screens read this to know which tab to return to on Esc/back
    app["menu"]["return"] = MENU_SETTINGS;
    switch (act)
    {
    case ACT_PREFS: app["menu"]["prefs_from_editor"] = false; app["menu"]["state"] = MENU_PREFS; break;
    case ACT_LANGUAGE: app["menu"]["state"] = MENU_LAYOUT; break;
    case ACT_TIMEZONE: app["menu"]["state"] = MENU_TIMEZONE; break;
    case ACT_WIFI: app["menu"]["state"] = MENU_WIFI; break;
    case ACT_SYNC:
        if (!app["config"]["sync"]["url"].as<String>().isEmpty())
            app["menu"]["state"] = MENU_SYNC;
        break;
    case ACT_SYNCPROV: app["menu"]["state"] = MENU_SYNCPROV; break;
    case ACT_BLE: app["screen"] = KEYBOARDSCREEN; break;
    case ACT_DRIVE: app["menu"]["state"] = MENU_STORAGE; break;
    case ACT_UPDATE: app["menu"]["state"] = MENU_UPDATE; break;
    case ACT_HELP: app["menu"]["state"] = MENU_HELP; break;
    case ACT_ABOUT: app["menu"]["state"] = MENU_ABOUT; break;
    }
}

void Settings_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();
    g_cursor = 0;
}

void Settings_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();

    Menu_drawTabs(display, u8, 1);

    int ids[12];
    int n = buildList(ids);

    // Settings as a list of cells filling the band between the header and footer.
    // Each cell: label (left), value or chevron (right); the focused cell is a
    // full-width inverse bar; thin rules separate the rest so it reads as a list.
    // Pitch is derived from the item count so the list always fits above the
    // footer (it grew to 8 rows) and stays evenly spaced for shorter lists.
    const int top = 34;     // first cell top, just under the header divider
    const int footY = 276;  // footer divider
    const int xl = 24;      // label left edge
    const int xr = 378;     // value / chevron right edge
    int pitch = (footY - top) / n;
    if (pitch > 38)
        pitch = 38;                      // don't over-stretch a short list
    const int baseOff = (pitch + 15) / 2; // vertically center the glyph

    u8->setFont(u8g2_font_profont17_tf);
    for (int r = 0; r < n; r++)
    {
        int cellTop = top + r * pitch;
        int y = cellTop + baseOff; // text baseline, centered in the cell
        bool focused = (r == g_cursor);

        if (focused)
        {
            display->drawFilledRectangle(0, cellTop, 400, cellTop + pitch, 1);
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
        }

        u8->setCursor(xl, y);
        u8->print(actionLabel(ids[r]));

        // Language + Time zone carry a value; the rest open a sub-screen, marked
        // with a chevron so the affordance is obvious.
        if (ids[r] == ACT_LANGUAGE)
        {
            String loc = app["config"]["keyboard_layout"].as<String>();
            if (loc.isEmpty() || loc == "null")
                loc = "US";
            u8->setCursor(xr - u8->getUTF8Width(loc.c_str()), y);
            u8->print(loc.c_str());
        }
        else if (ids[r] == ACT_TIMEZONE)
        {
            String tz = tz_label(app["config"]["timezone"] | 180);
            u8->setCursor(xr - u8->getUTF8Width(tz.c_str()), y);
            u8->print(tz.c_str());
        }
        else if (ids[r] == ACT_SYNCPROV)
        {
            const char *prov =
                (app["config"]["sync"]["provider"].as<String>() == "git") ? "GitHub" : "Drive";
            u8->setCursor(xr - u8->getUTF8Width(prov), y);
            u8->print(prov);
        }
        else
        {
            drawChevron(display, xr, y, focused ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK);
        }

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }

        // hairline between cells (the inverse bar owns its own boundaries)
        if (r < n - 1 && !focused && r + 1 != g_cursor)
            display->drawLine(xl, cellTop + pitch, xr, cellTop + pitch, 1);
    }

    // divider above the footer hints (matches the header text/line spacing)
    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(6, 296);
    u8->print("[UP/DN] move  [ENT] open");
}

void Settings_keyboard(int key)
{
    JsonDocument &app = status();
    int ids[12];
    int n = buildList(ids);

    if (key == 20) // Up
    {
        g_cursor = paginate::clampInt(g_cursor - 1, 0, n - 1);
        Menu_clear();
        return;
    }
    if (key == 21) // Down
    {
        g_cursor = paginate::clampInt(g_cursor + 1, 0, n - 1);
        Menu_clear();
        return;
    }
    if (key == 18 || key == 'B' || key == 'b' || key == '\b') // Left / Back -> Files
    {
        app["menu"]["state"] = MENU_HOME;
        return;
    }
    if (key == 19) // Right -> Stats
    {
        app["menu"]["state"] = MENU_STATS;
        return;
    }
    if (key == 27 || key == MENU) // Esc -> editor
    {
        app["screen"] = WORDPROCESSOR;
        return;
    }

    if (key == '\n' || key == '\r')
    {
        dispatch(ids[g_cursor]);
        return;
    }

    // letter fast-paths (Language/Time zone/Sync provider/About moved to Preferences)
    if (key == 'R' || key == 'r') dispatch(ACT_PREFS);
    else if (key == 'W' || key == 'w') dispatch(ACT_WIFI);
    else if (key == 'S' || key == 's') dispatch(ACT_SYNC);
    else if (key == 'T' || key == 't') dispatch(ACT_BLE);
    else if (key == 'U' || key == 'u') dispatch(ACT_DRIVE);
    else if (key == 'H' || key == 'h') dispatch(ACT_HELP);
    else if (key == 'P' || key == 'p') dispatch(ACT_UPDATE);
}
