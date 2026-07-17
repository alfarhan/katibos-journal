#include "Timezone.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "app/Config/Config.h"
#include "service/Clock/Clock.h"

#define TZ_MIN (-12 * 60) // UTC-12:00
#define TZ_MAX (14 * 60)  // UTC+14:00
#define TZ_STEP 30        // half-hour steps cover :30 zones

static int sel_tz = 180; // edited offset (minutes), committed on close

String tz_label(int minutes)
{
    char sign = minutes < 0 ? '-' : '+';
    int a = minutes < 0 ? -minutes : minutes;
    char buf[16];
    snprintf(buf, sizeof(buf), "UTC%c%d:%02d", sign, a / 60, a % 60);
    return String(buf);
}

void Timezone_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();
    display->clearDisplay();
    sel_tz = status()["config"]["timezone"] | 180;
}

void Timezone_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_drawHeader(display, u8, "TIME ZONE");

    // big offset, centered
    u8->setFont(u8g2_font_profont29_mf);
    String off = "< " + tz_label(sel_tz) + " >";
    u8->setCursor((400 - u8->getUTF8Width(off.c_str())) / 2, 150);
    u8->print(off.c_str());

    // live preview of the current local time at this offset
    u8->setFont(u8g2_font_profont17_tf);
    String now = "now  " + clock_timestr_tz(sel_tz);
    u8->setCursor((400 - u8->getUTF8Width(now.c_str())) / 2, 196);
    u8->print(now.c_str());

    display->drawLine(0, 276, 400, 276, 1);
    const char *hint = "[<- ->] adjust   [Esc] save";
    u8->setCursor((400 - u8->getUTF8Width(hint)) / 2, 296);
    u8->print(hint);
}

static void commit()
{
    JsonDocument &app = status();
    app["config"]["timezone"] = sel_tz;
    config_save();
    app["menu"]["state"] = app["menu"]["return"] | MENU_SETTINGS;
}

void Timezone_keyboard(int key)
{
    if (key == 18 || key == '-' || key == '_') // Left / minus
    {
        sel_tz -= TZ_STEP;
        if (sel_tz < TZ_MIN)
            sel_tz = TZ_MIN;
        Menu_clear();
        return;
    }
    if (key == 19 || key == '+' || key == '=') // Right / plus
    {
        sel_tz += TZ_STEP;
        if (sel_tz > TZ_MAX)
            sel_tz = TZ_MAX;
        Menu_clear();
        return;
    }
    if (key == 27 || key == MENU || key == '\n' || key == '\r' ||
        key == 'B' || key == 'b' || key == '\b')
    {
        commit();
        return;
    }
}
