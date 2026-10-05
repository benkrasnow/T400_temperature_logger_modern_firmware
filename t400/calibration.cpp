#include <avr/eeprom.h>
#include "calibration.h"
#include "t400.h"

// Acceptance limits, in microvolts of type K output (about 40 uV per degree C)
#define MAX_SPREAD_UV       80    // max - min over the run: about 2 C
#define MAX_ZERO_ERROR_UV   250   // about 6 C: anything further out is not an ice bath

#define RESULT_SHOWN_SECONDS 6

namespace Cal {

namespace {

const uint16_t MAGIC = 0xCA11;

struct Stored {
  uint16_t magic;
  int16_t  offset[SENSOR_COUNT];
  uint8_t  sum;      // simple checksum of the bytes above
};

struct Accumulator {
  int32_t sum;
  int16_t minUv, maxUv;
  uint8_t count;     // readings seen
  uint8_t bad;       // of which out of range
};

int16_t offsets[SENSOR_COUNT];
ChannelResult results[SENSOR_COUNT];
Accumulator acc[SENSOR_COUNT];
volatile State currentState = IDLE;
volatile uint8_t resultTicks = 0;

uint8_t checksum(const Stored& s) {
  const uint8_t* p = (const uint8_t*)&s;
  uint8_t sum = 0x5A;
  for(uint8_t i = 0; i < sizeof(s) - 1; i++) sum += p[i];
  return sum;
}

void save() {
  Stored s;
  s.magic = MAGIC;
  for(uint8_t i = 0; i < SENSOR_COUNT; i++) s.offset[i] = offsets[i];
  s.sum = checksum(s);
  eeprom_update_block(&s, (void*)0, sizeof(s));   // only writes bytes that changed
}

void finish() {
  bool changed = false;
  for(uint8_t ch = 0; ch < SENSOR_COUNT; ch++) {
    const Accumulator& a = acc[ch];
    if(a.bad == a.count) {
      results[ch] = NOT_ATTACHED;
    } else if(a.bad > 0 || (a.maxUv - a.minUv) > MAX_SPREAD_UV) {
      results[ch] = UNSTABLE;
    } else {
      static_assert(SAMPLES_PER_CHANNEL == 16, "the average below is a shift by 4");
      int16_t mean = (int16_t)((a.sum + 8) >> 4);                       // sum / 16, rounded
      if(mean > MAX_ZERO_ERROR_UV || mean < -MAX_ZERO_ERROR_UV) {
        results[ch] = NOT_AT_ZERO;
      } else {
        offsets[ch] = (int16_t)(-mean);   // brings the reading in the bath to 0 C
        results[ch] = CALIBRATED;
        changed = true;
      }
    }
  }
  if(changed) save();
  resultTicks = RESULT_SHOWN_SECONDS;
  currentState = DONE;
}

}  // namespace

void load() {
  Stored s;
  eeprom_read_block(&s, (void*)0, sizeof(s));
  bool ok = (s.magic == MAGIC) && (s.sum == checksum(s));
  for(uint8_t i = 0; i < SENSOR_COUNT; i++) offsets[i] = ok ? s.offset[i] : 0;
}

int16_t offsetUv(uint8_t channel) { return offsets[channel]; }

void clear() {
  for(uint8_t i = 0; i < SENSOR_COUNT; i++) offsets[i] = 0;
  save();
}

bool start() {
  if(currentState == RUNNING) return false;
  for(uint8_t ch = 0; ch < SENSOR_COUNT; ch++) {
    acc[ch].sum = 0;
    acc[ch].minUv = 32767;
    acc[ch].maxUv = -32768;
    acc[ch].count = 0;
    acc[ch].bad = 0;
  }
  currentState = RUNNING;
  return true;
}

bool sample(uint8_t channel, bool valid, int32_t compensatedUv) {
  if(currentState != RUNNING || acc[channel].count >= SAMPLES_PER_CHANNEL) return false;
  Accumulator& a = acc[channel];
  a.count++;
  if(valid) {
    int16_t v = (int16_t)constrain(compensatedUv, -30000L, 30000L);
    a.sum += v;
    if(v < a.minUv) a.minUv = v;
    if(v > a.maxUv) a.maxUv = v;
  } else {
    a.bad++;
  }
  for(uint8_t ch = 0; ch < SENSOR_COUNT; ch++) {
    if(acc[ch].count < SAMPLES_PER_CHANNEL) return false;
  }
  finish();
  return true;
}

State state() { return currentState; }
bool running() { return currentState == RUNNING; }

void setHint(bool on) {
  if(on && currentState == IDLE) currentState = HINT;
  else if(!on && currentState == HINT) currentState = IDLE;
}

void secondTick() {
  if(currentState == DONE && resultTicks > 0 && --resultTicks == 0) currentState = IDLE;
}

ChannelResult result(uint8_t channel) { return results[channel]; }

// Two characters per result, in enum order: NOT_ATTACHED, CALIBRATED, UNSTABLE, NOT_AT_ZERO
char resultCode(uint8_t channel, uint8_t index) {
  static const char codes[] PROGMEM = "--ok????";
  return pgm_read_byte(&codes[results[channel] * 2 + index]);
}

}
