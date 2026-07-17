#pragma once

#include "ST7305_4p2_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"

//
void Menu_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);

//
void Menu_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);

// Whether anything visible changed during the last Menu_render() call, so the
// caller knows whether pushing the frame buffer over SPI is worth doing.
bool Menu_needsDisplay();

// int (not char): the menu now routes full key codes so screens like Rename can
// receive Arabic codepoints (> 255) instead of a truncated byte.
void Menu_keyboard(int key);

//
void Menu_clear();

// Draw the FILES / SETTINGS tab strip. activeTab: 0 = FILES, 1 = SETTINGS.
void Menu_drawTabs(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, int activeTab);

// Draw a plain title header (same position/divider as the tabs) for the deeper
// drill-down screens, so every menu screen shares one header style.
void Menu_drawHeader(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, const char *title);

//
void Menu_sync(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);

