#include "Sync.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
//
#include "service/WifiEntry/WifiEntry.h"
#include "service/Sync/Sync.h"
#include "service/Editor/Editor.h"

//
void Sync_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    _log("Sync_setup\n");

    //
    Menu_clear();

    // load wifi
    wifi_config_load();

    // the menu Sync screen always runs a full (all-files) sync
    status()["sync_scope"] = "all";

    // init sync
    sync_init();
}

//
void Sync_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    //
    JsonDocument &app = status();

    Menu_drawHeader(display, u8, "SYNC & BACKUP");

    // SYNC STATE
    int sync_state = app["sync_state"].as<int>();

    // resolve the line to show for the current state (and fire any side effects)
    String message;
    if (sync_state == SYNC_START)
    {
        message = "Preparing sync ...";
        sync_start_request();
    }
    else if (sync_state == SYNC_STARTED || sync_state == SYNC_PROGRESS)
        message = app["sync_message"].as<String>();
    else if (sync_state == SYNC_ERROR)
        message = app["sync_error"].as<String>();
    else if (sync_state == SYNC_COMPLETED)
        message = "Sync completed.";

    // status line centered in the body (between the header rule and the footer)
    u8->setFont(u8g2_font_profont17_tf);
    if (message != "")
    {
        int w = u8->getUTF8Width(message.c_str());
        u8->setCursor((400 - w) / 2, 152);
        u8->print(message.c_str());
    }

    // footer: divider + hint (matches Settings/Help). Terminal states invite a
    // key press to leave; while syncing, ask the writer to wait.
    bool done = (sync_state == SYNC_COMPLETED || sync_state == SYNC_ERROR);
    const char *hint = done ? "Press any key to exit" : "Please wait ...";
    display->drawLine(0, 276, 400, 276, 1);
    int hw = u8->getUTF8Width(hint);
    u8->setCursor((400 - hw) / 2, 296);
    u8->print(hint);
}

//
void Sync_keyboard(char key)
{
    //
    JsonDocument &app = status();

    // SYNC STATE
    int sync_state = app["sync_state"].as<int>();
    if (sync_state == SYNC_COMPLETED || sync_state == SYNC_ERROR)
    {
        // return to the tab this screen was opened from, not the editor
        app["menu"]["state"] = app["menu"]["return"] | MENU_SETTINGS;
    }
}
