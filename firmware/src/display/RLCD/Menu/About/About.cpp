#include "About.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/display_RLCD.h"

void About_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();
    display->clearDisplay();
}

void About_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_drawHeader(display, u8, "ABOUT");

    RLCD_drawDeviceMark(display, 34, 108);

    // name, version, then who made it - one block beside the mark
    u8->setFont(u8g2_font_profont22_mf);
    u8->setCursor(146, 144);
    u8->print("katibOS");

    u8->setFont(u8g2_font_profont22_tf);
    u8->setCursor(146, 170);
    u8->print(KATIBOS_VERSION);
    u8->setCursor(146, 200);
    u8->print("Fouad Alfarhan");
    u8->setCursor(146, 222);
    u8->print("@alfarhan on github");
}

void About_keyboard(int key)
{
    if (key == 27 || key == MENU || key == 18 || key == 'B' || key == 'b' || key == '\b')
        status()["menu"]["state"] = status()["menu"]["return"] | MENU_SETTINGS;
}
