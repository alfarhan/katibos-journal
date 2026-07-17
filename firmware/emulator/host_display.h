#pragma once
#include <cstdint>

// Framebuffer bridge between the (real) firmware screen code drawing through
// host_st7305.cpp and the SDL render loop in host_main.cpp.
void host_fb_lock();
void host_fb_unlock();
bool host_fb_dirty();
void host_fb_clear_dirty();
const uint8_t *host_fb_data(); // LCD_W * LCD_H bytes, 0 = white, 1 = black
uint8_t *host_fb_mutable();    // same buffer, writable (host-side overlays)
int host_fb_width();
int host_fb_height();
