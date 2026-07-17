#pragma once

#include <Arduino.h>
#include "ST7305_4p2_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"

// TIME ZONE screen: pick the local UTC offset (config["timezone"], minutes).
// Opened from SETTINGS; Left/Right adjust by 30 min, Esc/Enter saves and returns.
void Timezone_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void Timezone_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void Timezone_keyboard(int key);

// "UTC+3:00" for an offset in minutes — shared so SETTINGS can show the value.
String tz_label(int minutes);
