#include "Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "../WordProcessor/WordProcessor.h"

// Menu Sub Modules
#include "Home/Home.h"
#include "Sync/Sync.h"
#include "Clear/Clear.h"
#include "Layout/Layout.h"
#include "Wifi/Wifi.h"
#include "Storage/Storage.h"
#include "Rename/Rename.h"
#include "Settings/Settings.h"
#include "Help/Help.h"
#include "Stats/Stats.h"
#include "About/About.h"
#include "Ota/Ota.h"
#include "Timezone/Timezone.h"
#include "SyncProvider/SyncProvider.h"
#include "Preferences/Preferences.h"
#include "DeviceName/DeviceName.h"
#include "FactoryReset/FactoryReset.h"
#include "service/Clock/Clock.h"

// state
bool menu_clear = false;

// Whether the last Menu_render() actually changed anything visible - the
// caller uses this to decide whether the (expensive, full 30KB SPI) panel
// refresh is worth doing this tick.
static bool needsDisplay = true;

// 0 - home
// 1 - sync
// 2 - delete file confirm
// 3 - keyboard layout
int menu_state_prev = -1;

//
void Menu_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    //
    JsonDocument &app = status();

    // refresh the background
    menu_clear = true;

    // Default to FILES, unless a one-shot target was requested (e.g. Ctrl+, from
    // the editor asks for Preferences). Consume the request so the next plain
    // menu entry goes to FILES as usual.
    int target = app["menu"]["goto"] | MENU_HOME;
    app["menu"].remove("goto");
    app["menu"]["state"] = target;

    // Force the sub-screen's setup to run on every menu entry (not just on a
    // sub-state change). Home_setup re-scans the file list here, so a file
    // created/cleared in the editor since last time shows up correctly.
    menu_state_prev = -1;

    //
    
}

void Menu_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();

    // check if app clear is set as well
    if (app["clear"].as<bool>() == true)
    {
        // reset global clear flag
        app["clear"] = false;

        // clear screen
        menu_clear = true;
    }

    if (!menu_clear)
    {
        // nothing is changed
        // skip this rendering cycle
        needsDisplay = false;
        return;
    }

    // decide whether the panel actually needs to be pushed over SPI this tick
    needsDisplay = true;

    // clear screen
    {
        //
        display->clearDisplay();

        //
        menu_clear = false;
    }

    // The title/version moved into Settings; the tab strip (drawn by the FILES
    // and SETTINGS sub-screens) is now the header. Just set the default font.
    u8->setFont(u8g2_font_profont17_tf);

    // draw sub module of menu
    int menu_state = app["menu"]["state"].as<int>();

    if (menu_state == MENU_HOME)
    {
        if (menu_state_prev != menu_state)
            Home_setup(display, u8);

        Home_render(display, u8);
    }
    else if (menu_state == MENU_LAYOUT)
    {
        if (menu_state_prev != menu_state)
            Layout_setup(display, u8);

        Layout_render(display, u8);
    }
    else if (menu_state == MENU_SYNC)
    {
        if (menu_state_prev != menu_state)
            Sync_setup(display, u8);

        Sync_render(display, u8);
    }
    else if (menu_state == MENU_CLEAR)
    {
        if (menu_state_prev != menu_state)
            Clear_setup(display, u8);

        Clear_render(display, u8);
    }
    else if (menu_state == MENU_WIFI)
    {
        if (menu_state_prev != menu_state)
            Wifi_setup(display, u8);

        Wifi_render(display, u8);
    }
    else if (menu_state == MENU_STORAGE)
    {
        if (menu_state_prev != menu_state)
            Storage_setup(display, u8);

        Storage_render(display, u8);
    }
    else if (menu_state == MENU_RENAME)
    {
        if (menu_state_prev != menu_state)
            Rename_setup(display, u8);

        Rename_render(display, u8);
    }
    else if (menu_state == MENU_SETTINGS)
    {
        if (menu_state_prev != menu_state)
            Settings_setup(display, u8);

        Settings_render(display, u8);
    }
    else if (menu_state == MENU_HELP)
    {
        if (menu_state_prev != menu_state)
            Help_setup(display, u8);

        Help_render(display, u8);
    }
    else if (menu_state == MENU_STATS)
    {
        if (menu_state_prev != menu_state)
            Stats_setup(display, u8);

        Stats_render(display, u8);
    }
    else if (menu_state == MENU_ABOUT)
    {
        if (menu_state_prev != menu_state)
            About_setup(display, u8);

        About_render(display, u8);
    }
    else if (menu_state == MENU_UPDATE)
    {
        if (menu_state_prev != menu_state)
            Ota_setup(display, u8);

        Ota_render(display, u8);
    }
    else if (menu_state == MENU_TIMEZONE)
    {
        if (menu_state_prev != menu_state)
            Timezone_setup(display, u8);

        Timezone_render(display, u8);
    }
    else if (menu_state == MENU_SYNCPROV)
    {
        if (menu_state_prev != menu_state)
            SyncProvider_setup(display, u8);

        SyncProvider_render(display, u8);
    }
    else if (menu_state == MENU_PREFS)
    {
        if (menu_state_prev != menu_state)
            Preferences_setup(display, u8);

        Preferences_render(display, u8);
    }
    else if (menu_state == MENU_DEVNAME)
    {
        if (menu_state_prev != menu_state)
            DeviceName_setup(display, u8);

        DeviceName_render(display, u8);
    }
    else if (menu_state == MENU_FACTORY)
    {
        if (menu_state_prev != menu_state)
            FactoryReset_setup(display, u8);

        FactoryReset_render(display, u8);
    }

    // save prev state
    menu_state_prev = menu_state;
}

