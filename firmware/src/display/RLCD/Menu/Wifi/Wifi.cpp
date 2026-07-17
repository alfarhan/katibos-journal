#include "Wifi.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "../../WordProcessor/WordProcessor.h"

//
#include "service/WifiEntry/WifiEntry.h"
#include "service/Buffer/BufferService.h"
#include "display/RLCD/display_RLCD.h"
#include "display/RLCD/Menu/FileList/Pagination.h"

//
void Wifi_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    WifiEntry_setup();
}

//
void Wifi_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_drawHeader(display, u8, "WI-FI");
    u8->setCursor(0, 50);

    // RENDER based on the screen index
    //
    JsonDocument &app = status();
    int wifi_config_status = app["wifi_config_status"].as<int>();
    if (wifi_config_status == WIFI_CONFIG_LIST)
    {
        Wifi_render_list(display, u8);
    }
    else if (wifi_config_status == WIFI_CONFIG_SCAN)
    {
        Wifi_render_scan(display, u8);
    }
    else if (wifi_config_status == WIFI_CONFIG_ENTRY)
    {
        Wifi_render_entry(display, u8);
    }
    else if (wifi_config_status >= WIFI_CONFIG_EDIT_SSID)
    {
        Wifi_render_edit(display, u8);
    }
}

void Wifi_render_entry(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();
    int idx = app["wifi_config_index"].as<int>();
    JsonArray aps = app["wifi"]["access_points"].as<JsonArray>();
    const char *ssid = ((int)aps.size() > idx) ? aps[idx]["ssid"].as<const char *>() : "";
    if (!ssid || !*ssid)
        ssid = "(empty)";

    u8->setFont(u8g2_font_profont17_tf);

    // label + the network name in an inverse bar (matches the DELETE FILE look)
    u8->setCursor(10, 62);
    u8->print("Saved network:");

    display->drawFilledRectangle(8, 80, 392, 102, 1);
    u8->setForegroundColor(ST7305_COLOR_WHITE);
    u8->setBackgroundColor(ST7305_COLOR_BLACK);
    u8->setCursor(16, 97);
    u8->printf("[%d]  %s", idx + 1, ssid);
    u8->setForegroundColor(ST7305_COLOR_BLACK);
    u8->setBackgroundColor(ST7305_COLOR_WHITE);

    // actions in aligned key / label columns
    u8->setCursor(14, 150);
    u8->print("[Enter]");
    u8->setCursor(120, 150);
    u8->print("Edit name + password");

    u8->setCursor(14, 178);
    u8->print("[F]");
    u8->setCursor(120, 178);
    u8->print("Forget this network");

    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(10, 296);
    u8->print("[B] back");
}

//
void Wifi_render_list(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();

    // ensure the config structure exists
    if (!app["wifi"].is<JsonObject>())
        app["wifi"].to<JsonObject>();
    if (!app["wifi"]["access_points"].is<JsonArray>())
        app["wifi"]["access_points"].to<JsonArray>();

    JsonArray aps = app["wifi"]["access_points"].as<JsonArray>();
    int count = (int)aps.size();
    int cursor = paginate::clampInt(app["wifi_cursor"] | 0, 0, count > 0 ? count - 1 : 0);
    int page = paginate::pageOf(cursor, WIFI_PER_PAGE);
    int rows = paginate::rowsOnPage(page, WIFI_PER_PAGE, count);
    int pages = paginate::pageCount(count, WIFI_PER_PAGE);

    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(10, 46);
    u8->print("SAVED NETWORKS:");
    if (pages > 1)
    {
        char pg[12];
        snprintf(pg, sizeof(pg), "p%d/%d", page + 1, pages);
        u8->setCursor(392 - u8->getUTF8Width(pg), 46);
        u8->print(pg);
    }

    if (count == 0)
    {
        u8->setCursor(14, 86);
        u8->print("(none yet - press N to add)");
    }

    for (int r = 0; r < rows; r++)
    {
        int idx = page * WIFI_PER_PAGE + r;
        int y = 72 + r * 22;
        bool focused = (idx == cursor);

        if (focused)
        {
            display->drawFilledRectangle(8, y - 15, 392, y + 4, 1);
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
        }

        const char *ssid = aps[idx]["ssid"].as<const char *>();
        if (!ssid || !*ssid)
            ssid = "(empty)";
        u8->setCursor(14, y);
        u8->printf("[%d]  %s", idx + 1, ssid);

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }
    }

    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(8, 296);
    u8->print("[Enter] open [S] scan [N] new [B] back");
}

