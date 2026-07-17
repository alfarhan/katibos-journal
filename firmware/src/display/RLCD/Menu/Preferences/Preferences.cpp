#include "Preferences.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/Menu/FileList/Pagination.h"
#include "../Timezone/Timezone.h" // tz_label

// One consolidated Preferences screen: behavior toggles/values grouped by
// section. Editor/display rows cycle in place (the editor re-reads these config
// keys on entry, so we only write them here); the heavier ones (keyboard layout,
// time zone, sync provider, about) drill into their existing screens.
enum
{
    R_HEAD = 0, // non-selectable section header
    R_THEME,
    R_SPACE,
    R_FLOW,
    R_STATUS,
    R_LAYOUT,
    R_TZ,
    R_CLOCK,
    R_PROV,
    R_GOAL,
    R_GOALTRACK,
    R_WAKE,
    R_DEVNAME,
    R_FACTORY,
    R_ABOUT,
};

static const int GOAL_STEP = 50, GOAL_MIN = 50, GOAL_MAX = 5000;

struct PRow
{
    int type;
    const char *label;
};

static const PRow ROWS[] = {
    {R_HEAD, "EDITOR"},
    {R_THEME, "Theme"},
    {R_SPACE, "Line spacing"},
    {R_FLOW, "Text flow"},
    {R_STATUS, "Status bar"},
    {R_HEAD, "INPUT"},
    {R_LAYOUT, "Keyboard layout"},
    {R_HEAD, "TIME & DATE"},
    {R_TZ, "Time zone"},
    {R_CLOCK, "Clock format"},
    {R_HEAD, "SYNC"},
    {R_PROV, "Sync provider"},
    {R_HEAD, "WRITING"},
    {R_GOAL, "Daily goal"},
    {R_GOALTRACK, "Goal tracking"},
    {R_HEAD, "SYSTEM"},
    {R_WAKE, "Wake-up animation"},
    {R_DEVNAME, "Device name"},
    {R_FACTORY, "Factory reset"},
    {R_ABOUT, "About"},
};
static const int NROWS = sizeof(ROWS) / sizeof(ROWS[0]);

static int g_cursor = 1; // index into ROWS (a selectable row)
static int g_top = 0;    // first visible row (scroll window)

static const char *SPACE_LBL[] = {"Normal", "Relaxed", "Spacious", "Compact"};
static const char *FLOW_LBL[] = {"Top", "Middle", "Bottom"};

static bool isSel(int i) { return ROWS[i].type != R_HEAD; }
static bool isDrill(int t)
{
    return t == R_LAYOUT || t == R_TZ || t == R_PROV || t == R_ABOUT ||
           t == R_DEVNAME || t == R_FACTORY;
}

// current value shown on the right of a row (empty for drill rows w/o a value)
static String valueStr(JsonDocument &app, int type)
{
    switch (type)
    {
    case R_THEME:
        return app["config"]["theme_dark"].as<bool>() ? "Dark" : "Light";
    case R_SPACE:
        return SPACE_LBL[(app["config"]["line_spacing"] | 0) % 4];
    case R_FLOW:
        return FLOW_LBL[(app["config"]["scroll_mode"] | 2) % 3];
    case R_STATUS:
        return app["config"]["statusbar_hidden"].as<bool>() ? "Hidden" : "Shown";
    case R_LAYOUT:
    {
        String l = app["config"]["keyboard_layout"].as<String>();
        return (l.isEmpty() || l == "null") ? String("US") : l;
    }
    case R_TZ:
        return tz_label(app["config"]["timezone"] | 180);
    case R_CLOCK:
        return (app["config"]["clock_24h"] | true) ? "24-hour" : "12-hour";
    case R_PROV:
        return (app["config"]["sync"]["provider"].as<String>() == "git") ? "GitHub" : "Drive";
    case R_GOAL:
    {
        int gg = app["config"]["daily_goal"] | 0;
        if (gg <= 0)
            gg = 500;
        return String(gg) + " w";
    }
    case R_GOALTRACK:
        return (app["config"]["goal_enabled"] | true) ? "On" : "Off";
    case R_WAKE:
        return app["config"]["wakeup_animation_disabled"].as<bool>() ? "Off" : "On";
    case R_DEVNAME:
    {
        String d = app["config"]["device_name"].as<String>();
        return (d.isEmpty() || d == "null") ? String("MICROJOURNAL") : d;
    }
    }
    return "";
}

