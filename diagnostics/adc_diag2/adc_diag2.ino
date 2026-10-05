// T400 ADC diagnostic v2: prints a line before and after every I2C step, with I2C timeouts enabled
// so a stuck bus is reported instead of freezing the sketch. Open Serial Monitor and copy everything.
#include <Wire.h>

#define ADC_ADDR 0x69

void report(const __FlashStringHelper* what, uint8_t v) {
  Serial.print(what); Serial.print(v);
  if(Wire.getWireTimeoutFlag()) { Serial.print(F("  <-- I2C TIMEOUT")); Wire.clearWireTimeoutFlag(); }
  Serial.println();
}

void setup() {
  pinMode(30, OUTPUT);       // board power latch (the bootloader normally does this)
  digitalWrite(30, LOW);
  Serial.begin(9600);
  Wire.begin();
  Wire.setWireTimeout(20000, true);   // 20 ms, reset the bus on timeout
}

void loop() {
  Serial.println(F("---- T400 ADC diagnostic v2 ----"));
  for(uint8_t ch = 0; ch < 4; ch++) {
    Serial.print(F("ch")); Serial.println(ch);

    Wire.beginTransmission(ADC_ADDR);
    Wire.write(0x80 | (ch << 5) | 0x08 | 0x03);   // start one-shot, 16 bit, gain x8
    report(F("  start conversion, endTransmission result = "), Wire.endTransmission());

    for(uint8_t poll = 0; poll < 12; poll++) {
      delay(10);
      uint8_t n = Wire.requestFrom((uint8_t)ADC_ADDR, (uint8_t)4);
      Serial.print(F("  poll ")); Serial.print(poll);
      Serial.print(F(": requestFrom returned ")); Serial.print(n);
      if(Wire.getWireTimeoutFlag()) { Serial.print(F(" <-- I2C TIMEOUT")); Wire.clearWireTimeoutFlag(); }
      Serial.print(F("  bytes:"));
      uint8_t b[4] = {0, 0, 0, 0}, cnt = 0;
      while(Wire.available() && cnt < 4) b[cnt++] = Wire.read();
      for(uint8_t i = 0; i < cnt; i++) { Serial.print(F(" 0x")); Serial.print(b[i], HEX); }
      if(cnt >= 3 && !(b[2] & 0x80)) {
        Serial.print(F("  READY raw=")); Serial.print((int16_t)((b[0] << 8) | b[1]));
        Serial.println();
        break;
      }
      Serial.println();
    }
  }
  Serial.println();
  delay(1500);
}
