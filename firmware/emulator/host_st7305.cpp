// Host replacement for ST7305_4p2_BW_DisplayDriver. Implements the exact public
// API declared in the real header, but instead of pushing pixels over SPI it
// writes into a logical 300(w) x 400(h) 1-byte-per-pixel framebuffer that the
// SDL main thread reads. ST73XX_UI.cpp (geometry) and U8g2_for_ST73XX.cpp
// (fonts) are the *real* firmware sources and draw through writePoint().
#include "ST7305_4p2_BW_DisplayDriver.h"
#include <cstring>
#include <mutex>

#include "host_display.h"

// The firmware draws in a LANDSCAPE logical space (x in [0,400), y in [0,300)).
// On-device the driver rotates that onto the portrait 300x400 panel, but for a
// readable emulator window we keep the framebuffer in the logical landscape
// orientation: 400 wide x 300 tall, fb[y*400 + x], no rotation. This is what
// the typewriter user actually sees.
#define LCD_W 400 // framebuffer width  (logical x)
#define LCD_H 300 // framebuffer height (logical y)
#define PANEL_W 300
#define PANEL_H 400

// front buffer (what writePoint draws into) and the snapshot handed to SDL.
static uint8_t g_fb[LCD_W * LCD_H];
static uint8_t g_shared[LCD_W * LCD_H];
static std::mutex g_mutex;
static bool g_dirty = false;

// ---- host accessors used by host_main / EMU_DUMP --------------------------
void host_fb_lock() { g_mutex.lock(); }
void host_fb_unlock() { g_mutex.unlock(); }
bool host_fb_dirty() { return g_dirty; }
void host_fb_clear_dirty() { g_dirty = false; }
const uint8_t *host_fb_data() { return g_shared; }
uint8_t *host_fb_mutable() { return g_shared; } // host overlay (e.g. FAKE badge)
int host_fb_width() { return LCD_W; }
int host_fb_height() { return LCD_H; }

// ---- driver ---------------------------------------------------------------
// ST73XX_UI(w, h): the real geometry base. The panel is 300 wide x 400 tall.
ST7305_4p2_BW_DisplayDriver::ST7305_4p2_BW_DisplayDriver(int dcPin, int resPin, int csPin, int sclkPin, int sdinPin, SPIClass &spi)
    : ST73XX_UI(LCD_W, LCD_H),
      DC_PIN(dcPin), RES_PIN(resPin), CS_PIN(csPin), SCLK_PIN(sclkPin), SDIN_PIN(sdinPin),
      LCD_WIDTH(LCD_W), LCD_HEIGHT(LCD_H), LCD_DATA_WIDTH(0), LCD_DATA_HEIGHT(0),
      DISPLAY_BUFFER_LENGTH(LCD_W * LCD_H), display_buffer(g_fb), spiRef(spi)
{
}

ST7305_4p2_BW_DisplayDriver::ST7305_4p2_BW_DisplayDriver(const ST73xxPins &pins, SPIClass &spi)
    : ST73XX_UI(LCD_W, LCD_H),
      DC_PIN(pins.dc), RES_PIN(pins.rst), CS_PIN(pins.cs), SCLK_PIN(pins.sclk), SDIN_PIN(pins.sdin),
      LCD_WIDTH(LCD_W), LCD_HEIGHT(LCD_H), LCD_DATA_WIDTH(0), LCD_DATA_HEIGHT(0),
      DISPLAY_BUFFER_LENGTH(LCD_W * LCD_H), display_buffer(g_fb), spiRef(spi)
{
}

ST7305_4p2_BW_DisplayDriver::ST7305_4p2_BW_DisplayDriver(SPIClass &spi)
    : ST73XX_UI(LCD_W, LCD_H),
      DC_PIN(0), RES_PIN(0), CS_PIN(0), SCLK_PIN(0), SDIN_PIN(0),
      LCD_WIDTH(LCD_W), LCD_HEIGHT(LCD_H), LCD_DATA_WIDTH(0), LCD_DATA_HEIGHT(0),
      DISPLAY_BUFFER_LENGTH(LCD_W * LCD_H), display_buffer(g_fb), spiRef(spi)
{
}

ST7305_4p2_BW_DisplayDriver::~ST7305_4p2_BW_DisplayDriver() {}

void ST7305_4p2_BW_DisplayDriver::initialize() { clearDisplay(); }

void ST7305_4p2_BW_DisplayDriver::fill(uint8_t data)
{
    memset(g_fb, data ? 1 : 0, sizeof(g_fb));
}

void ST7305_4p2_BW_DisplayDriver::clearDisplay()
{
    memset(g_fb, 0, sizeof(g_fb)); // 0 = white
}

// Logical landscape: x in [0,400) wide, y in [0,300) tall. Stored directly.
void ST7305_4p2_BW_DisplayDriver::writePhysicalPoint(uint x, uint y, bool enabled)
{
    if (x >= (uint)LCD_W || y >= (uint)LCD_H) return; // x<400, y<300
    g_fb[y * LCD_W + x] = enabled ? 1 : 0;
}

void ST7305_4p2_BW_DisplayDriver::writePoint(uint x, uint y, bool enabled)
{
    writePhysicalPoint(x, y, enabled);
}

void ST7305_4p2_BW_DisplayDriver::writePoint(uint x, uint y, uint16_t data)
{
    writePhysicalPoint(x, y, data != 0);
}

void ST7305_4p2_BW_DisplayDriver::display()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    memcpy(g_shared, g_fb, sizeof(g_fb));
    g_dirty = true;
}

void ST7305_4p2_BW_DisplayDriver::Initial_ST7305() {}
void ST7305_4p2_BW_DisplayDriver::Low_Power_Mode() {}
void ST7305_4p2_BW_DisplayDriver::High_Power_Mode() {}
void ST7305_4p2_BW_DisplayDriver::display_on(bool) {}
void ST7305_4p2_BW_DisplayDriver::display_Inversion(bool) {}

// private helpers (unused on host)
void ST7305_4p2_BW_DisplayDriver::address() {}
void ST7305_4p2_BW_DisplayDriver::Write_Register(uint8_t) {}
void ST7305_4p2_BW_DisplayDriver::Write_Parameter(uint8_t) {}