bool Menu_needsDisplay()
{
    return needsDisplay;
}

//
void Menu_keyboard(int key)
{
    //
    JsonDocument &app = status();

    // clear background for every key stroke
    Menu_clear();

    // based on the current menu state
    int menu_state = app["menu"]["state"].as<int>();

    // Ctrl+Shift+U from any menu screen → full sync. Jump to the SYNC screen
    // (which runs it and shows progress); remember the tab to return to.
    if (key == SYNC_ALL)
    {
        app["sync_scope"] = "all";
        if (menu_state != MENU_SYNC)
            app["menu"]["return"] = menu_state;
        app["menu"]["state"] = MENU_SYNC;
        return;
    }

    if (menu_state == MENU_HOME)
    {
        Home_keyboard(key);
        return;
    }

    // LAYOUT MENU
    else if (menu_state == MENU_LAYOUT)
    {
        Layout_keyboard(key);
        return;
    }

    // SYNC MENU
    else if (menu_state == MENU_SYNC)
    {
        Sync_keyboard(key);
        return;
    }

    // Delete file
    else if (menu_state == MENU_CLEAR)
    {
        Clear_keyboard(key);
        return;
    }

    // Wifi
    else if (menu_state == MENU_WIFI)
    {
        Wifi_keyboard(key);
        return;
    }

    // Storage
    else if (menu_state == MENU_STORAGE)
    {
        Storage_keyboard(key);
        return;
    }

    // Rename (takes the full int key so Arabic codepoints survive)
    else if (menu_state == MENU_RENAME)
    {
        Rename_keyboard(key);
        return;
    }

    // Settings
    else if (menu_state == MENU_SETTINGS)
    {
        Settings_keyboard(key);
        return;
    }

    // Help
    else if (menu_state == MENU_HELP)
    {
        Help_keyboard(key);
        return;
    }

    // Stats
    else if (menu_state == MENU_STATS)
    {
        Stats_keyboard(key);
        return;
    }
    // About
    else if (menu_state == MENU_ABOUT)
    {
        About_keyboard(key);
        return;
    }
    // Software Update (OTA)
    else if (menu_state == MENU_UPDATE)
    {
        Ota_keyboard(key);
        return;
    }
    // Time zone
    else if (menu_state == MENU_TIMEZONE)
    {
        Timezone_keyboard(key);
        return;
    }
    else if (menu_state == MENU_SYNCPROV)
    {
        SyncProvider_keyboard(key);
        return;
    }
    else if (menu_state == MENU_PREFS)
    {
        Preferences_keyboard(key);
        return;
    }
    else if (menu_state == MENU_DEVNAME)
    {
        DeviceName_keyboard(key);
        return;
    }
    else if (menu_state == MENU_FACTORY)
    {
        FactoryReset_keyboard(key);
        return;
    }
}

//
void Menu_clear()
{
    menu_clear = true;
}

// Shared FILES / SETTINGS tab strip, drawn just under the title bar. The active
// tab gets an inverse bar; the inactive one is plain. ←/→ switch between them.
void Menu_drawTabs(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, int activeTab)
{
    int y = 18;
    u8->setFont(u8g2_font_profont17_tf);

    // FILES
    if (activeTab == 0)
    {
        display->drawFilledRectangle(6, y - 14, 70, y + 4, 1);
        u8->setForegroundColor(ST7305_COLOR_WHITE);
        u8->setBackgroundColor(ST7305_COLOR_BLACK);
    }
    u8->setCursor(12, y);
    u8->print("FILES");
    if (activeTab == 0)
    {
        u8->setForegroundColor(ST7305_COLOR_BLACK);
        u8->setBackgroundColor(ST7305_COLOR_WHITE);
    }

    // SETTINGS
    if (activeTab == 1)
    {
        display->drawFilledRectangle(78, y - 14, 170, y + 4, 1);
        u8->setForegroundColor(ST7305_COLOR_WHITE);
        u8->setBackgroundColor(ST7305_COLOR_BLACK);
    }
    u8->setCursor(84, y);
    u8->print("SETTINGS");
    if (activeTab == 1)
    {
        u8->setForegroundColor(ST7305_COLOR_BLACK);
        u8->setBackgroundColor(ST7305_COLOR_WHITE);
    }

    // STATS
    if (activeTab == 2)
    {
        display->drawFilledRectangle(178, y - 14, 240, y + 4, 1);
        u8->setForegroundColor(ST7305_COLOR_WHITE);
        u8->setBackgroundColor(ST7305_COLOR_BLACK);
    }
    u8->setCursor(184, y);
    u8->print("STATS");
    if (activeTab == 2)
    {
        u8->setForegroundColor(ST7305_COLOR_BLACK);
        u8->setBackgroundColor(ST7305_COLOR_WHITE);
    }

    // today's date, right-aligned in the header band (shared across FILES /
    // SETTINGS / STATS); "----------" until the clock is set or synced
    {
        u8->setForegroundColor(ST7305_COLOR_BLACK);
        u8->setBackgroundColor(ST7305_COLOR_WHITE);
        String ds = clock_datestr();
        u8->setCursor(392 - u8->getUTF8Width(ds.c_str()), y);
        u8->print(ds.c_str());
    }

    // divider under the header tabs
    display->drawLine(0, 28, 400, 28, 1);
}

void Menu_drawHeader(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, const char *title)
{
    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(8, 18);
    u8->print(title);
    display->drawLine(0, 28, 400, 28, 1);
}
