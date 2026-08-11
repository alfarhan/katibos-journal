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
#include <string.h>

// Fonts used for labels (file titles, status bar). Latin glyphs come from the
// monospace profont17; Arabic from the connected-forms Arabic font - same pair
// idea as the word processor, so Arabic shapes and joins correctly outside it.
#define LBL_FONT u8g2_font_profont17_tf
#define LBL_FONT_ARABIC wp_arabic_font()

int RLCD_drawShapedLabel(U8G2_FOR_ST73XX *u8, int x, int y, const char *utf8, bool baseHintRTL)
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

        u8->drawGlyph(x, y, cells[c].glyph);

        // overlay combining harakat centered over the base glyph (no advance)
        for (int m = 0; m < cells[c].nmarks; m++)
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
}

//
void display_RLCD_loop()
{
  static unsigned int last = millis();
  if (millis() - last > 100)
  {
    last = millis();

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