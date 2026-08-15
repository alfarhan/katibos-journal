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

// One list, saved keyboards first: they are the ones you actually switch
// between, and a scan hit you have already paired should not appear twice.
// Rows are built the same way for render and for the key handler, so the cursor
// can never mean a different row in each.
struct BtRow
{
    String name;
    String addr;
    int type;
    bool saved;
};

static int btRows(BtRow *out, int max)
{
    JsonDocument &app = status();
    int n = 0;

    if (app["ble"]["saved"].is<JsonArray>())
        for (JsonVariant v : app["ble"]["saved"].as<JsonArray>())
        {
            if (n >= max)
                break;
            out[n++] = {v["name"].as<String>(), v["addr"].as<String>(), v["type"] | 0, true};
        }

    if (app["ble"]["devices"].is<JsonArray>())
        for (JsonVariant v : app["ble"]["devices"].as<JsonArray>())
        {
            if (n >= max)
                break;
            String addr = v["addr"].as<String>();
            bool known = false;
            for (int i = 0; i < n; i++)
                if (out[i].saved && out[i].addr == addr)
                    known = true;
            if (known)
                continue;
            out[n++] = {v["name"].as<String>(), addr, v["type"] | 0, false};
        }
    return n;
}

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

    BtRow list[16];
    int count = btRows(list, 16);
    int cursor = paginate::clampInt(g_cursor, 0, count > 0 ? count - 1 : 0);
    int page = paginate::pageOf(cursor, BT_PER_PAGE);
    int rows = paginate::rowsOnPage(page, BT_PER_PAGE, count);

    u8->setCursor(10, 74);
    u8->print("KEYBOARDS:");
    RLCD_drawScrollbar(display, 384, 84, 266, page * BT_PER_PAGE, BT_PER_PAGE, count);

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

        String nm = list[idx].name;
        if (nm.isEmpty() || nm == "null")
            nm = "(unknown)";
        u8->setCursor(14, y);
        u8->printf("[%d]  %s", idx + 1, nm.c_str());

        // A saved keyboard is marked; the connected one says so in words, since
        // "paired" and "in use right now" are different things to know.
        const char *peer = app["ble"]["peer"] | "";
        bool live = (app["ble"]["connected"] | false) && list[idx].name == peer;
        if (live || list[idx].saved)
        {
            const char *tag = live ? "in use" : "saved";
            u8->setCursor(374 - u8->getUTF8Width(tag), y);
            u8->print(tag);
        }

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }
    }

    // Footer key legend - this screen is the only place the pairing keys exist,
    // so it has to say what they are. Same footer as Sync / Update.
    static const RLCD_Hint HINTS[] = {
        {"ENT", "PAIR"}, {"R", "SCAN"}, {"F", "FORGET"}, {"ESC", "BACK"}};
    u8->setFont(u8g2_font_profont17_tf);
    display->drawLine(0, 276, 400, 276, 1);
    RLCD_drawHintBar(display, u8, (400 - RLCD_hintBarWidth(u8, RLCD_HINTS(HINTS))) / 2, 296,
                     RLCD_HINTS(HINTS));
}

void Bluetooth_keyboard(int key)
{
    JsonDocument &app = status();
    BtRow list[16];
    int count = btRows(list, 16);

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
        if (count > 0 && g_cursor < count)
        {
            const BtRow &r = list[g_cursor];
            blehost_connect_addr(r.addr.c_str(), r.type, r.name.c_str());
        }
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
        if (count > 0 && g_cursor < count && list[g_cursor].saved)
            blehost_forget_addr(list[g_cursor].addr.c_str(), list[g_cursor].type);
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