// cycle an in-place (non-drill) value; dir +1 forward, -1 back
static void cycle(JsonDocument &app, int type, int dir)
{
    switch (type)
    {
    case R_THEME:
        app["config"]["theme_dark"] = !app["config"]["theme_dark"].as<bool>();
        break;
    case R_SPACE:
        app["config"]["line_spacing"] = (((app["config"]["line_spacing"] | 0) + (dir > 0 ? 1 : 3)) % 4);
        break;
    case R_FLOW:
        app["config"]["scroll_mode"] = (((app["config"]["scroll_mode"] | 2) + (dir > 0 ? 1 : 2)) % 3);
        break;
    case R_STATUS:
        app["config"]["statusbar_hidden"] = !app["config"]["statusbar_hidden"].as<bool>();
        break;
    case R_CLOCK:
        app["config"]["clock_24h"] = !(app["config"]["clock_24h"] | true);
        break;
    case R_GOAL:
    {
        int gg = app["config"]["daily_goal"] | 0;
        if (gg <= 0)
            gg = 500;
        gg += (dir > 0 ? GOAL_STEP : -GOAL_STEP);
        if (gg < GOAL_MIN) gg = GOAL_MIN;
        if (gg > GOAL_MAX) gg = GOAL_MAX;
        app["config"]["daily_goal"] = gg;
        break;
    }
    case R_GOALTRACK:
        app["config"]["goal_enabled"] = !(app["config"]["goal_enabled"] | true);
        break;
    case R_WAKE:
        app["config"]["wakeup_animation_disabled"] = !app["config"]["wakeup_animation_disabled"].as<bool>();
        break;
    default:
        return;
    }
    config_save();
}

static void drill(JsonDocument &app, int type)
{
    app["menu"]["return"] = MENU_PREFS; // these screens come back here on Esc
    switch (type)
    {
    case R_LAYOUT: app["menu"]["state"] = MENU_LAYOUT; break;
    case R_TZ: app["menu"]["state"] = MENU_TIMEZONE; break;
    case R_PROV: app["menu"]["state"] = MENU_SYNCPROV; break;
    case R_DEVNAME: app["menu"]["state"] = MENU_DEVNAME; break;
    case R_FACTORY: app["menu"]["state"] = MENU_FACTORY; break;
    case R_ABOUT: app["menu"]["state"] = MENU_ABOUT; break;
    }
}

// small right-pointing chevron centred vertically on yc
static void drawChevron(ST7305_4p2_BW_DisplayDriver *display, int xr, int yc, uint16_t color)
{
    display->drawLine(xr - 6, yc - 5, xr, yc, color);
    display->drawLine(xr, yc, xr - 6, yc + 5, color);
}

// rows between the header divider and the footer. Section headers get a little
// extra height (HEAD_GAP) as breathing space ABOVE the title.
static const int TOP = 34, FOOT = 276, ITEM_H = 18, HEAD_GAP = 8;

static int rowHeight(int i)
{
    return (ROWS[i].type == R_HEAD && i > 0) ? ITEM_H + HEAD_GAP : ITEM_H;
}

// highest row index fully visible when the window starts at `top`
static int lastVisible(int top)
{
    int y = TOP, last = top;
    for (int i = top; i < NROWS; i++)
    {
        if (y + rowHeight(i) > FOOT)
            break;
        y += rowHeight(i);
        last = i;
    }
    return last;
}

