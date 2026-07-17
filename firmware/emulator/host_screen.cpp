// Small bridge so host_main (SDL/event side) can read & override the firmware's
// current screen / menu-state without including all of app.h's machinery, and
// can tell when one of the faked screens is showing so it can stamp a badge.
#include <ArduinoJson.h>
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/Menu/Menu.h"

int host_screen()
{
    return status()["screen"].as<int>();
}

int host_menu_state()
{
    return status()["menu"]["state"].as<int>();
}

void host_set_screen(int s)
{
    status()["screen"] = s;
}

void host_set_menu_state(int m)
{
    status()["menu"]["state"] = m;
}

void host_menu_repaint()
{
    Menu_clear();
}

// True on screens the emulator can't really drive (BLE keyboard, WiFi, USB
// Drive mode), so the SDL blit / EMU_DUMP path overlays a "FAKE" badge. The
// Sync screen is NOT fake anymore — it does real HTTP syncing via libcurl.
bool host_is_fake_screen()
{
    int screen = host_screen();
    if (screen == KEYBOARDSCREEN)
        return true;
    if (screen == MENUSCREEN)
    {
        int m = host_menu_state();
        return m == MENU_WIFI || m == MENU_STORAGE;
    }
    return false;
}