// Four signal bars of rising height, filled to the strength level (RSSI dBm).
static void Wifi_drawBars(ST7305_4p2_BW_DisplayDriver *display, int x, int y, int rssi, uint16_t color)
{
    int level = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    for (int b = 0; b < 4; b++)
    {
        int bx = x + b * 6;
        int h = 4 + b * 3;
        if (b < level)
            display->drawFilledRectangle(bx, y - h, bx + 4, y, color);
        else
            display->drawLine(bx, y - 1, bx + 4, y - 1, color); // empty-bar base tick
    }
}

void Wifi_render_scan(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();
    u8->setFont(u8g2_font_profont17_tf);

    // While the scan flag is set, show feedback, push it, then run the (blocking
    // on device) scan, and repaint clean for the results.
    if (app["wifi_scanning"] | false)
    {
        display->clearDisplay();
        Menu_drawHeader(display, u8, "WI-FI");
        u8->setFont(u8g2_font_profont17_tf);
        u8->setCursor(10, 80);
        u8->print("Scanning for networks...");
        display->display();

        wifi_scan(app);
        app["wifi_scanning"] = false;
        app["wifi_cursor"] = 0;

        display->clearDisplay();
        Menu_drawHeader(display, u8, "WI-FI");
        u8->setFont(u8g2_font_profont17_tf);
    }

    JsonArray scan = app["network"]["scan"].as<JsonArray>();
    int count = (int)scan.size();
    int cursor = paginate::clampInt(app["wifi_cursor"] | 0, 0, count > 0 ? count - 1 : 0);
    int page = paginate::pageOf(cursor, WIFI_PER_PAGE);
    int rows = paginate::rowsOnPage(page, WIFI_PER_PAGE, count);
    int pages = paginate::pageCount(count, WIFI_PER_PAGE);

    u8->setCursor(10, 46);
    u8->print("NEARBY NETWORKS:");
    if (pages > 1)
    {
        char pg[12];
        snprintf(pg, sizeof(pg), "p%d/%d", page + 1, pages);
        u8->setCursor(392 - u8->getUTF8Width(pg), 46);
        u8->print(pg);
    }

    if (count == 0)
    {
        u8->setCursor(14, 86);
        u8->print("none found - press R to rescan");
    }

    for (int r = 0; r < rows; r++)
    {
        int idx = page * WIFI_PER_PAGE + r;
        int y = 72 + r * 22;
        bool focused = (idx == cursor);

        if (focused)
        {
            display->drawFilledRectangle(8, y - 15, 392, y + 4, 1);
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
        }

        const char *ssid = scan[idx]["ssid"].as<const char *>();
        if (!ssid)
            ssid = "";
        u8->setCursor(14, y);
        u8->print(ssid);
        Wifi_drawBars(display, 360, y, scan[idx]["rssi"] | -100,
                      focused ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK);

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }
    }

    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(8, 296);
    u8->print("[Enter] select  [R] rescan  [B] back");
}

void Wifi_render_edit(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    //
    JsonDocument &app = status();
    int wifi_config_index = app["wifi_config_index"].as<int>();
    int wifi_config_status = app["wifi_config_status"].as<int>();

    // Load saved WiFi connection information from the app["config"]["access_points"] array
    JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();

    const char *savedSsid = savedAccessPoints[wifi_config_index]["ssid"];
    const char *savedPassword = savedAccessPoints[wifi_config_index]["password"];

    u8->printf(" EDIT [%d] WIFI CONFIG", wifi_config_index + 1);
    u8->println("");

    if (wifi_config_status == WIFI_CONFIG_EDIT_SSID)
    {
        u8->println(" TYPE SSID:");
        u8->println("");

        u8->printf("      %s", buffer_get());
        u8->println("");
        u8->println("");
        u8->println(" [ENTER] NEXT ");
    }
    else if (wifi_config_status == WIFI_CONFIG_EDIT_KEY)
    {
        u8->println(" TYPE WIFI KEY:");
        u8->println("");

        u8->printf("      %s", buffer_get());
        u8->println("");
        u8->println("");
        u8->println(" [ENTER] SAVE ");
    }
}


//
void Wifi_keyboard(char key)
{
    //
    WifiEntry_keyboard(key);

    //
    Menu_clear();
}
