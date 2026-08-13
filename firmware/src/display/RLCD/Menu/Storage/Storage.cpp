#include "Storage.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/display_RLCD.h"

// Partition stats + note count, sampled once on entry: usedBytes() hits the FAT
// driver and the note count probes exists() up to the same cap the Home list
// uses, neither of which belongs in a per-tick render.
static size_t g_total = 0, g_used = 0;
static int g_notes = 0;

// Human size: MB above a megabyte, KB below - the numbers on this screen are
// read at a glance, not summed.
static String humanSize(size_t bytes)
{
    char out[24];
    if (bytes >= 1024 * 1024)
        snprintf(out, sizeof(out), "%.2f MB", bytes / (1024.0 * 1024.0));
    else
        snprintf(out, sizeof(out), "%u KB", (unsigned)((bytes + 1023) / 1024));
    return String(out);
}

// Disk icon: a 3.5" floppy - body, shutter, label.
static void drawDiskIcon(ST7305_4p2_BW_DisplayDriver *display, int x, int y)
{
    display->drawRectangle(x, y, x + 26, y + 26, 1);
    display->drawFilledRectangle(x + 7, y + 2, x + 19, y + 10, 1);
    display->drawFilledRectangle(x + 12, y + 3, x + 15, y + 9, 0);
    display->drawRectangle(x + 5, y + 15, x + 21, y + 25, 1);
}

//
void Storage_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    //
    Menu_clear();

    g_total = gfs()->totalBytes();
    g_used = gfs()->usedBytes();
    g_notes = 0;
    for (int i = 0; i < 100; i++)
        if (gfs()->exists(format("/%d.txt", i).c_str()))
            g_notes++;

    // Turn on Storage
    JsonDocument &app = status();
    app["massStorage"] = true;
}

//
void Storage_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    // Get-Info panel: what's on the disk, so the writer can see there's room
    // before copying files off over USB. The panel is the whole screen here -
    // no title bar and no footer, since the window carries its own title and
    // Esc is the only key drive mode listens to.
    const int wx = 44, wy = 30, ww = 312, wh = 240;
    RLCD_drawWindow(display, u8, wx, wy, ww, wh, "STORAGE");

    u8->setFont(u8g2_font_profont17_tf);
    drawDiskIcon(display, wx + 22, wy + 34);
    u8->setCursor(wx + 62, wy + 56);
    u8->print("Internal disk");

    if (g_total > 0)
    {
        // usage gauge: outline with the used share filled
        const int gx = wx + 22, gy = wy + 76, gw = ww - 44, gh = 18;
        display->drawRectangle(gx, gy, gx + gw, gy + gh, 1);
        int fill = (int)((gw - 4) * (double)g_used / (double)g_total);
        if (fill > 0)
            display->drawFilledRectangle(gx + 2, gy + 2, gx + 2 + fill, gy + gh - 2, 1);

        int pct = (int)(100.0 * g_used / g_total + 0.5);
        char used[40];
        snprintf(used, sizeof(used), "%s  (%d%%)", humanSize(g_used).c_str(), pct);

        const int lx = wx + 22, vx = wx + 110;
        u8->setCursor(lx, wy + 124); u8->print("Total");
        u8->setCursor(vx, wy + 124); u8->print(humanSize(g_total).c_str());
        u8->setCursor(lx, wy + 148); u8->print("Used");
        u8->setCursor(vx, wy + 148); u8->print(used);
        u8->setCursor(lx, wy + 172); u8->print("Free");
        u8->setCursor(vx, wy + 172); u8->print(humanSize(g_total - g_used).c_str());
        u8->setCursor(lx, wy + 196); u8->print("Notes");
        u8->setCursor(vx, wy + 196); u8->print(String(g_notes).c_str());
    }
    else
    {
        u8->setCursor(wx + 22, wy + 110);
        u8->print("Size unavailable");
    }

    u8->setCursor(wx + 22, wy + 222);
    u8->print("Connect USB to PC");
}

//
void Storage_keyboard(char key)
{
    _debug("Storage_Keyboard %d\n", key);
    JsonDocument &app = status();

    // MENU - SELECTED ACTION
    if (key == 27 || key == MENU)
    {
        // Go back to Home
        _log("Exit Mass Storage Started\n");

        // turn off USB drive
        app["massStorage"] = false;

        // wait until the storage is off
        while (true)
        {
            //
            _log("Checking when the device is ejected\n");

            //
            if (app["massStorageStarted"].as<bool>() == false)
            {
                _log("Detected device is ejected. Rebooting\n");

                //
                delay(3000);

                // restart
                ESP.restart();                

                //
                break;
            }

            delay(1000);
        }

        // Move to home screen
        app["menu"]["state"] = MENU_HOME;
    }
}