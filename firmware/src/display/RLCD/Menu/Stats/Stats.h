#pragma once

#include "ST7305_4p2_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"

// STATS tab: writing stats — today vs daily goal, streak, this session, and the
// journal-wide word total. [+/-] adjusts the goal. Left arrow returns to SETTINGS.
void Stats_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void Stats_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void Stats_keyboard(int key);
