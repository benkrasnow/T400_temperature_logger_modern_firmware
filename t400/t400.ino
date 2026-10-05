/*
# t400-firmware (modernized for Arduino IDE 2.x)

Firmware for the Pax Instruments T400 temperature datalogger.
See ../README.md for setup instructions and the list of libraries to install.
*/


// Import libraries
#include "t400.h"             // Board definitions

#include <Wire.h>             // I2C
#include <SPI.h>

#include <MCP342x.h>          // ADC
#include <MCP9800.h>          // Ambient/junction temperature sensor
#include "power.h"            // Manage board power
#include "buttons.h"          // User buttons
#include "typek_constant.h"   // Thermocouple calibration table
#include "functions.h"        // Misc. functions
#include "sd_log.h"           // SD card utilities
#include "fmt.h"              // Integer formatting
#include "rtc.h"              // DS3231 real time clock
#include "calibration.h"      // 0 C ice bath calibration

#include <avr/wdt.h>

char fileName[] =        "LD0001.CSV";

// MCP3424 for thermocouple measurements: one-shot, 16 bit, gain x8.
// Thin wrapper around the MCP342x library that returns the result in microvolts.
class ThermocoupleAdc {
public:
  explicit ThermocoupleAdc(uint8_t address) : adc(address) {}

  void begin() { adc.configure(MCP342x::Config(1, false, 16, 8)); }

  // Start a one-shot conversion on channel 0-3
  void startMeasurement(uint8_t channel) {
    adc.convert(MCP342x::Config(channel + 1, false, 16, 8));
  }

  // Returns true (and latches the result) if the conversion has finished
  bool measurementReady() {
    long raw;
    MCP342x::Config status;
    if(adc.read(raw, status) != MCP342x::errorNone) return false;

    // Out of range: flag with INT32 limits. (Careful: int is 16 bit here, so -0x8000 would be
    // the unsigned value 32768; use long constants.)
    if(raw >= 32767L)       value = INT32_MAX;
    else if(raw <= -32768L) value = INT32_MIN;
    else                    value = (raw * 1000) / 128;  // 62.5uV LSB / gain 8 = 7.8125uV
    return true;
  }

  int32_t getMeasurementUv() { return value; }

private:
  MCP342x adc;
  int32_t value = 0;
};

ThermocoupleAdc thermocoupleAdc(MCP3424_ADDR);

MCP9800 ambientSensor(0);      // Ambient temperature sensor

// Map of ADC inputs to thermocouple channels
const uint8_t temperatureChannels[SENSOR_COUNT] PROGMEM = {1, 0, 3, 2};

int16_t temperatures_int[SENSOR_COUNT] = {OUT_OF_RANGE_INT,OUT_OF_RANGE_INT,
                                          OUT_OF_RANGE_INT,OUT_OF_RANGE_INT};

// Ambient temperature
int16_t ambient =  0;

boolean backlightEnabled = true;

// Available log intervals, in seconds
#define LOG_INTERVAL_COUNT  7
const uint8_t logIntervals[LOG_INTERVAL_COUNT] PROGMEM = {0, 1, 2, 5, 10, 30, 60};
#define LOG_INTERVAL_S(i)   pgm_read_byte(&logIntervals[i])   // table is in flash to save RAM

// Timer 1 related variables
#if 1
#define TIMER_ISR_MS        100
uint16_t m_sample_interval_ms = TIMER_ISR_MS;
volatile uint16_t m_timer_isr_counter_limit = 1;
#endif

// currently selected log interval (Default 0=0.5sec. MUST SET FLAG)
volatile uint8_t m_logInterval    = 0;
volatile bool flag_halfsecond = true;   // If true we are doing 1/2 second

boolean logging = false;    // True if we are currently logging to a file

volatile uint8_t m_sample_tenth = 0;   // tenths of a second after the RTC tick of the latest sample (0 or 5)
volatile bool m_sample_flag = false;     // If true, the display should be redrawn

