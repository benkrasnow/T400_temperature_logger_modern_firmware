// On-device self test for the temperature formatting and unit conversion helpers in t400/fmt.h.
// Prints PASS/FAIL per case over USB serial, then a summary, once per 3 s.
#include "fmt.h"   // from ../../t400 (the sketch is built with -I pointing there, see tools/run_format_test.ps1 or the command in the README)

uint8_t failures;

void checkStr(const char* name, const char* got, const char* want) {
  bool ok = strcmp(got, want) == 0;
  if(!ok) failures++;
  Serial.print(ok ? F("PASS ") : F("FAIL ")); Serial.print(name);
  Serial.print(F(": got '")); Serial.print(got); Serial.print(F("' want '")); Serial.print(want); Serial.println('\'');
}
void checkInt(const char* name, long got, long want) {
  bool ok = got == want;
  if(!ok) failures++;
  Serial.print(ok ? F("PASS ") : F("FAIL ")); Serial.print(name);
  Serial.print(F(": got ")); Serial.print(got); Serial.print(F(" want ")); Serial.println(want);
}
void t(int16_t tenths, uint8_t width, const char* want) {
  char b[12]; fmtTenths(b, tenths, width);
  char name[24]; snprintf(name, sizeof(name), "fmtTenths(%d,%d)", tenths, width);
  checkStr(name, b, want);
}

void setup() { Serial.begin(9600); }

void loop() {
  failures = 0;
  Serial.println(F("---- format_test ----"));
  t(0, 0, "0.0");      t(5, 0, "0.5");       t(-5, 0, "-0.5");    t(-9, 0, "-0.9");
  t(-10, 0, "-1.0");   t(291, 0, "29.1");    t(-2700, 0, "-270.0"); t(13720, 0, "1372.0");
  t(32760, 0, "3276.0"); t(-32760, 0, "-3276.0");
  t(291, 4, "  29.1"); t(-5, 4, "  -0.5");   t(1000, 4, " 100.0"); t(-123, 4, " -12.3");
  checkInt("C->F 100.0C", convertTenths(1000, TEMPERATURE_UNITS_F), 2120);
  checkInt("C->F 0.0C", convertTenths(0, TEMPERATURE_UNITS_F), 320);
  checkInt("C->F 1372.0C (was 16-bit overflow)", convertTenths(13720, TEMPERATURE_UNITS_F), 25016);
  checkInt("C->F 200.0C (was 16-bit overflow)", convertTenths(2000, TEMPERATURE_UNITS_F), 3920);
  checkInt("C->F -40.0C", convertTenths(-400, TEMPERATURE_UNITS_F), -400);
  checkInt("C->K 25.0C", convertTenths(250, TEMPERATURE_UNITS_K), 2982);
  checkInt("C->C passthrough", convertTenths(-123, TEMPERATURE_UNITS_C), -123);
  // Rtc::toSeconds against reference values computed independently (Python datetime)
  struct { Rtc::Time t; uint32_t want; } secs[] = {
    {{2000, 1, 1, 0, 0, 0}, 0UL},
    {{2000, 3, 1, 0, 0, 0}, 5184000UL},          // 2000 is a leap year
    {{2001, 1, 1, 0, 0, 0}, 31622400UL},
    {{2024, 2, 29, 23, 59, 59}, 762566399UL},    // leap day
    {{2026, 10, 4, 12, 0, 0}, 844430400UL},
    {{2099, 12, 31, 23, 59, 59}, 3155759999UL},
  };
  for(uint8_t i = 0; i < sizeof(secs) / sizeof(secs[0]); i++) {
    char name[16]; snprintf(name, sizeof(name), "toSeconds #%d", i);
    checkInt(name, (long)Rtc::toSeconds(secs[i].t), (long)secs[i].want);
  }
  // Wrap-around: differences must still be right when seconds*10 overflows 32 bits
  uint32_t a = Rtc::toSeconds(secs[4].t) * 10 + 0, b = Rtc::toSeconds(secs[4].t) * 10 + 5;
  checkInt("tenths difference across 2^32 wrap", (long)(b - a), 5);
  Serial.print(F("SUMMARY: ")); Serial.print(failures); Serial.println(failures ? F(" FAILED") : F(" failures, all passed"));
  Serial.println();
  delay(3000);
}
