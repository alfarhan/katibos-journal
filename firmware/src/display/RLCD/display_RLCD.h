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
