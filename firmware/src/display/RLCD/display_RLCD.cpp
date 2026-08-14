#include "display_RLCD.h"
#include "../display.h"
#include "app/app.h"

//
#include <SPI.h>
#include <ST73xxPins.h>
#include "U8g2_for_ST73XX.h"
#include "ST7305_4p2_BW_DisplayDriver.h"

// SETUP SPI for the DISPLAY
const ST73xxPins PINS{PIN_DC, PIN_CS, PIN_SCLK, PIN_MOSI, PIN_RST};
ST7305_4p2_BW_DisplayDriver display(PINS, SPI);
U8G2_FOR_ST73XX u8g2;

// screens
#include "ErrorScreen/ErrorScreen.h"
#include "WordProcessor/WordProcessor.h"
#include "Menu/Menu.h"
#include "Update/Update.h"

#include "service/Bidi/Bidi.h"
#include "service/Idle/Idle.h"
#include <string.h>

// Fonts used for labels (file titles, status bar). Latin glyphs come from the
// monospace profont17; Arabic from the connected-forms Arabic font - same pair
// idea as the word processor, so Arabic shapes and joins correctly outside it.
#define LBL_FONT u8g2_font_profont17_tf
#define LBL_FONT_ARABIC u8g2_font_10x20_t_arabic

// Shape + lay out a label, drawing it only when `draw` is set. Measuring runs
// the identical path, so a centered label is placed with the exact width it
// will occupy (a label may mix Arabic and Latin, i.e. two fonts, two metrics).
static int RLCD_shapedLabel(U8G2_FOR_ST73XX *u8, int x, int y, const char *utf8,
                            bool baseHintRTL, bool draw)
{
    if (!utf8 || !*utf8)
        return 0;

    static bidi::Cell cells[80];
    bool rtl = false;
    int n = bidi::layoutLine(utf8, (int)strlen(utf8), cells, 80, &rtl, baseHintRTL);

    int startX = x;
    for (int c = 0; c < n; c++)
    {
        u8->setFont(cells[c].arabic ? LBL_FONT_ARABIC : LBL_FONT);

        char b[4];
        int bl = bidi::utf8Encode(cells[c].glyph, b);
        b[bl] = 0;
        int w = u8->getUTF8Width(b);

        if (draw)
            u8->drawGlyph(x, y, cells[c].glyph);

        // overlay combining harakat centered over the base glyph (no advance)
        for (int m = 0; draw && m < cells[c].nmarks; m++)
        {
            u8->setFont(LBL_FONT_ARABIC);
            char mb[4];
            int ml = bidi::utf8Encode(cells[c].marks[m], mb);
            mb[ml] = 0;
            int mw = u8->getUTF8Width(mb);
            u8->drawGlyph(x + (w - mw) / 2, y, cells[c].marks[m]);
        }

        x += w;
    }

    u8->setFont(LBL_FONT); // restore the default label font for the caller
    return x - startX;
}

int RLCD_drawShapedLabel(U8G2_FOR_ST73XX *u8, int x, int y, const char *utf8, bool baseHintRTL)
{
    return RLCD_shapedLabel(u8, x, y, utf8, baseHintRTL, true);
}

int RLCD_shapedLabelWidth(U8G2_FOR_ST73XX *u8, const char *utf8, bool baseHintRTL)
{
    return RLCD_shapedLabel(u8, 0, 0, utf8, baseHintRTL, false);
}

// Hint bar geometry. HINT_PAD is the chip's inner padding, HINT_GAP the space
// between a key and its chip, HINT_SEP the space between two hints - the gap
// between pairs stays wider than the gap inside one so the pairs read as units.
#define HINT_PAD 4
#define HINT_GAP 6
#define HINT_SEP 14

int RLCD_hintBarWidth(U8G2_FOR_ST73XX *u8, const RLCD_Hint *hints, int n)
{
    u8->setFont(LBL_FONT);
    int w = 0;
    for (int i = 0; i < n; i++)
    {
        if (hints[i].key && *hints[i].key)
            w += u8->getUTF8Width(hints[i].key) + HINT_GAP;
        w += u8->getUTF8Width(hints[i].verb) + HINT_PAD * 2 + HINT_SEP;
    }
    return w > 0 ? w - HINT_SEP : 0;
}

