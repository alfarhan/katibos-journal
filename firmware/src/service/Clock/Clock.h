#pragma once

#include <Arduino.h>

// Software clock for a device with no RTC. A real time (UTC epoch seconds) is
// learned from NTP-on-connect / the sync Date: header / manual entry, cached to
// config, and advanced via millis() while powered on. The streak/Today rollover
// only fires from a time CONFIRMED this session, so a stale cached clock can
// never falsely break a streak — see clock_tick().

// Local UTC offset in SECONDS, from config["timezone"] (stored as MINUTES from
// UTC; default +180 = KSA UTC+3, no DST). Day boundaries are local. Settable on
// the Time zone screen.
long clock_tz_offset();

// Wall time "HH:MM" for a given UTC offset in MINUTES ("--:--" if unconfirmed);
// used by the Time zone picker to preview the offset live.
String clock_timestr_tz(int tzMinutes);

// Feed a freshly learned absolute time (UTC epoch seconds). Marks the clock
// confirmed for this session, persists it, and runs a rollover.
void clock_set_epoch(long epoch);

// True once a real time has been confirmed this session (NTP / Date header /
// manual). Used to gate fallback date sources behind the primary one.
bool clock_confirmed();

// Local day number (days since 1970-01-01, local time), or 0 if no time has
// been confirmed this session.
int clock_localday();

// Run the daily rollover if the local day has advanced. Cheap; safe to call
// often (on save, on viewing STATS). No-op until a time is confirmed.
void clock_tick();

// "YYYY-MM-DD" for the current local day, or the last cached day if the clock
// is unconfirmed this session, or "----------" if never set.
String clock_datestr();

// Convert a civil date to / from a day number (days since 1970-01-01).
long clock_days_from_civil(int y, unsigned m, unsigned d);
void clock_civil_from_days(long z, int &y, unsigned &m, unsigned &d);
