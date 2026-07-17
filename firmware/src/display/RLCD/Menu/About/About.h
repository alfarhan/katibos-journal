#pragma once

#include "ST7305_4p2_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"

// ABOUT screen: device identity — MicroJournal firmware + katibOS layer
// versions. Opened from SETTINGS; Esc/Back returns there.
void About_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void About_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void About_keyboard(int key);