volatile uint8_t isrTick = 0;        // Number of 1-second tics that have elapsed since the last sample
uint8_t lastIsrTick = 0;    // Last tick that we redrew the screen
// Whole seconds elapsed since the first sample after the last reset (start/stop logging, interval or unit
// change, boot). It is incremented on every RTC tick, so it starts at -1 and the first sample is 0.
#define ELAPSED_RESET       0xFFFFFFFFUL
volatile uint32_t logTimeSeconds = ELAPSED_RESET;   // tick count: only used while the RTC is unset

// Elapsed time is normally computed from the RTC (in tenths of a second), relative to the first sample after
// a reset. That keeps it consistent with the timestamp column and immune to stray interrupts.
bool elapsedRebase = true;           // capture a new baseline at the next sample
uint32_t elapsedBaseTenths;          // RTC time at the baseline, in tenths of a second (wraps on purpose)

uint8_t temperatureUnit;    // Measurement unit for temperature

uint8_t graphChannel = 4;

volatile uint8_t btn_disable_count = 0;
volatile uint8_t sd_full_count = 0;

// Calibration button: holding the units button for CAL_HOLD_MS starts a 0 C ice bath calibration.
// A quick press (shorter than CAL_HINT_MS) still cycles the units; it now acts when the button is released.
#define CAL_BUTTON          BUTTON_C
#define CAL_BUTTON_PIN      BUTTON_C_PIN
#define CAL_HOLD_MS         5000UL
#define CAL_HINT_MS         1000UL    // held longer than this: show the hint, and a release is not a quick press
#define CAL_DEBOUNCE_MS     150UL     // ignore the contact bounce that follows a release
bool     calBtnHeld = false;
bool     calBtnFired = false;
uint32_t calBtnStart;
uint32_t calBtnReleasedAt = (uint32_t)-1000;

void rotateTemperatureUnit() {
  // Rotate the unit
  temperatureUnit = (temperatureUnit + 1) % TEMPERATURE_UNITS_COUNT;

  // Reset the graph so we don't have to worry about scaling it
  //resetGraph();

  // TODO: Convert the current data to new units?
  return;
}

int16_t convertTemperatureInt(int16_t celcius) {
  return convertTenths(celcius, temperatureUnit);
}

// This function runs once. Use it for setting up the program state.
void setup(void) {
  uint8_t x;

  Power::setup();
  ChargeStatus::setup();
  //#if SERIAL_OUTPUT_ENABLED
  Serial.begin(9600);
  //#endif

  Wire.begin(); // Start using the Wire library; does the i2c communication.

  Backlight::setup();
  Backlight::set(backlightEnabled);

  setupDisplay();
  resetGraph();

  thermocoupleAdc.begin();

  ambientSensor.begin();
  ambientSensor.writeConfig(MCP9800::ADC_RES_12BITS);

  // Set up the RTC to generate a 1 Hz signal
  pinMode(RTC_INT, INPUT);
  Rtc::begin();             // 1 Hz square wave on SQW/INT, alarms off
  Cal::load();              // calibration offsets from EEPROM

  // And configure the atmega to interrupt on falling edge of the 1 Hz signal
  EICRA |= _BV(ISC21);    // Configure INT2 to trigger on falling edge
  EIMSK |= _BV(INT2);    // and enable the INT2 interrupt

  setupButtons();

  timer1_reset();

  wdt_enable(WDTO_2S);

  // Kick off the ADC sampling loop
  adc_start_next_conversion();

  return;
}

void startLogging()
{
  bool result;
  if(logging) return;
  sd::init();
  result = sd::open(fileName);
  if(!result)
  {
      sd_full_count = 3;
  }
  logging = result;
  return;
}

void stopLogging() {
  if(!logging) return;

  logging = false;
  sd::close();
  return;
}

