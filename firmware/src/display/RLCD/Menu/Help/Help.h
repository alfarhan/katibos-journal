#pragma once

#include "ST7305_4p2_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"

// HELP: static keyboard cheat-sheet, opened from the SETTINGS tab.
void Help_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void Help_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void Help_keyboard(int key);

// Editor-context overlay (Ctrl+/ in the word processor): only the editor-
// relevant sections, drawn over the page. Reuses the same cheat-sheet data.
void Help_render_editor(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
