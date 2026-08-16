#include <ST73XX_UI.h>
#include <ST7305_4p2_BW_DisplayDriver.h>
#include <stdlib.h>

// The supplier uses 0x12. The previous panel profile used 0x05, which selects
// a slower HPM refresh and produces stronger optical contrast on this batch.
static constexpr uint8_t ST7305_FRAME_RATE = 0x05;

#if RLCD_TYPE == 1
static constexpr int ST7305_LCD_WIDTH = 300;
static constexpr int ST7305_LCD_HEIGHT = 400;
static constexpr int ST7305_DATA_WIDTH = 150;
static constexpr int ST7305_DATA_HEIGHT = 200;
static constexpr int ST7305_BUFFER_LENGTH = 30000;
#else
static constexpr int ST7305_LCD_WIDTH = 400;
static constexpr int ST7305_LCD_HEIGHT = 300;
static constexpr int ST7305_DATA_WIDTH = 200;
static constexpr int ST7305_DATA_HEIGHT = 75;
static constexpr int ST7305_BUFFER_LENGTH = 15000;
#endif

ST7305_4p2_BW_DisplayDriver::ST7305_4p2_BW_DisplayDriver(
    int dcPin,
    int resPin,
    int csPin,
    int sclkPin,
    int sdinPin,
    SPIClass &spi)
    : DC_PIN(dcPin),
      RES_PIN(resPin),
      CS_PIN(csPin),
      SCLK_PIN(sclkPin),
      SDIN_PIN(sdinPin),
      LCD_WIDTH(ST7305_LCD_WIDTH),
      LCD_HEIGHT(ST7305_LCD_HEIGHT),
      ST73XX_UI(ST7305_LCD_WIDTH, ST7305_LCD_HEIGHT),
      LCD_DATA_WIDTH(ST7305_DATA_WIDTH),
      LCD_DATA_HEIGHT(ST7305_DATA_HEIGHT),
      DISPLAY_BUFFER_LENGTH(ST7305_BUFFER_LENGTH),
      spiRef(spi)
{
    display_buffer = new uint8_t[DISPLAY_BUFFER_LENGTH];
    dirtyAll();
}

// The buffer is row-major in real_y (index = real_y * LCD_DATA_WIDTH + real_x), and
// for type 1 the 0x2B row window is 0..199, exactly the real_y range - so a row span
// is both contiguous in RAM and directly addressable. That is the whole reason the
// dirty span tracks rows and not columns: a column slice would need a strided gather
// and a 0x2A window, for no gain on the case that actually matters.
void ST7305_4p2_BW_DisplayDriver::dirtyAll()
{
    dirty_lo = 0;
    dirty_hi = (DISPLAY_BUFFER_LENGTH / LCD_DATA_WIDTH) - 1;
}

ST7305_4p2_BW_DisplayDriver::~ST7305_4p2_BW_DisplayDriver()
{
    delete[] display_buffer;
    delete[] invert_buffer;
}

void ST7305_4p2_BW_DisplayDriver::initialize()
{
    pinMode(DC_PIN, OUTPUT);
    pinMode(RES_PIN, OUTPUT);
    pinMode(CS_PIN, OUTPUT);

    // Keep the panel deselected while SPI and reset are configured.
    digitalWrite(CS_PIN, HIGH);
    digitalWrite(DC_PIN, HIGH);
    digitalWrite(RES_PIN, HIGH);

    spiRef.begin(SCLK_PIN, -1, SDIN_PIN, -1);
    spiRef.setFrequency(40000000);
    spiRef.setDataMode(SPI_MODE0);
    spiRef.setBitOrder(MSBFIRST);

    Initial_ST7305();
    High_Power_Mode();
    display_on(true);
    display_Inversion(false);
    clearDisplay();
}

void ST7305_4p2_BW_DisplayDriver::fill(uint8_t data)
{
    memset(display_buffer, data, DISPLAY_BUFFER_LENGTH);
    dirtyAll();
    Serial.printf("fill data = 0x%x\n", data);
}

