// T400 RTC / sample-interrupt diagnostic. Prints once a second over USB serial.
#include <Wire.h>
#define DS3231 0x68
#define RTC_INT 0

volatile uint16_t int2Count = 0;
ISR(INT2_vect) { int2Count++; }

uint8_t rd(uint8_t reg) {
  Wire.beginTransmission(DS3231); Wire.write(reg); Wire.endTransmission();
  Wire.requestFrom((uint8_t)DS3231, (uint8_t)1);
  return Wire.read();
}

void setup() {
  pinMode(30, OUTPUT); digitalWrite(30, LOW);   // power latch (bootloader normally does this)
  Serial.begin(9600);
  Wire.begin();
  pinMode(RTC_INT, INPUT);
  Wire.beginTransmission(DS3231); Wire.write(0x0E); Wire.write((uint8_t)0); Wire.endTransmission();
  EICRA |= _BV(ISC21);
  EIMSK |= _BV(INT2);
}

void loop() {
  static uint16_t last = 0;
  delay(1000);
  noInterrupts(); uint16_t c = int2Count; interrupts();
  Serial.print(F("INT2 edges total=")); Serial.print(c);
  Serial.print(F(" (+")); Serial.print(c - last); Serial.print(F(" in last second)"));
  last = c;
  Serial.print(F("  pin0=")); Serial.print(digitalRead(RTC_INT));
  Serial.print(F("  ctrl=0x")); Serial.print(rd(0x0E), HEX);
  Serial.print(F(" status=0x")); Serial.print(rd(0x0F), HEX);
  Serial.print(F(" seconds(bcd)=0x")); Serial.println(rd(0x00), HEX);
}