#if DEBUG_FAKE_DATA
void fake_data(){
    // DEBUG: Fake some data

    temperatures_int[0] = OUT_OF_RANGE_INT;
    temperatures_int[1] = OUT_OF_RANGE_INT;
    temperatures_int[2] = OUT_OF_RANGE_INT;
    temperatures_int[3] = OUT_OF_RANGE_INT;
    #if 0
    // EXTREME!
    temperatures_int[0] = 30000; // 3270.9 C
    temperatures_int[1] = -2732; // -273.2C (abs zero)
    temperatures_int[2] = OUT_OF_RANGE_INT;
    temperatures_int[3] = OUT_OF_RANGE_INT;
    #endif
    #if 0
    temperatures_int[0] = 1234;
    temperatures_int[1] = -345;
    temperatures_int[2] = OUT_OF_RANGE_INT;
    temperatures_int[3] = OUT_OF_RANGE_INT;
    #endif
    #if 0
    #define OFFSET  30.0
    #define SCALE   10.0
    #define ADD     0.05
    static double val=0.0;
    double tmpdbl;
    tmpdbl = ((SCALE*sin(val))+OFFSET)*10;
    temperatures_int[0] = (int16_t)tmpdbl;
    temperatures_int[1] = (int16_t)tmpdbl+5.0;
    val += ADD;
    #endif

    #if 1
    static int16_t val=300;
    static int16_t step=5;
    temperatures_int[0] = val;
    temperatures_int[1] = val+50;
    //temperatures_int[2] = val+100;
    //temperatures_int[3] = val+150;
    val+=step;
    if(val>=400 || val<= 200) step=step*-1;
    #endif

#if 0
    temperatures_int[0] = convertTemperatureInt(temperatures_int[0]);
    temperatures_int[1] = convertTemperatureInt(temperatures_int[1]);
    temperatures_int[2] = convertTemperatureInt(temperatures_int[2]);
    temperatures_int[3] = convertTemperatureInt(temperatures_int[3]);
#endif

    return;
}
#endif

uint8_t m_channel_index = SENSOR_COUNT;
static void adc_start_next_conversion()
{
    m_channel_index = (m_channel_index+1);
    if(m_channel_index>=SENSOR_COUNT) m_channel_index=0;
    thermocoupleAdc.startMeasurement(pgm_read_byte(&temperatureChannels[m_channel_index]));
    return;
}

static int16_t adc_read_ambient()
{
    int32_t tmpint32;
    int16_t tmpint16;
    // This gets the temperature as an integer which is °C times 16.
    tmpint32 = ambientSensor.readTempC16(MCP9800::AMBIENT);
    // Convert 1/16ths to 1/10ths, rounding. (Shifting down to whole degrees first would throw
    // away the cold junction resolution and make every reading up to 1 C low.)
    tmpint16 = ((tmpint32 * 10) + 8) >> 4;
    return tmpint16;
}

// Print the outcome of a calibration run over serial: "CAL 1:ok(-12) 2:-- 3:?? 4:--"
// ok = calibrated (offset in uV), -- = no probe, ?? = rejected (unstable or not near 0 C), offset unchanged
static void reportCalibration()
{
  Serial.print(F("CAL"));
  for(uint8_t ch = 0; ch < SENSOR_COUNT; ch++) {
    Serial.print(' ');
    Serial.print(ch + 1);
    Serial.print(':');
    Serial.print(Cal::resultCode(ch, 0));
    Serial.print(Cal::resultCode(ch, 1));
    if(Cal::result(ch) == Cal::CALIBRATED) {
      Serial.print('(');
      Serial.print(Cal::offsetUv(ch));
      Serial.print(')');
    }
  }
  Serial.println();
}