int RLCD_drawHintBar(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8,
                     int x, int y, const RLCD_Hint *hints, int n)
{
    u8->setFont(LBL_FONT);
    for (int i = 0; i < n; i++)
    {
        if (hints[i].key && *hints[i].key)
        {
            u8->setCursor(x, y);
            u8->print(hints[i].key);
            x += u8->getUTF8Width(hints[i].key) + HINT_GAP;
        }

        int vw = u8->getUTF8Width(hints[i].verb);
        display->drawFilledRectangle(x, y - 15, x + vw + HINT_PAD * 2, y + 3, 1);
        u8->setForegroundColor(ST7305_COLOR_WHITE);
        u8->setBackgroundColor(ST7305_COLOR_BLACK);
        u8->setCursor(x + HINT_PAD, y);
        u8->print(hints[i].verb);
        u8->setForegroundColor(ST7305_COLOR_BLACK);
        u8->setBackgroundColor(ST7305_COLOR_WHITE);
        x += vw + HINT_PAD * 2 + HINT_SEP;
    }
    return x - HINT_SEP;
}

// Title bar: stripes across the band, title in a cleared tab centered over them.
// The tab is what makes the stripes read as chrome rather than noise, so it is
// cleared (not just drawn over) - the panel has no anti-aliasing to hide seams.
int RLCD_drawTitleBar(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8,
                      int x, int y, int w, int h, const char *title, int iconW)
{
    for (int sy = y + 4; sy <= y + h - 5; sy += 3)
        display->drawLine(x + 4, sy, x + w - 4, sy, 1);

    if (!title || !*title)
        return x;

    // shaped, so a title carrying Arabic renders (and measures) correctly
    int tw = RLCD_shapedLabelWidth(u8, title, false);
    int iconX = x + (w - tw - iconW) / 2;
    display->drawFilledRectangle(iconX - 8, y + 2, iconX + iconW + tw + 8, y + h - 2, 0);
    RLCD_drawShapedLabel(u8, iconX + iconW, y + h - 7, title, false);
    return iconX;
}

#define WIN_SHADOW 3

void RLCD_drawWindow(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8,
                     int x, int y, int w, int h, const char *title)
{
    display->drawFilledRectangle(x + WIN_SHADOW, y + WIN_SHADOW,
                                 x + w + WIN_SHADOW, y + h + WIN_SHADOW, 1);
    display->drawFilledRectangle(x, y, x + w, y + h, 0);
    display->drawRectangle(x, y, x + w, y + h, 1);

    if (title && *title)
    {
        const int barH = 22;
        RLCD_drawTitleBar(display, u8, x, y, w, barH, title);
        display->drawLine(x, y + barH, x + w, y + barH, 1);
    }
}

void RLCD_drawScrollbar(ST7305_4p2_BW_DisplayDriver *display, int x, int yTop, int yBottom,
                        int first, int visible, int total)
{
    if (total <= visible || visible <= 0)
        return;

    const int w = 10;
    display->drawRectangle(x, yTop, x + w, yBottom, 1);

    int track = yBottom - yTop - 4;
    int thumb = track * visible / total;
    if (thumb < 8)
        thumb = 8;
    // A paged list steps by a whole page, so the last page's first row can sit
    // past the last scrollable position - clamp or the thumb runs off the track.
    int span = total - visible;
    if (first > span)
        first = span;
    if (first < 0)
        first = 0;
    int off = span > 0 ? (track - thumb) * first / span : 0;
    display->drawFilledRectangle(x + 2, yTop + 2 + off, x + w - 2, yTop + 2 + off + thumb, 1);
}

int display_RLCD_core()
{
  // Render on core 0, the same core that runs keyboard_loop() and mutates the
  // editor buffer. Rendering used to run on core 1 for input responsiveness,
  // but WP_render reads shared editor state (buffer, linePositions[],
  // lineLengths[], the measureCharWidthAt function pointer, the JsonDocument
  // tree) with no locking. Fast typing let core 0 rewrite that state mid-render
  // on core 1; the torn read jumped through a corrupted pointer and panicked
  // with an illegal instruction (seen as a TG1WDT reset). Keeping edit and
  // render on one core serializes them. Background work (app_loop: sync, word
  // count) stays on core 1, so the offload that mattered is intact.
  return 0;
}

