#pragma once

#include "ST7305_4p2_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"

// The system actions, as cards. There is no longer a SETTINGS screen of its own -
// the combined Home screen draws these in its lower half - so this is the model
// (list, labels, keys, icons, dispatch) rather than a screen.

// ---- card model, shared with the combined Home screen -----------------------
// Home draws the same cards in its lower half, so the list, labels, keys, icons
// and dispatch stay defined here rather than being copied.
int Settings_cards(int *ids, int max);
const char *Settings_cardLabel(int id);
char Settings_cardKey(int id);
void Settings_drawCard(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8,
                       int id, int x, int y, int w, int h, bool focused);
void Settings_openCard(int id);
// Letter fast-paths (P/W/S/K/D/U/H/A/T). True when the key was one of them.
bool Settings_letter(int key);
// The Restart confirm lives with the cards, so whichever screen shows them can
// render and answer it.
bool Settings_confirmActive();
void Settings_drawConfirm(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void Settings_confirmKey(int key);
