#include "Bluetooth.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "../../WordProcessor/WordProcessor.h"
#include "display/RLCD/display_RLCD.h"
#include "display/RLCD/Menu/FileList/Pagination.h"
#include "keyboard/BLEHost/BLEHost.h"

#define BT_PER_PAGE 8

static int g_cursor = 0;

void Bluetooth_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    g_cursor = 0;
    // Kick off a discovery scan as soon as the screen opens.
    blehost_scan_start();
    Menu_clear();
}

void Bluetooth_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();
    Menu_drawHeader(display, u8, "CONNECT KEYBOARD");
    u8->setFont(u8g2_font_profont17_tf);

    // status line
    u8->setCursor(10, 46);
    if (app["ble"]["connected"] | false)
    {
        const char *peer = app["ble"]["peer"] | "";
        u8->printf("Connected: %s", (peer && *peer) ? peer : "keyboard");
    }
    else
    {
        const char *st = app["ble"]["status"] | "Not connected";
        u8->print(st);
    }

    // found-devices list
    if (!app["ble"]["devices"].is<JsonArray>())
        app["ble"]["devices"].to<JsonArray>();
    JsonArray devs = app["ble"]["devices"].as<JsonArray>();
    int count = (int)devs.size();
    int cursor = paginate::clampInt(g_cursor, 0, count > 0 ? count - 1 : 0);
    int page = paginate::pageOf(cursor, BT_PER_PAGE);
    int rows = paginate::rowsOnPage(page, BT_PER_PAGE, count);

    u8->setCursor(10, 74);
    u8->print("FOUND KEYBOARDS:");
    RLCD_drawScrollbar(display, 384, 84, 286, page * BT_PER_PAGE, BT_PER_PAGE, count);

    if (count == 0)
    {
        u8->setCursor(14, 104);
        u8->print((app["ble"]["scanning"] | false) ? "Scanning..." : "none - press R to scan");
    }

    for (int r = 0; r < rows; r++)
    {
        int idx = page * BT_PER_PAGE + r;
        int y = 100 + r * 22;
        bool focused = (idx == cursor);

        if (focused)
        {
            display->drawFilledRectangle(8, y - 15, 378, y + 4, 1);
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
        }

        const char *nm = devs[idx]["name"].as<const char *>();
        if (!nm || !*nm)
            nm = "(unknown)";
        u8->setCursor(14, y);
        u8->printf("[%d]  %s", idx + 1, nm);

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }
    }


}

void Bluetooth_keyboard(int key)
{
    JsonDocument &app = status();
    JsonArray devs = app["ble"]["devices"].as<JsonArray>();
    int count = (int)devs.size();

    if (key == 20) // Up
    {
        g_cursor = paginate::clampInt(g_cursor - 1, 0, count > 0 ? count - 1 : 0);
        Menu_clear();
        return;
    }
    if (key == 21) // Down
    {
        g_cursor = paginate::clampInt(g_cursor + 1, 0, count > 0 ? count - 1 : 0);
        Menu_clear();
        return;
    }
    if (key == '\n' || key == '\r')
    {
        if (count > 0)
            blehost_connect_index(g_cursor);
        Menu_clear();
        return;
    }
    if (key == 'R' || key == 'r')
    {
        blehost_scan_start();
        Menu_clear();
        return;
    }
    if (key == 'F' || key == 'f')
    {
        blehost_forget();
        Menu_clear();
        return;
    }
    if (key == 18 || key == 'B' || key == 'b' || key == '\b' || key == 27 || key == MENU)
    {
        app["menu"]["state"] = app["menu"]["return"] | MENU_SETTINGS;
        Menu_clear();
        return;
    }
}