//
// Idle throttle hooks. The panel keeps showing the page in Low Power mode - it
// just refreshes it more cheaply - so there is nothing for the user to lose.
static void rlcd_idle_enter() { display.Low_Power_Mode(); }
static void rlcd_idle_exit() { display.High_Power_Mode(); }

void display_RLCD_setup()
{
  _log("DISPLAY RLCD SETUP\n");

  // The display driver owns SPI setup and the complete supplier startup sequence.
  display.initialize();

  // connect u8g2 procedures to TFT_eSPI
  u8g2.begin(display);
  u8g2.setFontMode(0);
  u8g2.setForegroundColor(ST7305_COLOR_BLACK);
  u8g2.setBackgroundColor(ST7305_COLOR_WHITE);

  idle_setup(rlcd_idle_enter, rlcd_idle_exit);
}

//
void display_RLCD_loop()
{
  static unsigned int last = millis();
  if (millis() - last > 100)
  {
    last = millis();

    idle_loop();

    JsonDocument &app = status();
    int screen = app["screen"].as<int>();
    int screen_prev = app["screen_prev"].as<int>();

    // ERROR SCREEN
    if (screen == ERRORSCREEN)
    {
      // setup only once
      if (screen != screen_prev)
        ErrorScreen_setup(&display, &u8g2);
      else
        // loop
        ErrorScreen_render(&display, &u8g2);
    }

    // SLEEP
    else if (screen == SLEEPSCREEN)
    {
      // redirect to WORDPROCESSOR
      app["screen"] = WORDPROCESSOR;
    }

    // WORD PROCESSOR
    else if (screen == WORDPROCESSOR)
    {
      // setup only once
      if (screen != screen_prev)
        WP_setup(&display, &u8g2);
      else
        // loop
        WP_render(&display, &u8g2);
    }

    // MENU SCREEN
    else if (screen == MENUSCREEN)
    {
      // setup only once
      if (screen != screen_prev)
        Menu_setup(&display, &u8g2);
      else
        // loop
        Menu_render(&display, &u8g2);
    }

    // UPDATE SCREEN
    else if (screen == UPDATESCREEN)
    {
      // setup only once
      if (screen != screen_prev)
        Update_setup(&display, &u8g2);
      else
        // loop
        Update_render(&display, &u8g2);
    }

    //
    app["screen_prev"] = screen;

    // Every screen tracks whether anything visible actually changed since
    // the last push, since display() always transfers the whole 30KB frame
    // buffer over SPI regardless of how much changed. The first tick after
    // switching screens (setup) always pushes once to show the initial frame.
    bool shouldDisplay = true;
    if (screen == screen_prev)
    {
      if (screen == ERRORSCREEN)
        shouldDisplay = ErrorScreen_needsDisplay();
      else if (screen == WORDPROCESSOR)
        shouldDisplay = WP_needsDisplay();
      else if (screen == MENUSCREEN)
        shouldDisplay = Menu_needsDisplay();
      else if (screen == UPDATESCREEN)
        shouldDisplay = Update_needsDisplay();
    }

    if (shouldDisplay)
      display.display();
  }
}

// Redirect the key press to the current GUI
void display_RLCD_keyboard(int key, bool pressed, int index)
{
  JsonDocument &app = status();
  int screen = app["screen"].as<int>();

  if (screen == ERRORSCREEN)
  {
    if (!pressed)
      ErrorScreen_keyboard(key);
  }

  else if (screen == WORDPROCESSOR)
  {
    // send the key stroke to word processor
    WP_keyboard(key, pressed, index);
  }

  else if (screen == MENUSCREEN)
  {
    if (!pressed)
      Menu_keyboard(key);
  }

  else if (screen == UPDATESCREEN)
  {
    if (!pressed)
      Update_keyboard(key);
  }
}

void display_RLCD_keyboard_report(uint8_t modifier, uint8_t reserved, uint8_t *keycodes)
{
  JsonDocument &app = status();
  int screen = app["screen"].as<int>();
}