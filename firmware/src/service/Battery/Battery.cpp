#include "Battery.h"
#include "app/app.h"

#ifdef BATTERY

// Board wiring comes from platformio.ini. Waveshare ESP32-S3-RLCD-4.2 senses the
// 18650 on GPIO4 (ADC1_CH3) behind a 3x divider, so a full 4.2 V cell reads
// ~1.4 V at the pin - within range only with the 12 dB attenuation set below.
// ADC1 (GPIO1-10) is used deliberately: ADC2 is unusable while WiFi is on.
#ifndef BATTERY_ADC_PIN
#define BATTERY_ADC_PIN 4
#endif
#ifndef BATTERY_DIVIDER
#define BATTERY_DIVIDER 3.0f
#endif

// Li-ion discharge curve. Voltage maps to charge non-linearly, so a plain
// (v - min) / (max - min) reading sits at "80%" for hours and then falls off a
// cliff; interpolating this table tracks the real cell much closer.
static const struct
{
    float v;
    int pct;
} CURVE[] = {
    {4.20f, 100}, {4.10f, 92}, {4.00f, 83}, {3.92f, 75}, {3.85f, 66},
    {3.80f, 58},  {3.75f, 50}, {3.70f, 41}, {3.65f, 33}, {3.60f, 25},
    {3.50f, 16},  {3.40f, 8},  {3.30f, 3},  {3.00f, 0},
};

static float volts_ema = 0.0f;
static int percent = -1;

static int curve_percent(float v)
{
    const int n = (int)(sizeof(CURVE) / sizeof(CURVE[0]));
    if (v >= CURVE[0].v)
        return 100;
    for (int i = 1; i < n; i++)
    {
        if (v >= CURVE[i].v)
        {
            float span = CURVE[i - 1].v - CURVE[i].v;
            float t = span > 0 ? (v - CURVE[i].v) / span : 0.0f;
            return CURVE[i].pct + (int)(t * (CURVE[i - 1].pct - CURVE[i].pct) + 0.5f);
        }
    }
    return 0;
}

// One reading is noisy (the ADC picks up the panel's SPI bursts), so average a
// burst and feed the result through an EMA.
static float battery_sample()
{
    uint32_t mv = 0;
    for (int i = 0; i < 8; i++)
        mv += analogReadMilliVolts(BATTERY_ADC_PIN);
    return (mv / 8.0f) * BATTERY_DIVIDER / 1000.0f;
}

void battery_setup()
{
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db); // ~0-3.1 V range

    volts_ema = battery_sample();
    percent = curve_percent(volts_ema);
    _log("Battery %.2fV (%d%%)\n", volts_ema, percent);
}

void battery_loop()
{
    static unsigned int last = 0;
    if (millis() < last + 10000)
        return;
    last = millis();

    volts_ema = volts_ema * 0.8f + battery_sample() * 0.2f;
    percent = curve_percent(volts_ema);
}

int battery_percent()
{
    return percent;
}

#else

// No battery sensing on this board (rev_8 runs the cell through a charger /
// step-up module with nothing wired to an ADC). Keep the symbols so callers -
// the status bar - stay free of #ifdefs.
void battery_setup() {}
void battery_loop() {}
int battery_percent() { return -1; }

#endif
