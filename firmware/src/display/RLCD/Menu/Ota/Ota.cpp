#include "Ota.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Updater/Ota.h"

// Blocking steps (Wi-Fi connect, download, flash) are deferred by one render so
// the "please wait" message paints + pushes before the UI freezes. `pending`
// holds the action to run; `settle` counts the frames to wait first.
static int pending = 0; // 0 none, 1 check, 2 apply
static int settle = 0;

void Ota_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();
    display->clearDisplay();
    status()["ota_state"] = OTA_CHECKING;
    status()["ota_message"] = "Checking for update...";
    status()["clear"] = true;
    pending = 1; // run ota_check() after the first frame paints
    settle = 1;
}

// Panel + font handles kept for the download's progress repaints, which happen
// inside ota_apply() rather than on a render tick.
static ST7305_4p2_BW_DisplayDriver *g_display = nullptr;
static U8G2_FOR_ST73XX *g_u8 = nullptr;

static void drawScreen(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();
    int st = app["ota_state"] | OTA_IDLE;

    Menu_drawHeader(display, u8, "SOFTWARE UPDATE");

    // status line, centered in the body
    u8->setFont(u8g2_font_profont17_tf);
    String msg = app["ota_message"].as<String>();
    if (!msg.isEmpty() && msg != "null")
    {
        int w = u8->getUTF8Width(msg.c_str());
        u8->setCursor((400 - w) / 2, 150);
        u8->print(msg.c_str());
    }

    // footer hint follows the state
    const char *hint;
    if (st == OTA_AVAILABLE)
        hint = "[Enter] install   [Esc] back";
    else if (st == OTA_DONE)
        hint = "[Enter] reboot";
    else if (st == OTA_CHECKING || st == OTA_DOWNLOADING)
        hint = "Please wait ...";
    else if (st == OTA_UPTODATE || st == OTA_ERROR)
        hint = "[Enter] retry   [Esc] back";
    else
        hint = "[Esc] back";
    display->drawLine(0, 276, 400, 276, 1);
    int hw = u8->getUTF8Width(hint);
    u8->setCursor((400 - hw) / 2, 296);
    u8->print(hint);

    // progress bar, drawn only while a download is running
    int pct = app["ota_progress"] | -1;
    if (st == OTA_DOWNLOADING && pct >= 0)
    {
        const int bx = 60, by = 180, bw = 280, bh = 18;
        display->drawRectangle(bx, by, bx + bw, by + bh, 1);
        int fill = (bw - 4) * pct / 100;
        if (fill > 0)
            display->drawFilledRectangle(bx + 2, by + 2, bx + 2 + fill, by + bh - 2, 1);
    }
}

// Repaint from inside the blocking download: nothing else draws while
// ota_apply() holds the render task, so going straight to the panel is safe.
static void redrawDuringDownload()
{
    if (!g_display || !g_u8)
        return;
    g_display->clearDisplay();
    drawScreen(g_display, g_u8);
    g_display->display();
}

void Ota_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    g_display = display;
    g_u8 = u8;
    ota_set_redraw(redrawDuringDownload);

    drawScreen(display, u8);

    // deferred blocking work (one frame after the message is shown)
    if (pending && settle > 0)
    {
        settle--;
        return;
    }
    if (pending == 1)
    {
        pending = 0;
        ota_check();
    }
    else if (pending == 2)
    {
        pending = 0;
        ota_apply();
    }
}

void Ota_keyboard(int key)
{
    JsonDocument &app = status();
    int st = app["ota_state"] | OTA_IDLE;

    // never bail out mid-network/flash
    bool busy = (st == OTA_CHECKING || st == OTA_DOWNLOADING);

    if ((key == 27 || key == MENU || key == 18) && !busy)
    {
        app["menu"]["state"] = app["menu"]["return"] | MENU_SETTINGS;
        return;
    }

    if (key == '\n' || key == '\r')
    {
        if (st == OTA_AVAILABLE)
        {
            app["ota_state"] = OTA_DOWNLOADING;
            // No percentage until bytes actually flow: the TLS handshake and the
            // GitHub redirect take ~5s, and a "0%" sitting there reads as stuck.
            app["ota_progress"] = -1;
            app["ota_message"] = "Connecting...";
            app["clear"] = true;
            pending = 2;
            settle = 1;
        }
        else if (st == OTA_DONE)
        {
            ota_reboot();
        }
        else if (st == OTA_UPTODATE || st == OTA_ERROR)
        {
            app["ota_state"] = OTA_CHECKING;
            app["ota_message"] = "Checking for update...";
            app["clear"] = true;
            pending = 1;
            settle = 1;
        }
    }
}