static void readTemperatures()
{
    int32_t measuredVoltageUv;
    int32_t compensatedVoltage;
    int32_t tmpint32;
    int16_t tmpint16=0;
    //float tmpflt;

    // Skip if we don't have a temperature to measure?
    //if(!thermocoupleAdc.measurementReady()) return;

    // This gets the temperature as an integer
    ambient = adc_read_ambient();

    // This function should be called when there is a measurement ready
    // to be read.  This value is the temperature for channel stored
    // in m_channel_index


    // getMeasurementUv returns an int32_t which is the value in micro volts for this channel
    tmpint32 = thermocoupleAdc.getMeasurementUv();

    // The ADC reports its limits when the input is open or saturated. Do not push those through the
    // calibration arithmetic (it would overflow and could produce a plausible looking temperature).
    if(tmpint32 == INT32_MAX || tmpint32 == INT32_MIN) {
      if(Cal::sample(m_channel_index, false, 0)) reportCalibration();
      #if !DEBUG_FAKE_DATA
      temperatures_int[m_channel_index] = OUT_OF_RANGE_INT;
      #endif
      adc_start_next_conversion();
      return;
    }

#if 0
    /******************* Float Math Start ********************/
    // Now we need to calibrate things.  This is y=mx+b
    // Calibration value: MCP3424_OFFSET_CALIBRATION
    tmpflt =  (((float)tmpint32)* MCP3424_CALIBRATION_MULTIPLY) + MCP3424_CALIBRATION_ADD;
    measuredVoltageUv = (uint32_t)tmpflt;
    /******************* Float Math End ********************/
    //(61277 * 1.00713) + 5.826 = 61719.73101 -> 61719
    //(7458 * 1.00713) + 5.826 = 7517.00154 -> 7517
#else
    // max microvolts is 61277
    // max could be (61277*10071)+58260 = 617178927
    //25C (7458*10071)+58260 = 75167778
    tmpint32 =  (tmpint32*MCP3424_CALIBRATION_MUL_INT) + MCP3424_CALIBRATION_ADD_INT;
    // max could be 617178927 / 10000 = 61717
    // 25C 75167778/10000 = 7516
    tmpint32 = tmpint32 / 10000;
    // The original code never assigned measuredVoltageUv on this path (it was only set in the
    // disabled float branch above), so the calibrated value was silently dropped.
    measuredVoltageUv = tmpint32;
#endif

    // Get the measured voltage, removing the ambient junction temperature
    //compensatedVoltage = measuredVoltageUv + celcius_to_microvolts( (((float)(ambient))/10.0) );
    compensatedVoltage = measuredVoltageUv + celcius_to_microvolts(ambient);


    // While calibrating, hand the uncorrected voltage to the calibration code
    if(Cal::running()) {
      bool valid = (microvolts_to_celcius(compensatedVoltage) != OUT_OF_RANGE_INT);
      if(Cal::sample(m_channel_index, valid, compensatedVoltage)) reportCalibration();
    }

    // Given a voltage, get the temperature. The stored calibration offset is a per channel
    // correction (in microvolts) that makes the channel read 0 C in an ice bath.
    tmpint16 = microvolts_to_celcius(compensatedVoltage + Cal::offsetUv(m_channel_index));


    #if !DEBUG_FAKE_DATA
    temperatures_int[m_channel_index] = tmpint16;
    #endif

    // We are done with this channel, kick off the next one
    adc_start_next_conversion();

    return;
}

// Send one piece of the current row to the serial port and, if logging, to the SD card.
// A row is assembled from small pieces instead of one big buffer, which saves about 70 bytes of RAM.
static void emit(const char* text)
{
  #if SERIAL_OUTPUT_ENABLED
  Serial.print(text);
  #endif
  if(logging) sd::write(text);
}

