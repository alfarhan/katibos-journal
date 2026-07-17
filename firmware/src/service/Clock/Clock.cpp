#include "Clock.h"
#include "app/app.h"
#include "app/Config/Config.h"

// confirmed-this-session time anchor (RAM only; a reboot makes the cached clock
// "unconfirmed" again so we don't invent day-crossings on a stale base)
static bool g_confirmed = false;
static long g_epoch_base = 0;          // UTC epoch at the moment it was learned
static unsigned long g_millis_base = 0; // millis() at that same moment

// Howard Hinnant's civil<->days algorithms (proleptic Gregorian, branch-free).
long clock_days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

void clock_civil_from_days(long z, int &y, unsigned &m, unsigned &d)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
}

static long clock_now()
{
    if (!g_confirmed)
        return 0;
    return g_epoch_base + (long)((millis() - g_millis_base) / 1000);
}

long clock_tz_offset()
{
    // stored as minutes from UTC; default +180 (KSA UTC+3)
    int mins = status()["config"]["timezone"] | 180;
    return (long)mins * 60;
}

// Wall time "HH:MM" for an arbitrary UTC offset (minutes), for the Time zone
// picker's live preview. "--:--" until a real time is confirmed this session.
String clock_timestr_tz(int tzMinutes)
{
    long e = clock_now();
    if (e <= 0)
        return "--:--";
    long sod = (e + (long)tzMinutes * 60) % 86400;
    if (sod < 0)
        sod += 86400;
    int h = (int)(sod / 3600), m = (int)((sod % 3600) / 60);
    char b[12];
    bool h24 = status()["config"]["clock_24h"] | true; // default 24-hour
    if (h24)
        snprintf(b, sizeof(b), "%02d:%02d", h, m);
    else
    {
        int h12 = h % 12;
        if (h12 == 0)
            h12 = 12;
        snprintf(b, sizeof(b), "%d:%02d %s", h12, m, h < 12 ? "AM" : "PM");
    }
    return String(b);
}

int clock_localday()
{
    long e = clock_now();
    if (e <= 0)
        return 0;
    long local = e + clock_tz_offset();
    return (int)(local / 86400);
}

static int goalOf()
{
    int g = status()["config"]["daily_goal"].as<int>();
    return g > 0 ? g : 500;
}

// Compare today's local day to the last recorded one; advance streak + reset
// Today across a day boundary. Streak counts consecutive days that MET the goal;
// any idle gap or a missed goal resets it to 0. Never punishes on unknown time.
static void clock_rollover(int today)
{
    if (today <= 0)
        return;

    JsonDocument &app = status();
    int last = app["config"]["last_day"].as<int>();

    if (last <= 0) // first time we know the date: anchor, judge nothing
    {
        app["config"]["last_day"] = today;
        config_save();
        return;
    }
    if (today == last)
        return;
    if (today < last) // clock corrected backwards: adopt, don't punish
    {
        app["config"]["last_day"] = today;
        config_save();
        return;
    }

    // a new day (or several) has begun: settle the day that just ended
    bool met = app["config"]["today_words"].as<int>() >= goalOf();
    int gap = today - last;
    int streak = app["config"]["streak"].as<int>();
    streak = (met && gap == 1) ? streak + 1 : 0;

    app["config"]["streak"] = streak;
    app["config"]["today_words"] = 0;
    app["config"]["last_day"] = today;
    config_save();
}

void clock_set_epoch(long epoch)
{
    if (epoch <= 0)
        return;
    g_epoch_base = epoch;
    g_millis_base = millis();
    g_confirmed = true;
    status()["config"]["clock_epoch"] = epoch;
    clock_tick();
}

bool clock_confirmed()
{
    return g_confirmed;
}

void clock_tick()
{
    int d = clock_localday();
    if (d > 0)
        clock_rollover(d);
}

String clock_datestr()
{
    int day = clock_localday();                 // confirmed this session
    bool live = (day > 0);
    if (!live)
        day = status()["config"]["last_day"].as<int>(); // cached / stale
    if (day <= 0)
        return "----------";

    int y;
    unsigned m, d;
    clock_civil_from_days(day, y, m, d);

    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02u-%02u", y, m, d);
    if (live)
    {
        // append the wall time only while the clock is actively advancing (a
        // stale cached date stays date-only so it never shows a frozen time),
        // formatted per the 12/24-hour Preferences setting.
        return String(buf) + " " + clock_timestr_tz((int)(clock_tz_offset() / 60));
    }
    return String(buf);
}
