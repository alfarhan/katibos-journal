#pragma once

#include "ST7305_4p2_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"

//
void Home_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);

// 
void Home_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);

// 
void Home_keyboard(char key);

// Put the cursor in the cards half on the next entry (Ctrl+. from the editor,
// which used to open the SETTINGS tab).
void Home_focusCards(bool on);