static void writeOutputs()
{
  char piece[24];       // one field: ", " plus a timestamp (21 characters) plus the NUL is the longest

  // Read the clock once; it provides both the elapsed time and the timestamp
  Rtc::Time now;
  bool haveTime = Rtc::isSet() && Rtc::read(now);

  // Elapsed time in tenths of a second
  uint32_t elapsedTenths;
  if(haveTime) {
    uint32_t t = Rtc::toSeconds(now) * 10 + m_sample_tenth;   // wraps modulo 2^32; differences stay correct
    if(elapsedRebase || (int32_t)(t - elapsedBaseTenths) < 0) {   // new baseline, or the clock was set backwards
      elapsedBaseTenths = t;
      elapsedRebase = false;
    }
    elapsedTenths = t - elapsedBaseTenths;
  } else {
    // Clock not set: fall back to counting RTC ticks (32 bit, updated in an interrupt, so read it atomically)
    noInterrupts();
    uint32_t ticks = logTimeSeconds;
    interrupts();
    elapsedTenths = ticks * 10 + m_sample_tenth;
  }
  uint8_t n = fmtUInt(piece, elapsedTenths / 10);
  if(flag_halfsecond) {            // 500 ms mode: "12.0" and "12.5"
    piece[n++] = '.';
    piece[n++] = '0' + (elapsedTenths % 10);
    piece[n] = 0;
  }
  emit(piece);

  // Timestamp
  piece[0] = ',';
  piece[1] = ' ';
  if(haveTime) {
    fmtTimestamp(piece + 2, now, m_sample_tenth);
  } else {
    strcpy_P(piece + 2, PSTR("unset"));
  }
  emit(piece);

  for(uint8_t i = 0; i < SENSOR_COUNT; i++)
  {
    piece[0] = ',';
    piece[1] = ' ';
    if(temperatures_int[i] == OUT_OF_RANGE_INT)
    {
      piece[2] = '-';
      piece[3] = 0;
    }else {
      // Log in the selected unit, matching the column headers written by sd::open()
      fmtTenths(piece + 2, convertTemperatureInt(temperatures_int[i]));
    }
    emit(piece);
  }

  #if SERIAL_OUTPUT_ENABLED
  Serial.println();
  #endif

  if(logging) {
    logging = sd::endRow();
  }
  return;
}

// Reset the tick counter, so that a new measurement takes place within 1 second
void resetTicks()
{
  uint8_t sec;
  noInterrupts();
  sec = LOG_INTERVAL_S(m_logInterval);
  if(sec>0){
      isrTick = sec - 1;
      flag_halfsecond = false;
      timer1_stop();
  }else{
      isrTick = 0;
      flag_halfsecond = true;
  }
  logTimeSeconds = ELAPSED_RESET;
  elapsedRebase = true;
  interrupts();
  return;
}

// Serial commands from the PC (one per line):
//   H                       reply with the status line (unit, interval, logging file)
//   C?  /  C0  /  C!        show the calibration offsets / clear them / start a calibration run
//   T?                      reply with the RTC time
//   T2026-10-04 12:34:56    set the RTC (local time); reply "OK <time>" or "ERR <reason>"
// tools/set-time.ps1 and tools/upload.ps1 use this to sync the clock to the PC.
static uint8_t parseDigits(const char* p, uint8_t n, bool& ok) {
  uint8_t v = 0;
  for(uint8_t i = 0; i < n; i++) {
    if(p[i] < '0' || p[i] > '9') { ok = false; return 0; }
    v = v * 10 + (p[i] - '0');
  }
  return v;
}

static void printRtcTime() {
  char text[22];
  Rtc::Time t;
  if(Rtc::isSet() && Rtc::read(t)) {
    fmtTimestamp(text, t, 0);
    text[19] = 0;   // whole seconds
    Serial.println(text);
  } else {
    Serial.println(F("unset"));
  }
}

static void handleCalCommand(const char* cmd, uint8_t len) {
  if(len != 2) { Serial.println(F("ERR format")); return; }
  if(cmd[1] == '?') {
    Serial.print(F("OK offsets uV:"));
    for(uint8_t ch = 0; ch < SENSOR_COUNT; ch++) { Serial.print(' '); Serial.print(Cal::offsetUv(ch)); }
    Serial.println();
  } else if(cmd[1] == '0') {
    Cal::clear();
    Serial.println(F("OK"));
  } else if(cmd[1] == '!' && !logging && Cal::start()) {
    Serial.println(F("OK"));
  } else {
    Serial.println(F("ERR"));
  }
}

