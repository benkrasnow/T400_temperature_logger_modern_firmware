#include <Wire.h>
#include "rtc.h"

#define DS3231_ADDR     0x68
#define REG_TIME        0x00
#define REG_CONTROL     0x0E
#define REG_STATUS      0x0F
#define STATUS_OSF      0x80   // oscillator stopped: the time is not trustworthy

namespace Rtc {

static bool timeIsSet = false;

static uint8_t toBcd(uint8_t v)   { return ((v / 10) << 4) | (v % 10); }
static uint8_t fromBcd(uint8_t v) { return ((v >> 4) * 10) + (v & 0x0F); }

static bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static int16_t readReg(uint8_t reg) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(reg);
  if(Wire.endTransmission() != 0) return -1;
  if(Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

static bool plausible(const Time& t) {
  return t.year >= 2000 && t.year <= 2099 && t.month >= 1 && t.month <= 12 && t.day >= 1 &&
         t.day <= 31 && t.hour <= 23 && t.minute <= 59 && t.second <= 59;
}

void begin() {
  writeReg(REG_CONTROL, 0);   // 1 Hz square wave on SQW/INT, alarms off
  int16_t status = readReg(REG_STATUS);
  timeIsSet = (status >= 0) && !(status & STATUS_OSF);
}

bool isSet() { return timeIsSet; }

bool read(Time& t) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(REG_TIME);
  if(Wire.endTransmission() != 0) return false;
  if(Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)7) != 7) return false;
  t.second = fromBcd(Wire.read() & 0x7F);
  t.minute = fromBcd(Wire.read() & 0x7F);
  t.hour   = fromBcd(Wire.read() & 0x3F);   // 24 hour mode
  Wire.read();                              // day of week
  t.day    = fromBcd(Wire.read() & 0x3F);
  t.month  = fromBcd(Wire.read() & 0x1F);   // bit 7 is the century flag
  t.year   = 2000 + fromBcd(Wire.read());
  return plausible(t);
}

bool set(const Time& t) {
  if(!plausible(t)) return false;
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(REG_TIME);
  Wire.write(toBcd(t.second));
  Wire.write(toBcd(t.minute));
  Wire.write(toBcd(t.hour));
  Wire.write((uint8_t)1);                   // day of week: not used by the T400, any value 1-7 is valid
  Wire.write(toBcd(t.day));
  Wire.write(toBcd(t.month));
  Wire.write(toBcd(t.year - 2000));
  if(Wire.endTransmission() != 0) return false;

  // Clear the oscillator-stopped flag now that the time is valid
  int16_t status = readReg(REG_STATUS);
  if(status >= 0) writeReg(REG_STATUS, status & ~STATUS_OSF);
  timeIsSet = true;
  return true;
}

}
