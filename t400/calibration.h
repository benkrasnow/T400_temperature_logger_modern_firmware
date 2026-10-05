#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <Arduino.h>

// Single-point (0 C ice bath) calibration.
//
// While calibrating, every thermocouple channel is sampled SAMPLES_PER_CHANNEL times. A channel that gave a
// valid reading every time is "attached". For each attached channel whose readings are stable and plausibly
// near 0 C, an offset in microvolts is computed so that the channel reads 0.0 C in the bath, and it is stored
// in EEPROM. The offset is added to that channel's compensated thermocouple voltage on every later
// reading. Channels that are not attached, or whose readings are rejected, keep their previous offset.
namespace Cal {

const uint8_t SAMPLES_PER_CHANNEL = 16;

enum State : uint8_t {
  IDLE,
  HINT,       // button held long enough to show the "hold to calibrate" hint
  RUNNING,    // collecting samples
  DONE        // result is being shown (for a few seconds)
};

enum ChannelResult : uint8_t {
  NOT_ATTACHED,   // no valid reading: no probe connected
  CALIBRATED,
  UNSTABLE,       // readings varied too much: probe still cooling down, or a bad connection
  NOT_AT_ZERO     // reading is too far from 0 C for this to be an ice bath
};

// Read the stored offsets from EEPROM (all zero if none are stored or the data is damaged)
void load();

// Offset in microvolts to add to a channel's compensated voltage
int16_t offsetUv(uint8_t channel);

// Forget all offsets (also erases them from EEPROM)
void clear();

// Begin a calibration run. Returns false if one is already running.
bool start();

// Feed every reading to this. 'compensatedUv' is the cold junction compensated thermocouple voltage
// WITHOUT any calibration offset, 'valid' is false when the reading was out of range.
// Returns true on the call that completes the run.
bool sample(uint8_t channel, bool valid, int32_t compensatedUv);

State state();
bool running();

// Show or hide the "hold to calibrate" hint (only while idle)
void setHint(bool on);

// Call once per second (from the RTC interrupt): ends the result display after a few seconds
void secondTick();

// Outcome of the last run for a channel, and the offset it computed
ChannelResult result(uint8_t channel);

// First character of the two character code for a channel's result: "ok", "--" (no probe) or "??" (rejected)
char resultCode(uint8_t channel, uint8_t index = 0);

}

#endif