// Status line for the PC, sent on request (H) and whenever the unit, interval or logging state changes:
//   S,<firmware version>,<unit C/F/K>,<interval in seconds, 0 = 500 ms>,<log file name, or - if not logging>
static void printStatus() {
  Serial.print(F("S," FIRMWARE_VERSION ","));
  Serial.print(unitLetter(temperatureUnit));
  Serial.print(',');
  Serial.print(LOG_INTERVAL_S(m_logInterval));
  Serial.print(',');
  if(logging) Serial.print(fileName); else Serial.print('-');
  Serial.println();
}

static void handleCommand(const char* cmd, uint8_t len) {
  if(cmd[0] == 'H') { printStatus(); return; }
  if(cmd[0] == 'C') { handleCalCommand(cmd, len); return; }
  if(cmd[0] != 'T') return;
  if(len == 2 && cmd[1] == '?') {
    Serial.print(F("OK "));
    printRtcTime();
    return;
  }
  // "T2026-10-04 12:34:56" is 20 characters
  bool ok = (len == 20) && cmd[1] == '2' && cmd[2] == '0' && cmd[5] == '-' && cmd[8] == '-' &&
            cmd[11] == ' ' && cmd[14] == ':' && cmd[17] == ':';
  Rtc::Time t;
  if(ok) {
    t.year   = 2000 + parseDigits(cmd + 3, 2, ok);   // only 20xx is supported
    t.month  = parseDigits(cmd + 6, 2, ok);
    t.day    = parseDigits(cmd + 9, 2, ok);
    t.hour   = parseDigits(cmd + 12, 2, ok);
    t.minute = parseDigits(cmd + 15, 2, ok);
    t.second = parseDigits(cmd + 18, 2, ok);
  }
  if(!ok)           { Serial.println(F("ERR format")); return; }
  if(logging)       { Serial.println(F("ERR logging")); return; }
  if(!Rtc::set(t))  { Serial.println(F("ERR range")); return; }
  elapsedRebase = true;     // the clock jumped, so restart elapsed time
  Serial.print(F("OK "));
  printRtcTime();
}

static void pollSerialCommands() {
  static char line[24];
  static uint8_t len = 0;
  while(Serial.available()) {
    char c = Serial.read();
    if(c == '\n' || c == '\r') {
      if(len > 0 && len < sizeof(line)) { line[len] = 0; handleCommand(line, len); }
      len = 0;
    } else if(len < sizeof(line)) {
      line[len++] = c;
    } else {
      len = sizeof(line) + 1;   // overlong line: discard until the next newline
    }
  }
}

