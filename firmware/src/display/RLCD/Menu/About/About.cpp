#include "About.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"

void About_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();
    display->clearDisplay();
}

// label/value row, value right-aligned
static void row(U8G2_FOR_ST73XX *u8, int y, const char *label, const char *value)
{
    const int xLabel = 20, xRight = 384;
    u8->setCursor(xLabel, y);
    u8->print(label);
    u8->setCursor(xRight - u8->getUTF8Width(value), y);
    u8->print(value);
}

void About_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_drawHeader(display, u8, "ABOUT");

    u8->setFont(u8g2_font_profont17_tf);
    row(u8, 84, "MicroJournal", VERSION);
    row(u8, 120, "katibOS", KATIBOS_VERSION);

    // rule, then the katibOS author credit
    display->drawLine(20, 144, 384, 144, 1);
    u8->setCursor(20, 178);
    u8->print("Fouad Alfarhan");
    u8->setCursor(20, 208);
    u8->print("@alfarhan on github");

    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(8, 296);
    u8->print("[Esc] back");
}

void About_keyboard(int key)
{
    if (key == 27 || key == MENU || key == 18 || key == 'B' || key == 'b' || key == '\b')
        status()["menu"]["state"] = status()["menu"]["return"] | MENU_SETTINGS;
}
