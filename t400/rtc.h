#ifndef RTC_H
#define RTC_H

#include <Arduino.h>

// Minimal DS3231 driver: just enough to read and set the date/time over I2C.
// The T400 stores local time as given by the PC (the RTC has no time zone or DST handling).
namespace Rtc {

struct Time {
  uint16_t year;    // 2000-2099
  uint8_t month;    // 1-12
  uint8_t day;      // 1-31
  uint8_t hour;     // 0-23
  uint8_t minute;   // 0-59
  uint8_t second;   // 0-59
};

// Configure the 1 Hz square wave output and check whether the clock has ever been set.
// Call once from setup() after Wire.begin().
void begin();

// True if the time has been set since the oscillator last stopped (DS3231 OSF flag clear)
bool isSet();

// Read the current time. Returns false on an I2C error or an implausible value.
bool read(Time& t);

// Seconds since 2000-01-01 00:00:00 (same calendar as the clock; valid for 2000-2099). Header-only so
// it can be unit tested without the I2C code.
inline uint32_t toSeconds(const Time& t) {
  static const uint16_t daysBeforeMonth[12] PROGMEM = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  uint16_t y = t.year - 2000;
  uint32_t days = (uint32_t)y * 365 + (y + 3) / 4;            // + one day for each leap year before this one
  days += pgm_read_word(&daysBeforeMonth[t.month - 1]) + (t.day - 1);
  if(t.month > 2 && (y % 4) == 0) days++;                     // this year's leap day (2000-2099: every 4th)
  return ((days * 24 + t.hour) * 60 + t.minute) * 60 + t.second;
}

// Set the time and mark the clock as valid. Returns false if a field is out of range or on I2C error.
bool set(const Time& t);

}

#endif