void ST7305_4p2_BW_DisplayDriver::clearDisplay()
{
    memset(display_buffer, 0x00, DISPLAY_BUFFER_LENGTH);
    dirtyAll();
}

void ST7305_4p2_BW_DisplayDriver::writePhysicalPoint(uint x, uint y, bool black)
{
#if RLCD_TYPE == 1
    if (x >= LCD_HEIGHT || y >= LCD_WIDTH)
        return;

    uint px = LCD_WIDTH - 1 - y;
    uint py = x;
    uint real_x = px / 2;
    uint real_y = py / 2;
    uint write_byte_index = real_y * LCD_DATA_WIDTH + real_x;

    if (write_byte_index >= DISPLAY_BUFFER_LENGTH)
        return;

    // One byte holds FOUR pixels at 2 bits each, not two at a nibble each:
    // 30000 bytes * 4 = 120000 = exactly this panel's 300x400 dots. Selecting
    // only on px&1 and writing a whole 0x0F nibble threw the low bit of py away,
    // so py and py+1 collided in one nibble and the second write won - and since
    // py is the UI's x, that halved horizontal resolution and doubled every
    // vertical stem. Arabic naskh carries its letterforms in that axis, which is
    // why alef-lam came out the wrong width. If rows interleave wrong, flip the
    // py parity term - which 2-bit field is the even row is the one guess here.
    uint8_t shift = ((px & 1) == 0 ? 4 : 0) + ((py & 1) == 0 ? 2 : 0);
    uint8_t mask = 0x3 << shift;
    uint8_t prev = display_buffer[write_byte_index];
    uint8_t next = black ? (uint8_t)(prev | mask) : (uint8_t)(prev & ~mask);
    if (next == prev)
        return;

    display_buffer[write_byte_index] = next;
    if ((int)real_y < dirty_lo)
        dirty_lo = (int)real_y;
    if ((int)real_y > dirty_hi)
        dirty_hi = (int)real_y;
#else
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT)
        return;

    // Supplier format: one byte contains a 2x4 block of monochrome pixels.
    // Bytes are column-major and the panel's physical Y axis is inverted.
    uint inv_y = LCD_HEIGHT - 1 - y;
    uint byte_x = x / 2;
    uint block_y = inv_y / 4;
    uint write_byte_index = byte_x * LCD_DATA_HEIGHT + block_y;

    if (write_byte_index >= DISPLAY_BUFFER_LENGTH)
        return;

    uint8_t local_x = x % 2;
    uint8_t local_y = inv_y % 4;
    uint8_t write_bit = 7 - ((local_y << 1) | local_x);

    if (black)
        display_buffer[write_byte_index] |= (1U << write_bit);
    else
        display_buffer[write_byte_index] &= ~(1U << write_bit);
#endif
}

void ST7305_4p2_BW_DisplayDriver::writePoint(uint x, uint y, bool data)
{
    writePhysicalPoint(x, y, data);
}

void ST7305_4p2_BW_DisplayDriver::writePoint(uint x, uint y, uint16_t data)
{
    writePhysicalPoint(x, y, data != 0);
}

void ST7305_4p2_BW_DisplayDriver::address(int row_lo, int row_hi)
{
    Write_Register(0x2A);
#if RLCD_TYPE == 1
    Write_Parameter(0x05);
    Write_Parameter(0x36);
#else
    Write_Parameter(0x12);
    Write_Parameter(0x2A);
#endif

    Write_Register(0x2B);
#if RLCD_TYPE == 1
    Write_Parameter((uint8_t)(row_lo & 0xFF));
    Write_Parameter((uint8_t)(row_hi & 0xFF));
#else
    // Type 2's buffer is column-major, so its rows are not this window's rows and
    // a partial span would land in the wrong place. Always the full frame here.
    (void)row_lo;
    (void)row_hi;
    Write_Parameter(0x00);
    Write_Parameter(0xC7);
#endif

    Write_Register(0x2C);
}