// This function is called periodically, and performs slow tasks:
// Taking measurements
// Updating the screen
void loop()
{
  // If true, the display needs to be updated
  bool refresh_display_flag = false;

  // If true, tell the PC that the unit, interval or logging state changed
  bool statusChanged = false;

  // Feed the watchdog, except while the IDE is asking for a reset into the bootloader.
  // The Arduino USB core requests that by opening and closing the port at 1200 baud, which arms a
  // 120 ms watchdog and expects the sketch to stop feeding it. Without this check the reset never
  // happens (loop() runs more often than every 120 ms) and uploads need the manual button method.
  if(!(Serial.baud() == 1200 && !Serial.dtr())) {
    wdt_reset();
  }

  pollSerialCommands();

  // This will read temperatures as fast as we can, this decouples the
  // slow reading from blocking the rest of the system
  if(thermocoupleAdc.measurementReady())
      readTemperatures();

  // This locks in the samples into the array and does some other stuff. This
  // controls the sample rate of the data
  if(m_sample_flag)
  {
    m_sample_flag = false;

    // DEBUG, force fake values for testing
    #if DEBUG_FAKE_DATA
    fake_data();
    #endif

    // Write the data to serial AND the SD card
    writeOutputs();

    // Update some graph data.
    updateGraphData(temperatures_int);
    updateGraphScaling();

    // Indicate we want to redraw the display
    refresh_display_flag = true;
  }

  // Check for button presses
  if(buttonPending()) {
    uint8_t button = buttonGetPending();

    switch(button){
    case BUTTON_POWER:
      // Disable power
      if(!logging) {
        clear();
        Backlight::set(0);
        Power::shutdown();
      }else{
        btn_disable_count = 3;
        refresh_display_flag = true;
      }
      break;

    case BUTTON_A:
      // Start/stop logging
      #if SD_LOGGING_ENABLED
      // NOTE: Logging takes up 30% of the flash!!!
      if(!logging) {
          // This will block for a bit
          startLogging();
      } else {
          stopLogging();
      }
      resetTicks();
      refresh_display_flag = true;
      statusChanged = true;
      #endif
      break;

    case BUTTON_B:
      // Cycle log interval
      if(!logging) {
        m_logInterval = (m_logInterval + 1) % LOG_INTERVAL_COUNT;
        resetTicks();
        resetGraph();  // Reset the graph, to keep the x axis consistent
        statusChanged = true;
      }else{
          btn_disable_count = 3;
      }
      refresh_display_flag = true;
      break;
    case CAL_BUTTON:
      // Quick press: cycle temperature units (done on release, below). Hold: calibrate.
      if(!calBtnHeld && (millis() - calBtnReleasedAt) > CAL_DEBOUNCE_MS) {
        calBtnHeld = true;
        calBtnFired = false;
        calBtnStart = millis();
      }
      break;
    case BUTTON_D:
      // Sensor display mode
      graphChannel = (graphChannel + 1) % GRAPH_CHANNELS_COUNT;
      while( (graphChannel < SENSOR_COUNT) && (temperatures_int[graphChannel] == OUT_OF_RANGE_INT) )
      {
        graphChannel = (graphChannel + 1) % GRAPH_CHANNELS_COUNT;
      }
      refresh_display_flag = true;
      break;
    case BUTTON_E:
      // Toggle backlight
      backlightEnabled = !backlightEnabled;
      Backlight::set(backlightEnabled);
      break;

    default: break;
    } // end button select

  } // end if button pending

  // Track the calibration button while it is held
  if(calBtnHeld) {
    bool down = (digitalRead(CAL_BUTTON_PIN) == LOW);
    uint32_t held = millis() - calBtnStart;
    if(down) {
      if(!calBtnFired && held >= CAL_HOLD_MS) {
        calBtnFired = true;
        Cal::setHint(false);
        if(logging) {
          btn_disable_count = 3;          // "Disabled while logging"
        } else {
          Cal::start();
        }
        refresh_display_flag = true;
      } else if(!calBtnFired && held >= CAL_HINT_MS && Cal::state() == Cal::IDLE) {
        Cal::setHint(true);
        refresh_display_flag = true;
      }
    } else {
      if(!calBtnFired && held < CAL_HINT_MS) {     // a quick press: cycle the units
        if(!logging) {
          rotateTemperatureUnit();
          resetTicks();
          statusChanged = true;
        } else {
          btn_disable_count = 3;
        }
      }
      Cal::setHint(false);
      calBtnHeld = false;
      calBtnReleasedAt = millis();
      refresh_display_flag = true;
    }
  }

  if(statusChanged) printStatus();

  // Redraw when the calibration message changes (it also times out by itself)
  static Cal::State lastCalState = Cal::IDLE;
  if(Cal::state() != lastCalState) {
    lastCalState = Cal::state();
    refresh_display_flag = true;
  }

  // If we are charging, refresh the display every second to make the charging animation
  if(ChargeStatus::get() == ChargeStatus::CHARGING) {
    if(lastIsrTick != isrTick) {
      refresh_display_flag = true;
      lastIsrTick = isrTick;
    }
  }

  // Draw the display
  if(refresh_display_flag)
  {
    char * ptr = NULL;
    if(logging) ptr = fileName;

    refresh_display_flag = false;

    // Actual draw of display, takes a bit of time
    draw(graphChannel,
      temperatureUnit,
      ptr,
      LOG_INTERVAL_S(m_logInterval),
      ChargeStatus::get(),
      ChargeStatus::getBatteryLevel()
    );

  }

  // Sleep if we are on battery power
  // Note: Don't sleep if there is power, in case we need to communicate over USB
  if(ChargeStatus::get() == ChargeStatus::DISCHARGING && !calBtnHeld && !Cal::running()) {
    Power::sleep();
  }


  return;
}


