#pragma once

#include "ST7305_4p2_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"

// Rename screen: edit the title of the currently selected file. A typed title
// becomes a manual title (wins over the auto first-line title); saving a blank
// reverts to the auto title.
void Rename_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void Rename_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void Rename_keyboard(int key);
