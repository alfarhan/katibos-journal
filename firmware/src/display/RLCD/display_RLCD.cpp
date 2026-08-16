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
#include "service/Editor/Editor.h"
#include "service/Tools/TextUtil.h"
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
// ---- rest screens -----------------------------------------------------------
// Optional, and off by default: this panel is reflective, so there is no burn-in
// to protect against - a rest screen is decorative, and it costs you the sight of
// your own text. `config.screensaver` opts in.
static bool screensaverOn()
{
    return status()["config"]["screensaver"] | false;
}

// Words in the open document: the on-disk part plus what is still in the buffer,
// which is the same number the status bar shows.
static int rlcd_wordcount()
{
    JsonDocument &app = status();
    int i = app["config"]["file_index"].as<int>();
    return app["config"][format("wordcount_file_%d", i)].as<int>() +
           app["config"][format("wordcount_buffer_%d", i)].as<int>();
}

// The device as a mark. The smile is plotted as a circular arc rather than
// assembled from line segments - at this size a three-segment "curve" reads as a
// chevron, not a smile.
void RLCD_drawDeviceMark(ST7305_4p2_BW_DisplayDriver *display, int x, int y)
{
    display->drawRectangle(x, y, x + 84, y + 104, 1);
    display->drawRectangle(x + 1, y + 1, x + 83, y + 103, 1);
    display->drawRectangle(x + 13, y + 13, x + 71, y + 61, 1);

    display->drawFilledRectangle(x + 29, y + 26, x + 34, y + 32, 1); // eyes
    display->drawFilledRectangle(x + 50, y + 26, x + 55, y + 32, 1);

    const int cx = x + 42, cy = y + 26, r = 18;
    for (int dx = -14; dx <= 14; dx++)
    {
        int dy = (int)(sqrtf((float)(r * r - dx * dx)) + 0.5f);
        display->writePoint(cx + dx, cy + dy, true); // 2px: the 1px arc is faint
        display->writePoint(cx + dx, cy + dy - 1, true);
    }

    // keyboard deck below the screen
    display->drawFilledRectangle(x + 20, y + 72, x + 64, y + 77, 1);
    display->drawFilledRectangle(x + 12, y + 86, x + 72, y + 92, 1);
}

// The rest screen: the deck mark beside what you were
// writing and how far you got. One design for both states - the mark says which
// machine this is, the numbers say where you left off - with only the closing
// line differing, since a nap needs a key and a shut down needs the same key but
// pays a boot for it.
//
// Mark left, text right: the same arrangement the About screen uses, so the two
// read as the same family.
static void rlcd_draw_rest(bool asleep)
{
    JsonDocument &app = status();
    display.clearDisplay();

    // ---- the deck, drawn as a mark ----
    const int mx = 44, my = 84;
    RLCD_drawDeviceMark(&display, mx, my);

    // ---- what you were writing ----
    const int tx = 156;
    int idx = app["config"]["file_index"].as<int>();

    String title = app["config"][format("title_%d", idx)].as<String>();
    title.trim();
    if (title.isEmpty() || title == "null")
        title = "Untitled";
    u8g2.setFont(u8g2_font_profont17_tf);
    RLCD_drawShapedLabel(&u8g2, tx, my + 34, capUtf8(title, 20).c_str(), false);

    u8g2.setFont(u8g2_font_profont22_mf);
    String count = String(rlcd_wordcount()) + " words";
    u8g2.setCursor(tx, my + 70);
    u8g2.print(count.c_str());

    u8g2.setFont(u8g2_font_profont17_tf);

    // ---- the way back ----
    const char *hint = asleep ? "Press any key" : "Any key to carry on";
    u8g2.setCursor((400 - u8g2.getUTF8Width(hint)) / 2, 254);
    u8g2.print(hint);

    display.display();
}