void ST7305_4p2_BW_DisplayDriver::display()
{
    const int stride = LCD_DATA_WIDTH;
#if RLCD_TYPE == 1
    // Nothing on the glass differs from what the panel already holds. The panel
    // refreshes from its own RAM, so the cheapest correct frame is no frame.
    if (dirty_lo > dirty_hi)
        return;

    const int offset = dirty_lo * stride;
    const int length = (dirty_hi - dirty_lo + 1) * stride;
    address(dirty_lo, dirty_hi);
#else
    const int offset = 0;
    const int length = DISPLAY_BUFFER_LENGTH;
    address(0, (DISPLAY_BUFFER_LENGTH / stride) - 1);
#endif

    digitalWrite(DC_PIN, HIGH);
    digitalWrite(CS_PIN, LOW);

    // Dark theme: send an inverted copy. XOR 0xFF flips every pixel for both
    // packings (type-1 full 0x0/0xF nibbles, type-2 1bpp). Sent from a scratch
    // buffer so the source framebuffer stays intact for the next partial draw.
    uint8_t *out = display_buffer + offset;
    if (m_invert)
    {
        if (!invert_buffer)
            invert_buffer = new uint8_t[DISPLAY_BUFFER_LENGTH];
        if (invert_buffer)
        {
            for (int i = offset; i < offset + length; i++)
                invert_buffer[i] = display_buffer[i] ^ 0xFF;
            out = invert_buffer + offset;
        }
    }

    spiRef.writeBytes(out, length);
    digitalWrite(CS_PIN, HIGH);

    dirty_lo = DISPLAY_BUFFER_LENGTH / stride;
    dirty_hi = -1;
}

void ST7305_4p2_BW_DisplayDriver::setInvert(bool enabled)
{
    if (m_invert == enabled)
        return;
    m_invert = enabled;
    // Every pixel's sent value flips even though the framebuffer is untouched, so
    // the dirty span cannot see this - say so explicitly or the theme change would
    // only reach whatever rows a later edit happened to touch.
    dirtyAll();
}

