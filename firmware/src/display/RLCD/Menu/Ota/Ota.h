#pragma once

#include "ST7305_4p2_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"

// SOFTWARE UPDATE screen (OTA over Wi-Fi). Opened from SETTINGS; checks for a
// newer firmware via the manifest in config["update"]["url"], offers to install,
// and reboots. Esc/Back returns to SETTINGS (ignored mid-download).
void Ota_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void Ota_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);
void Ota_keyboard(int key);
