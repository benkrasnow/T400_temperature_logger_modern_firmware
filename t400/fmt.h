#ifndef FMT_H
#define FMT_H

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "t400.h"
#include "rtc.h"

// Small replacement for sprintf("%d") and friends. Linking sprintf pulls in vfprintf,
// which costs about 1 kB of flash; the T400 has very little to spare.

// Write 'v' into 'out', right-justified in at least 'width' columns, padded on the left
// with 'pad' (' ' or '0'). The result is NUL terminated.
// @return number of characters written, not counting the NUL
__attribute__((noinline, noclone)) inline uint8_t fmtInt(char* out, int32_t v, uint8_t width = 0, char pad = ' ') {
  char digits[12];
  ltoa(v, digits, 10);
  uint8_t len = strlen(digits);
  uint8_t n = 0;
  for(; (uint8_t)(n + len) < width; n++) out[n] = pad;
  memcpy(out + n, digits, len + 1);
  return n + len;
}

// Same as fmtInt(), for unsigned 32 bit values
inline uint8_t fmtUInt(char* out, uint32_t v) {
  ultoa(v, out, 10);
  return strlen(out);
}


// Format a temperature given in tenths of a degree as "[-]W.F", with the whole part right-justified
// in at least 'width' columns. Handles values between -0.9 and 0 correctly (sign is kept even
// though the whole part is 0, which "t/10" and "t%10" printing loses).
// @return number of characters written, not counting the NUL
__attribute__((noinline, noclone)) inline uint8_t fmtTenths(char* out, int16_t tenths, uint8_t width = 0) {
  uint16_t mag = (tenths < 0) ? (uint16_t)(-(int32_t)tenths) : (uint16_t)tenths;
  char whole[8];
  uint8_t n = 0;
  if(tenths < 0) whole[n++] = '-';
  utoa(mag / 10, whole + n, 10);
  uint8_t len = strlen(whole);
  uint8_t o = 0;
  for(; (uint8_t)(o + len) < width; o++) out[o] = ' ';
  memcpy(out + o, whole, len);
  o += len;
  out[o++] = '.';
  out[o++] = '0' + (mag % 10);
  out[o] = 0;
  return o;
}

// Format a date/time as "YYYY-MM-DD HH:MM:SS.f" (21 characters + NUL). 'tenth' is the tenths of a
// second to append (0 for the sample taken on the RTC tick, 5 for the half-second sample).
inline void fmtTimestamp(char* out, const Rtc::Time& t, uint8_t tenth) {
  fmtInt(out,      t.year,   4, '0'); out[4]  = '-';
  fmtInt(out + 5,  t.month,  2, '0'); out[7]  = '-';
  fmtInt(out + 8,  t.day,    2, '0'); out[10] = ' ';
  fmtInt(out + 11, t.hour,   2, '0'); out[13] = ':';
  fmtInt(out + 14, t.minute, 2, '0'); out[16] = ':';
  fmtInt(out + 17, t.second, 2, '0'); out[19] = '.';
  out[20] = '0' + tenth;
  out[21] = 0;
}

// 'C', 'F' or 'K' for a TEMPERATURE_UNITS_* value
inline char unitLetter(uint8_t unit) {
  static const char letters[] PROGMEM = "CFK";
  return pgm_read_byte(&letters[unit]);
}

// Convert a temperature in tenths of a degree C to tenths of the selected unit.
// Uses 32 bit math: C*18 overflows 16 bit ints above 182 C.
inline int16_t convertTenths(int16_t celsiusTenths, uint8_t unit) {
  switch(unit) {
  case TEMPERATURE_UNITS_F: return (int16_t)(((int32_t)celsiusTenths * 18) / 10 + 320);
  case TEMPERATURE_UNITS_K: return celsiusTenths + 2732;
  default: break;
  }
  return celsiusTenths;
}

#endif