void ST7305_4p2_BW_DisplayDriver::Initial_ST7305()
{
    // A hardware reset should leave the panel in its power-on state, which would
    // make the previous session's mode irrelevant. That it DIDN'T - a soft restart
    // came back grey while a cold boot never did - says 10ms low with no settle
    // after release was not actually resetting a panel that had been running. Hold
    // it low properly and let it come up before any command goes out.
    digitalWrite(RES_PIN, HIGH);
    delay(10);
    digitalWrite(RES_PIN, LOW);
    delay(50);
    digitalWrite(RES_PIN, HIGH);
    delay(120);

    // Supplier 4.2-inch ST7305 initialization sequence.
    Write_Register(0xD6); // NVM Load Control
#if RLCD_TYPE == 1
    Write_Parameter(0X17);
#else
    Write_Parameter(0X13);
#endif
    Write_Parameter(0X02);

    Write_Register(0xD1); // Booster Enable
    Write_Parameter(0X01);

    Write_Register(0xC0);  // Gate Voltage Setting
#if RLCD_TYPE == 1
    Write_Parameter(0X11);
    Write_Parameter(0X04);
#else
    Write_Parameter(0X12);
    Write_Parameter(0X0A);
#endif

    Write_Register(0xC1);
#if RLCD_TYPE == 1
    Write_Parameter(0X41);
    Write_Parameter(0X41);
    Write_Parameter(0X41);
    Write_Parameter(0X41);
#else
    Write_Parameter(0X73);
    Write_Parameter(0X3E);
    Write_Parameter(0X3C);
    Write_Parameter(0X3C);
#endif

    Write_Register(0xC2);
#if RLCD_TYPE == 1
    Write_Parameter(0X19);
    Write_Parameter(0X19);
    Write_Parameter(0X19);
    Write_Parameter(0X19);
#else
    Write_Parameter(0X00);
    Write_Parameter(0X21);
    Write_Parameter(0X23);
    Write_Parameter(0X23);
#endif

    Write_Register(0xC4);
#if RLCD_TYPE == 1
    Write_Parameter(0X41);
    Write_Parameter(0X41);
    Write_Parameter(0X41);
    Write_Parameter(0X41);
#else
    Write_Parameter(0X32);
    Write_Parameter(0X5C);
    Write_Parameter(0X5A);
    Write_Parameter(0X5A);
#endif

    Write_Register(0xC5);
#if RLCD_TYPE == 1
    Write_Parameter(0X19);
    Write_Parameter(0X19);
    Write_Parameter(0X19);
    Write_Parameter(0X19);
#else
    Write_Parameter(0X32);
    Write_Parameter(0X35);
    Write_Parameter(0X37);
    Write_Parameter(0X37);
#endif

    Write_Register(0xD8);
#if RLCD_TYPE == 1
    Write_Parameter(0XA6);
#else
    Write_Parameter(0X80);
#endif
    Write_Parameter(0XE9);

    Write_Register(0xB2);  // Frame Rate Control
    Write_Parameter(ST7305_FRAME_RATE);

    Write_Register(0xB3); // Update Period Gate EQ Control in HPM
    Write_Parameter(0XE5);
    Write_Parameter(0XF6);
#if RLCD_TYPE == 1
    Write_Parameter(0X05);
    Write_Parameter(0X46);
    Write_Parameter(0X77);
    Write_Parameter(0X77);
    Write_Parameter(0X77);
    Write_Parameter(0X77);
    Write_Parameter(0X76);
    Write_Parameter(0X45);
#else
    Write_Parameter(0X17);
    Write_Parameter(0X77);
    Write_Parameter(0X77);
    Write_Parameter(0X77);
    Write_Parameter(0X77);
    Write_Parameter(0X77);
    Write_Parameter(0X77);
    Write_Parameter(0X71);
#endif

    Write_Register(0xB4);  // Update Period Gate EQ Control in LPM
    Write_Parameter(0X05); // LPM EQ Control
    Write_Parameter(0X46);
    Write_Parameter(0X77);
    Write_Parameter(0X77);
    Write_Parameter(0X77);
    Write_Parameter(0X77);
    Write_Parameter(0X76);
    Write_Parameter(0X45);

    Write_Register(0x62); // Gate Timing Control
    Write_Parameter(0X32);
    Write_Parameter(0X03);
    Write_Parameter(0X1F);

    Write_Register(0xB7); // Source EQ Enable
    Write_Parameter(0X13);

    Write_Register(0xB0);  // Gate Line Setting
    Write_Parameter(0X64); // 60---384 line    64---400 line

    Write_Register(0x11); // Sleep out
#if RLCD_TYPE == 1
    delay(255);
#else
    delay(120);
#endif

    Write_Register(0xC9);  // Source Voltage Select
    Write_Parameter(0X00); // VSHP1; VSLP1 ; VSHN1 ; VSLN1

    Write_Register(0x36); // Memory Data Access Control
    // Write_Parameter(0X00); //Memory Data Access Control: MX=0 ; DO=0
    Write_Parameter(0X48); // MX=1 ; DO=1
    // Write_Parameter(0X4c); //MX=1 ; DO=1 GS=1

    Write_Register(0x3A);  // Data Format Select
    Write_Parameter(0X11); // 10:4write for 24bit ; 11: 3write for 24bit

    Write_Register(0xB9);  // Gamma Mode Setting
    Write_Parameter(0X20); // 20: Mono 00:4GS

    Write_Register(0xB8);  // Panel Setting
    Write_Parameter(0X29); // Panel Setting Frame inversion  09:column 29:dot_1-Frame 25:dot_1-Line

#if RLCD_TYPE == 1
    Write_Register(0x21); // Legacy panel inversion on during initial startup
#endif

    // Panel-specific RAM window.
    Write_Register(0x2A);
    Write_Parameter(0X12);
#if RLCD_TYPE == 1
    Write_Parameter(0X2B);
#else
    Write_Parameter(0X2A);
#endif

    Write_Register(0x2B);
    Write_Parameter(0X00);
    Write_Parameter(0XC7);

    Write_Register(0x35);
    Write_Parameter(0X00);

    Write_Register(0xD0);
    Write_Parameter(0XFF);

#if RLCD_TYPE == 1
    // End in LPM as the supplier sheet does, even though initialize() switches to
    // HPM immediately after and nothing parks this panel in LPM later. The point
    // is the TRANSITION: 0x38 re-ramps the booster only when the panel is not
    // already in HPM. On a soft restart (ESP.restart(), the Restart menu item) the
    // panel never loses power and is still in HPM from the previous session, so
    // coming up in HPM made High_Power_Mode() a no-op - the bias voltages kept
    // whatever the last session left and the whole screen came back grey and
    // washed out until the cable was pulled. Cold boot hid it completely.
    Write_Register(0x39); // LPM - so the HPM in initialize() is a real transition
    Write_Register(0x29); // DISPLAY ON
    // ...and give it time to actually BE in LPM. initialize() calls
    // High_Power_Mode() the instant this returns, so without a settle the two mode
    // writes go out back to back and whether the panel registers a transition at
    // all is a race. 1.14.1 added the LPM write and won that race on a manual
    // restart, then lost it after an OTA reboot.
    delay(50);
#else
    Write_Register(0x38); // HPM
    Write_Register(0x29); // DISPLAY ON
    Write_Register(0x20); // Display inversion off
    Write_Register(0xBB); // Enable and clear controller RAM
    Write_Parameter(0x4F);
#endif
}

