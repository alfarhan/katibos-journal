#include "Layout.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/Menu/FileList/Pagination.h"
#include "../../WordProcessor/WordProcessor.h"

// One row of the language list: the fast-path letter, the human label, and the
// locale code stored in config. Single source of truth for render + keyboard.
struct LayoutOption
{
    char key;
    const char *label;
    const char *code;
};

static const LayoutOption LAYOUTS[] = {
    {'M', "Arabic - Mac", "AR"},
    {'W', "Arabic - Windows", "ARW"},
    {'A', "International", "INT"},
    {'B', "Belgian", "BE"},
    {'C', "Canadian", "CA"},
    {'D', "Dvorak", "DV"},
    {'F', "French", "FR"},
    {'G', "German", "GE"},
    {'I', "Italian", "IT"},
    {'K', "UK", "UK"},
    {'L', "Latin America", "LAT"},
    {'S', "Swedish", "SWE"},
    {'U', "US", "US"},
};
static const int LAYOUT_N = sizeof(LAYOUTS) / sizeof(LAYOUTS[0]);

static int g_cursor = 0;

// Row index of the layout currently saved in config (default US if unset).
static int currentRow()
{
    String loc = status()["config"]["keyboard_layout"].as<String>();
    if (loc.isEmpty() || loc == "null")
        loc = "US";
    for (int r = 0; r < LAYOUT_N; r++)
        if (loc == LAYOUTS[r].code)
            return r;
    for (int r = 0; r < LAYOUT_N; r++)
        if (String("US") == LAYOUTS[r].code)
            return r;
    return 0;
}

static void apply(int row)
{
    JsonDocument &app = status();
    app["config"]["keyboard_layout"] = LAYOUTS[row].code;
    config_save();
    app["menu"]["state"] = app["menu"]["return"] | MENU_SETTINGS;
}

// Checkmark drawn from two strokes (font-independent), right-aligned. `color`
// lets it invert on a focused row.
static void drawCheck(ST7305_4p2_BW_DisplayDriver *display, int xr, int y, uint16_t color)
{
    display->drawLine(xr - 13, y - 4, xr - 8, y + 1, color);
    display->drawLine(xr - 8, y + 1, xr, y - 9, color);
}

void Layout_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();
    display->clearDisplay();
    g_cursor = currentRow();
}

void Layout_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_drawHeader(display, u8, "LANGUAGE");

    int active = currentRow();
    const int pitch = 19;
    const int xr = 384;

    u8->setFont(u8g2_font_profont17_tf);
    for (int r = 0; r < LAYOUT_N; r++)
    {
        int y = 48 + r * pitch;
        bool focused = (r == g_cursor);

        if (focused)
        {
            display->drawFilledRectangle(8, y - 15, 392, y + 3, 1);
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
        }

        u8->setCursor(14, y);
        u8->printf("[%c]  %s", LAYOUTS[r].key, LAYOUTS[r].label);

        if (r == active)
            drawCheck(display, xr, y, focused ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK);

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }
    }

    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(8, 296);
    u8->print("[UP/DN] move  [ENT] select  [<-] back");
}

void Layout_keyboard(char key)
{
    JsonDocument &app = status();

    // Back to Settings without changing the layout (Esc / Left / B). Handled
    // first so these keys never fall through and reset the layout.
    if (key == 27 || key == MENU || key == 18 || key == 'B' || key == 'b')
    {
        app["menu"]["state"] = app["menu"]["return"] | MENU_SETTINGS;
        return;
    }

    if (key == 20) // Up
    {
        g_cursor = paginate::clampInt(g_cursor - 1, 0, LAYOUT_N - 1);
        Menu_clear();
        return;
    }
    if (key == 21) // Down
    {
        g_cursor = paginate::clampInt(g_cursor + 1, 0, LAYOUT_N - 1);
        Menu_clear();
        return;
    }
    if (key == '\n' || key == '\r')
    {
        apply(g_cursor);
        return;
    }

    // Letter fast-path: jump straight to a layout. Match is case-insensitive on
    // the row's letter; ignore anything else so an unknown key never resets the
    // layout.
    char up = (key >= 'a' && key <= 'z') ? key - 32 : key;
    for (int r = 0; r < LAYOUT_N; r++)
        if (LAYOUTS[r].key == up)
        {
            apply(r);
            return;
        }
}
