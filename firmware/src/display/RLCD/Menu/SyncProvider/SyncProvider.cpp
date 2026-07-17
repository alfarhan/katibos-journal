#include "SyncProvider.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/Menu/FileList/Pagination.h"

// Which sync backend reconcile uses. The code stored in config.sync.provider is
// the single source of truth (kept separate from the keyboard-layout setting).
struct ProviderOption
{
    char key;
    const char *label;
    const char *code;
};

static const ProviderOption PROVIDERS[] = {
    {'D', "Google Drive", "drive"},
    {'G', "GitHub", "git"},
};
static const int PROVIDER_N = sizeof(PROVIDERS) / sizeof(PROVIDERS[0]);

static int g_cursor = 0;

static int currentRow()
{
    String p = status()["config"]["sync"]["provider"].as<String>();
    if (p.isEmpty() || p == "null")
        p = "drive";
    for (int r = 0; r < PROVIDER_N; r++)
        if (p == PROVIDERS[r].code)
            return r;
    return 0; // drive
}

static void apply(int row)
{
    JsonDocument &app = status();
    app["config"]["sync"]["provider"] = PROVIDERS[row].code;
    config_save();
    app["menu"]["state"] = app["menu"]["return"] | MENU_SETTINGS;
}

// Checkmark from two strokes, right-aligned (mirrors Layout.cpp).
static void drawCheck(ST7305_4p2_BW_DisplayDriver *display, int xr, int y, uint16_t color)
{
    display->drawLine(xr - 13, y - 4, xr - 8, y + 1, color);
    display->drawLine(xr - 8, y + 1, xr, y - 9, color);
}

void SyncProvider_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();
    display->clearDisplay();
    g_cursor = currentRow();
}

void SyncProvider_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_drawHeader(display, u8, "SYNC PROVIDER");

    int active = currentRow();
    const int pitch = 19;
    const int xr = 384;

    u8->setFont(u8g2_font_profont17_tf);
    for (int r = 0; r < PROVIDER_N; r++)
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
        u8->printf("[%c]  %s", PROVIDERS[r].key, PROVIDERS[r].label);

        if (r == active)
            drawCheck(display, xr, y, focused ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK);

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }
    }

    // GitHub needs a token in config.json (entered over USB Drive Mode); the
    // device only chooses the provider here.
    u8->setCursor(14, 48 + (PROVIDER_N + 1) * pitch);
    u8->print("GitHub repo/token set in config.json");

    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(8, 296);
    u8->print("[UP/DN] move  [ENT] select  [<-] back");
}

void SyncProvider_keyboard(char key)
{
    JsonDocument &app = status();

    if (key == 27 || key == MENU || key == 18 || key == 'B' || key == 'b')
    {
        app["menu"]["state"] = app["menu"]["return"] | MENU_SETTINGS;
        return;
    }

    if (key == 20) // Up
    {
        g_cursor = paginate::clampInt(g_cursor - 1, 0, PROVIDER_N - 1);
        Menu_clear();
        return;
    }
    if (key == 21) // Down
    {
        g_cursor = paginate::clampInt(g_cursor + 1, 0, PROVIDER_N - 1);
        Menu_clear();
        return;
    }
    if (key == '\n' || key == '\r')
    {
        apply(g_cursor);
        return;
    }

    char up = (key >= 'a' && key <= 'z') ? key - 32 : key;
    for (int r = 0; r < PROVIDER_N; r++)
        if (PROVIDERS[r].key == up)
        {
            apply(r);
            return;
        }
}
