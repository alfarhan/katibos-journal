#include "DeviceName.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Buffer/BufferService.h"
#include "service/Tools/TextUtil.h"

// Text entry for the Wi-Fi hostname (config "device_name"). Same blank-field
// pattern as Rename; menu screens force the US layout, so input is ASCII.
void DeviceName_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    buffer_clear();
    Menu_clear();
}

void DeviceName_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();
    Menu_drawHeader(display, u8, "DEVICE NAME");

    const int xl = 20;
    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(xl, 70);
    u8->print("Wi-Fi hostname");

    const int bx = xl, by = 84, bw = 360, bh = 32;
    display->drawRectangle(bx, by, bx + bw, by + bh, 1);
    u8->setCursor(bx + 10, by + 22);
    u8->print(capUtf8(buffer_get(), 30).c_str());
    u8->drawGlyph(u8->getCursorX(), by + 22, '_');

    u8->setCursor(xl, 150);
    u8->print("Blank = MICROJOURNAL.");

    String current = app["config"]["device_name"].as<String>();
    if (current.isEmpty() || current == "null")
        current = "MICROJOURNAL";
    u8->setCursor(xl, 190);
    u8->print("Current:  ");
    u8->print(current.c_str());

    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(xl, 296);
    u8->print("[ENT] save   [ESC] cancel");
}

void DeviceName_keyboard(int key)
{
    JsonDocument &app = status();

    if (key == '\n' || key == '\r')
    {
        String t = String(buffer_get());
        t.trim();
        if (t.isEmpty())
            app["config"].remove("device_name"); // revert to default hostname
        else
            app["config"]["device_name"] = capUtf8(t, 32);
        config_save();
        buffer_clear();
        app["menu"]["state"] = app["menu"]["return"] | MENU_PREFS;
    }
    else if (key == 27 || key == MENU)
    {
        buffer_clear();
        app["menu"]["state"] = app["menu"]["return"] | MENU_PREFS;
    }
    else if (key == '\b' || key == 127)
    {
        if (strlen(buffer_get()) > 0)
            buffer_remove();
    }
    // printable ASCII only (a hostname has no Arabic/space anyway)
    else if (key >= 32 && key < 127)
    {
        buffer_add((char)key);
    }
    Menu_clear();
}