void ST7305_4p2_BW_DisplayDriver::Low_Power_Mode()
{
    Write_Register(0x39); // LPM:Low Power Mode ON
}

void ST7305_4p2_BW_DisplayDriver::High_Power_Mode()
{
    Write_Register(0x38); // HPM:high Power Mode ON
#if RLCD_TYPE == 2
    delay(300);

    Write_Register(0xC1);
    Write_Parameter(0X73);
    Write_Parameter(0X3E);
    Write_Parameter(0X3C);
    Write_Parameter(0X3C);

    Write_Register(0xC2);
    Write_Parameter(0X00);
    Write_Parameter(0X21);
    Write_Parameter(0X23);
    Write_Parameter(0X23);

    Write_Register(0xC4);
    Write_Parameter(0X32);
    Write_Parameter(0X5C);
    Write_Parameter(0X5A);
    Write_Parameter(0X5A);

    Write_Register(0xC5);
    Write_Parameter(0X32);
    Write_Parameter(0X35);
    Write_Parameter(0X37);
    Write_Parameter(0X37);

    Write_Register(0xC9);
    Write_Parameter(0X00);
    delay(20);
#endif
}

void ST7305_4p2_BW_DisplayDriver::display_on(bool enabled)
{
    if (enabled)
    {
        Write_Register(0x29); // DISPLAY ON
    }
    else
    {
        Write_Register(0x28); // DISPLAY OFF
    }
}

void ST7305_4p2_BW_DisplayDriver::display_Inversion(bool enabled)
{
    if (enabled)
    {
        Write_Register(0x21); // Display Inversion On
    }
    else
    {
        Write_Register(0x20); // Display Inversion Off
    }
}

void ST7305_4p2_BW_DisplayDriver::Write_Register(uint8_t idat)
{
    digitalWrite(DC_PIN, LOW);
    digitalWrite(CS_PIN, LOW);
    spiRef.write(idat);
    digitalWrite(CS_PIN, HIGH);
}

void ST7305_4p2_BW_DisplayDriver::Write_Parameter(uint8_t ddat)
{
    digitalWrite(DC_PIN, HIGH);
    digitalWrite(CS_PIN, LOW);
    spiRef.write(ddat);
    digitalWrite(CS_PIN, HIGH);
}

ST7305_4p2_BW_DisplayDriver::ST7305_4p2_BW_DisplayDriver(SPIClass &spi)
    : ST7305_4p2_BW_DisplayDriver(4, 0, 3, 2, 1, spi)
{
    // Uses default pins defined in DisplayConfig.h
}

// Named-pin overload to avoid argument order mistakes
ST7305_4p2_BW_DisplayDriver::ST7305_4p2_BW_DisplayDriver(const ST73xxPins &pins, SPIClass &spi)
    : ST7305_4p2_BW_DisplayDriver(pins.dc, pins.rst, pins.cs, pins.sclk, pins.sdin, spi)
{
}
