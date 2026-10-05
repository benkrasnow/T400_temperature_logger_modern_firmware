// On-device self test for the calibration logic (t400/calibration.cpp), using synthetic samples.
// Prints PASS/FAIL over USB serial. The EEPROM calibration data is saved first and restored at the end.
//
// Build with the t400 folder on the include path so the module can be pulled in directly, e.g.
//   arduino-cli compile -b PaxInstruments:avr:t400 --build-property "compiler.cpp.extra_flags=-I<path to t400>" diagnostics/cal_test
#include <avr/eeprom.h>
#include "calibration.cpp"

uint8_t failures;
uint8_t savedEeprom[16];

void check(const char* name, long got, long want) {
  bool ok = (got == want);
  if(!ok) failures++;
  Serial.print(ok ? F("PASS ") : F("FAIL ")); Serial.print(name);
  Serial.print(F(": got ")); Serial.print(got); Serial.print(F(" want ")); Serial.println(want);
}

// Feed a full run: per channel a list of kinds. kind 0: stable near 'level' uV, 1: unstable, 2: all invalid, 3: some invalid
void run(const int32_t level[4], const uint8_t kind[4]) {
  Cal::start();
  for(uint8_t i = 0; i < Cal::SAMPLES_PER_CHANNEL; i++) {
    for(uint8_t ch = 0; ch < 4; ch++) {
      bool valid = true;
      int32_t v = level[ch] + ((i & 1) ? 3 : -3);        // +-3 uV of noise
      if(kind[ch] == 1) v = level[ch] + (i == 5 ? 150 : 0);   // one outlier: spread over the limit
      if(kind[ch] == 2) valid = false;
      if(kind[ch] == 3 && i == 7) valid = false;
      Cal::sample(ch, valid, v);
    }
  }
}

void setup() {
  Serial.begin(9600);
  eeprom_read_block(savedEeprom, (void*)0, sizeof(savedEeprom));
}

void loop() {
  failures = 0;
  Serial.println(F("---- cal_test ----"));
  Cal::clear();
  check("starts at zero", Cal::offsetUv(0) + Cal::offsetUv(1), 0);

  // Run 1: ch0 stable at +400 uV is too far from 0 C; ch1 stable at -57; ch2 unstable; ch3 no probe
  {
    const int32_t level[4] = {400, -57, 10, 0};
    const uint8_t kind[4]  = {0, 0, 1, 2};
    run(level, kind);
    check("run done", Cal::state() == Cal::DONE, 1);
    check("ch0 far from zero -> NOT_AT_ZERO", Cal::result(0), Cal::NOT_AT_ZERO);
    check("ch1 stable -> CALIBRATED", Cal::result(1), Cal::CALIBRATED);
    check("ch1 offset = +57 uV", Cal::offsetUv(1), 57);
    check("ch2 outlier -> UNSTABLE", Cal::result(2), Cal::UNSTABLE);
    check("ch3 no probe -> NOT_ATTACHED", Cal::result(3), Cal::NOT_ATTACHED);
    check("rejected/absent channels unchanged (ch0)", Cal::offsetUv(0), 0);
    check("rejected/absent channels unchanged (ch2)", Cal::offsetUv(2), 0);
    check("rejected/absent channels unchanged (ch3)", Cal::offsetUv(3), 0);
  }

  // Persistence: a fresh load() must return what was stored
  Cal::load();
  check("persisted ch1 offset", Cal::offsetUv(1), 57);

  // Run 2: only ch3 attached now (ch1 absent): ch1 keeps its stored offset, ch3 gets one
  {
    const int32_t level[4] = {0, 0, 0, 120};
    const uint8_t kind[4]  = {2, 2, 2, 0};
    run(level, kind);
    check("run2 ch3 CALIBRATED", Cal::result(3), Cal::CALIBRATED);
    check("run2 ch3 offset = -120", Cal::offsetUv(3), -120);
    check("run2 ch1 (absent) keeps old offset", Cal::offsetUv(1), 57);
    Cal::load();
    check("run2 persisted ch1", Cal::offsetUv(1), 57);
    check("run2 persisted ch3", Cal::offsetUv(3), -120);
  }

  // Run 3: a channel with one invalid reading is not trusted (UNSTABLE), and negative means round correctly
  {
    const int32_t level[4] = {-250, 5, 0, 0};
    const uint8_t kind[4]  = {0, 3, 2, 2};
    run(level, kind);
    check("-250 uV is within the limit -> CALIBRATED", Cal::result(0), Cal::CALIBRATED);
    check("ch0 offset = +250", Cal::offsetUv(0), 250);
    check("one bad reading -> UNSTABLE", Cal::result(1), Cal::UNSTABLE);
    check("ch1 still 57 after UNSTABLE", Cal::offsetUv(1), 57);
  }

  // Damaged EEPROM data must be ignored
  {
    uint8_t b = eeprom_read_byte((uint8_t*)3);
    eeprom_update_byte((uint8_t*)3, b ^ 0x55);     // corrupt an offset byte (checksum no longer matches)
    Cal::load();
    check("corrupt EEPROM -> offsets zero", Cal::offsetUv(0) + Cal::offsetUv(1) + Cal::offsetUv(3), 0);
  }

  // Restore the EEPROM contents from before the test
  eeprom_update_block(savedEeprom, (void*)0, sizeof(savedEeprom));
  Cal::load();

  Serial.print(F("SUMMARY: ")); Serial.print(failures);
  Serial.println(failures ? F(" FAILED") : F(" failures, all passed"));
  Serial.println();
  delay(3000);
}