// 1 Hz interrupt from RTC
// TODO: Why not use a timer?
ISR(INT2_vect)
{
  Cal::secondTick();
  m_sample_tenth = 0;
  logTimeSeconds++;      // every tick, in every mode (it used to stop counting in 500 ms mode)
  if(flag_halfsecond)
  {
      m_sample_flag = true;
      // If half second processing, kick off the timer
      config_sample_time_ms(500);
  }else{
      isrTick = (isrTick + 1)%(LOG_INTERVAL_S(m_logInterval));
      if(isrTick == 0)
      {
        m_sample_flag = true;
      }
  }

  if(btn_disable_count>0) btn_disable_count--;
  if(sd_full_count>0) sd_full_count--;

  return;
}

/**********************************************************
 * Timer 1 functions
 *********************************************************/
#if 1
void config_sample_time_ms(uint16_t time_ms)
{
    timer1_stop();

    // This is how many ms is in each sample
    //m_sample_interval_ms = time_ms;
    // This is the count limit for the timer
    m_timer_isr_counter_limit = (time_ms/TIMER_ISR_MS);
    // Configure the timer
    timer1_setup(TIMER_ISR_MS);

    // Start timer
    timer1_start();

    return;
}

// Setup timer 1 but don't start it
// _clockTimeRes is the number of milliseconds
void timer1_setup(uint8_t _clockTimeRes)
{
    uint16_t tmpu16;
    cli();

    // Clear timer 1
    TCCR1A = 0;
    TCCR1B = 0;

    // set Compare Match value:
    // t400 crystal is 8MHz
    // With prescale = 64
    // timer resolution = 1/( 8000000 /64) = 125000Hz = 0.000008 seconds. 1ms=125 counts

    // target time = timer resolution * (# timer counts + 1)
    // so timer counts = (target time)/(timer resolution) -1
    // For 1 ms interrupt, timer counts = 1E-3/8E-6 - 1 = 124
    tmpu16 = _clockTimeRes;
    tmpu16 = (uint16_t)(tmpu16 * 124);

    // Maximum time is 263ms
    OCR1A = tmpu16;

    // Turn on CTC mode:
    TCCR1B |= (1 << WGM12);

    // Enable timer compare interrupt:
    TIMSK1 |= (1 << OCIE1A);

    // Interrupt enable
    sei();

    return;
}

void timer1_start()
{
    cli();

    // Start the timer 1 counting, with prescaler 64
    TCCR1B |= (1 << CS11) | (1 << CS10);
    // Start the timer counting, with prescaler 1024
    //TCCR1B |= (1 << CS12) | (1 << CS10);

    sei();
}

void timer1_stop()
{
    cli();
    //Stop the timer counting
    TCCR1B &= 0B11111000;
    sei();
}

void timer1_reset()
{
    cli();
    TCCR1A = 0;  // set all bits of Timer/Counter 1 Control Register to 0
    TCCR1B = 0;
    // Reset the Counter value to 0:
    TCNT1 = 0;
    OCR1A = 0;

    sei();
}

// This code is triggered every time the global clock ticks
uint8_t isr_counter=0;
ISR(TIMER1_COMPA_vect)
{
    isr_counter+=1;
    if(isr_counter>=m_timer_isr_counter_limit)
    {
        isr_counter = 0;
        m_sample_tenth = 5;

        //Stop the timer counting
        TCCR1B &= 0B11111000;

        m_sample_flag = true;
    }
    return;
}
#endif


//eof