static void keepVisible()
{
    if (g_cursor < g_top)
        g_top = g_cursor;
    while (g_cursor > lastVisible(g_top))
        g_top++;
    if (g_top < 0)
        g_top = 0;
}

void Preferences_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();
    display->clearDisplay();
    if (!isSel(g_cursor))
        g_cursor = 1;
    g_top = 0;
    keepVisible();
}

void Preferences_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();
    Menu_drawHeader(display, u8, "PREFERENCES");

    const int xl = 24, xr = 384;
    u8->setFont(u8g2_font_profont17_tf);

    int y = TOP;
    for (int i = g_top; i < NROWS; i++)
    {
        int h = rowHeight(i);
        if (y + h > FOOT)
            break;
        const PRow &row = ROWS[i];

        if (row.type == R_HEAD)
        {
            // text sits in the lower part of the cell; the extra HEAD_GAP above
            // it separates this section from the previous group's last item.
            int base = y + (h - ITEM_H) + 14;
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
            u8->setCursor(8, base);
            u8->print(row.label);
            display->drawLine(8, y + h - 2, xr, y + h - 2, 1);
            y += h;
            continue;
        }

        int base = y + 14, yc = y + 9;
        bool focused = (i == g_cursor);
        if (focused)
        {
            display->drawFilledRectangle(0, y, 400, y + ITEM_H, 1);
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
        }

        u8->setCursor(xl, base);
        u8->print(row.label);

        if (isDrill(row.type))
        {
            String v = valueStr(app, row.type);
            if (v.length())
            {
                u8->setCursor(xr - 14 - u8->getUTF8Width(v.c_str()), base);
                u8->print(v.c_str());
            }
            drawChevron(display, xr, yc, focused ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK);
        }
        else
        {
            String v = String("< ") + valueStr(app, row.type) + " >";
            u8->setCursor(xr - u8->getUTF8Width(v.c_str()), base);
            u8->print(v.c_str());
        }

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }

        y += h;
    }

    display->drawLine(0, FOOT, 400, FOOT, 1);
    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(6, 296);
    u8->print("[UP/DN] move  [<>] change  [ENT] open  [Esc] back");
}

void Preferences_keyboard(int key)
{
    JsonDocument &app = status();

    if (key == 27 || key == MENU || key == 'B' || key == 'b')
    {
        // Back to wherever we came from. Don't read app.menu.return — our own
        // drill-ins (layout / time zone / sync provider) overwrite it with
        // MENU_PREFS. A dedicated flag tracks an editor (Ctrl+,) entry.
        if (app["menu"]["prefs_from_editor"] | false)
        {
            app["menu"]["prefs_from_editor"] = false;
            app["screen"] = WORDPROCESSOR;
        }
        else
        {
            app["menu"]["state"] = MENU_SETTINGS;
        }
        return;
    }

    if (key == 20) // Up
    {
        for (int i = g_cursor - 1; i >= 0; i--)
            if (isSel(i)) { g_cursor = i; break; }
        keepVisible();
        Menu_clear();
        return;
    }
    if (key == 21) // Down
    {
        for (int i = g_cursor + 1; i < NROWS; i++)
            if (isSel(i)) { g_cursor = i; break; }
        keepVisible();
        Menu_clear();
        return;
    }

    int type = ROWS[g_cursor].type;
    if (key == '\n' || key == '\r')
    {
        if (isDrill(type))
            drill(app, type);
        else
            cycle(app, type, +1);
        Menu_clear();
        return;
    }
    if (key == 19) // Right → forward (or open a drill row)
    {
        if (isDrill(type))
            drill(app, type);
        else
            cycle(app, type, +1);
        Menu_clear();
        return;
    }
    if (key == 18) // Left → back-cycle in-place values
    {
        if (!isDrill(type))
            cycle(app, type, -1);
        Menu_clear();
        return;
    }
}
