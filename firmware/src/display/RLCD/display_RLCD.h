#pragma once

// TFT_eSPI setup
#include <SPI.h>


//
void display_RLCD_setup();
void display_RLCD_loop();
int display_RLCD_core();

//
void display_RLCD_keyboard(int key, bool pressed, int index = -1);

// Draw a UTF-8 label with Arabic shaping + bidi reordering at baseline (x,y),
// using the menu/label fonts. Returns pixel width. Use for any label that may
// contain Arabic (file titles in menus, status bar) so it renders instead of
// coming out blank in the Latin-only font.
class U8G2_FOR_ST73XX;
int RLCD_drawShapedLabel(U8G2_FOR_ST73XX *u8, int x, int y, const char *utf8, bool baseHintRTL = false);
// Same layout, no drawing - for centering a label before it is drawn.
int RLCD_shapedLabelWidth(U8G2_FOR_ST73XX *u8, const char *utf8, bool baseHintRTL = false);

// Action hints: the key in plain text, the verb on an inverted chip next to it
// ("^S SAVE", "ESC BACK"). The chip is what carries the "this is pressable"
// signal, so keys need no brackets and the pairs stay readable in a row. Verbs
// are uppercase; "^" is Ctrl (Fn on the keypad), matching the Ctrl+/ cheat sheet.
struct RLCD_Hint
{
    const char *key;  // "ESC", "^S", "UP/DN" - empty for a verb-only chip
    const char *verb; // uppercase
};
class ST7305_4p2_BW_DisplayDriver;
int RLCD_hintBarWidth(U8G2_FOR_ST73XX *u8, const RLCD_Hint *hints, int n);
int RLCD_drawHintBar(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8,
                     int x, int y, const RLCD_Hint *hints, int n);
#define RLCD_HINTS(a) (a), (int)(sizeof(a) / sizeof((a)[0]))

// Window chrome. A title bar is a band of stripes with the title in a cleared
// tab pinned over them; a window adds an offset shadow and a frame around it.
// Use the title bar alone for a full-width screen header, the window for a
// dialog or an info panel floating on the screen (pass a null title for a plain
// framed box).
// Reserve iconW px before the title for a mark; returns the x where that mark
// goes (inside the cleared tab), so the caller draws it without measuring.
int RLCD_drawTitleBar(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8,
                      int x, int y, int w, int h, const char *title, int iconW = 0);
void RLCD_drawWindow(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8,
                     int x, int y, int w, int h, const char *title);

// Proportional scrollbar for a paged/scrolled list: track between yTop/yBottom
// with a thumb sized by visible/total and placed by `first` (index of the first
// visible row). Draws nothing when everything already fits.
void RLCD_drawScrollbar(ST7305_4p2_BW_DisplayDriver *display, int x, int yTop, int yBottom,
                        int first, int visible, int total);