// Idle throttle hooks. The panel is deliberately left in High Power mode: this
// panel's LPM refresh keeps redrawing the held frame with the wrong drive
// voltages and the text visibly rots while you are away - it looks like the
// device crashed. Nothing repaints while idle, so the decay just sits there
// until a keypress forces a full repaint. Staying in HPM costs only the panel
// booster; the real saving in this state is the main loop's delay(30).
static void rlcd_idle_enter()
{
    if (screensaverOn())
    {
        // The card is about to hide the text, so make sure it is on disk first.
        Editor::getInstance().saveFile();
        rlcd_draw_rest(false);
    }
}

static void rlcd_idle_exit()
{
    // Whatever screen is live has to repaint in full - the rest card overwrote it.
    Editor::getInstance().pageChanged = true;
    Menu_clear();
    status()["clear"] = true;
}

// Drawn while the chip is still awake, immediately before it sleeps; the panel
// then holds this frame on its own. Shown regardless of the screensaver setting -
// a device that looks dead is worse than one that says it is asleep.
static void rlcd_idle_sleep(bool deep)
{
    (void)deep;
    display.High_Power_Mode(); // make sure this frame actually lands
    rlcd_draw_rest(true);
}

// Depolarizing flush. YDP420H001-V3 is a NORMALLY WHITE reflective TFT, and on
// those a long-held frame leaves DC bias trapped in the liquid crystal - the
// image stays faintly visible under whatever is drawn next. Rewriting the same
// white pixel does NOT lift it: the charge only relaxes while the pixel is
// driven the OTHER way, so a clean boot (even off an erased chip) still shows
// the ghost. Full black and full white alternately drive every pixel to both
// polarities.
//
// Kept short deliberately. The dramatic ghost this was written for turned out to
// be a wrong panel profile (see RLCD_TYPE in platformio.ini) rather than trapped
// charge, and nothing parks this panel in LPM any more, so a long flush would be
// boot time spent on a problem that no longer occurs. Two cycles is hygiene.
static const int FLUSH_CYCLES = 2;
static const int FLUSH_HOLD_MS = 150;

static void rlcd_flush_retention()
{
  for (int i = 0; i < FLUSH_CYCLES; i++)
  {
    display.drawFilledRectangle(0, 0, 399, 299, ST7305_COLOR_BLACK);
    display.display();
    delay(FLUSH_HOLD_MS);

    display.clearDisplay();
    display.display();
    delay(FLUSH_HOLD_MS);
  }
}

void display_RLCD_setup()
{
  _log("DISPLAY RLCD SETUP\n");

  // The display driver owns SPI setup and the complete supplier startup sequence.
  display.initialize();

  rlcd_flush_retention();

  // connect u8g2 procedures to TFT_eSPI
  u8g2.begin(display);
  u8g2.setFontMode(0);
  u8g2.setForegroundColor(ST7305_COLOR_BLACK);
  u8g2.setBackgroundColor(ST7305_COLOR_WHITE);

  idle_setup(rlcd_idle_enter, rlcd_idle_exit, rlcd_idle_sleep);
}

//
// How often the screen may repaint. This is a latency floor, not a frame rate:
// a render costs ~38ms (median; p90 51) and every keystroke dirties the page, so
// at the old 100ms a character could wait ~140ms before it appeared. The render
// itself early-outs when nothing changed, so a tighter tick costs CPU only while
// something is actually moving, and the matrix now has its own 5ms scan task at
// higher priority than this loop - it cannot be starved by the extra drawing.
#define RLCD_TICK_MS 40

void display_RLCD_loop()
{
  static unsigned int last = millis();
  if (millis() - last > RLCD_TICK_MS)
  {
    last = millis();

    idle_loop();

    // While a rest card is up, don't let the live screen repaint over it. Safe to
    // skip: resting starts at 30s of stillness at the earliest and autosave fires
    // after 2, so there is nothing pending. It also spares the SPI pushes, which
    // is the point of resting.
    if (idle_active() && screensaverOn())
      return;

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