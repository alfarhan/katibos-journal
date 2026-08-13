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

// The device, drawn as a mark: a deck with "كاتب" on its screen. Gives the
// About screen a face the way the Happy Mac does, in the script the OS is for.
static void drawDeviceMark(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, int x, int y)
{
    display->drawRectangle(x, y, x + 84, y + 104, 1);
    display->drawRectangle(x + 1, y + 1, x + 83, y + 103, 1);
    display->drawRectangle(x + 13, y + 13, x + 71, y + 61, 1);

    RLCD_drawShapedLabel(u8, x + 22, y + 40, "كاتب", true);

    // keyboard deck below the screen
    display->drawFilledRectangle(x + 20, y + 72, x + 64, y + 77, 1);
    display->drawFilledRectangle(x + 12, y + 86, x + 72, y + 92, 1);
}

void About_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_drawHeader(display, u8, "ABOUT");

    drawDeviceMark(display, u8, 34, 108);

    // name, version, then who made it - one block beside the mark
    u8->setFont(u8g2_font_profont22_mf);
    u8->setCursor(146, 144);
    u8->print("katibOS");

    u8->setFont(u8g2_font_profont17_tf);
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
