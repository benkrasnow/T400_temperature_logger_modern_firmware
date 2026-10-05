// T400 hardware diagnostic: I2C scan, ambient sensor, and raw ADC reads done two ways.
// Upload, open Serial Monitor (any baud), and copy the output.
//
//  Method A: how the original Pax MCP3424 driver reads (3 bytes)
//  Method B: how the MCP342x library reads (4 bytes)
//  The 4-byte dump shows what the chip really returns for a 4th byte.
#include <Wire.h>
#include <MCP342x.h>
#include <MCP9800.h>

#define ADC_ADDR 0x69
MCP342x adc(ADC_ADDR);
MCP9800 amb(0);

void setup() {
  pinMode(30, OUTPUT);       // board power latch (the bootloader normally does this)
  digitalWrite(30, LOW);
  Serial.begin(9600);
  Wire.begin();
  amb.writeConfig(MCP9800::ADC_RES_12BITS);
}

void loop() {
  Serial.println(F("---- T400 diagnostic ----"));

  Serial.print(F("I2C devices (expect 0x48 0x68 0x69):"));
  for(uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if(Wire.endTransmission() == 0) { Serial.print(F(" 0x")); Serial.print(a, HEX); }
  }
  Serial.println();

  Serial.print(F("Ambient (C*16): "));
  Serial.println(amb.readTempC16(MCP9800::AMBIENT));

  for(uint8_t ch = 0; ch < 4; ch++) {
    Serial.print(F("ADC ch")); Serial.print(ch); Serial.print(F(":  "));

    // Method A: start one-shot, 16 bit, gain x8, then read 3 bytes
    Wire.beginTransmission(ADC_ADDR);
    Wire.write(0x80 | (ch << 5) | 0x08 | 0x03);
    uint8_t e = Wire.endTransmission();
    delay(100);
    uint8_t n = Wire.requestFrom((uint8_t)ADC_ADDR, (uint8_t)3);
    uint8_t b0 = Wire.read(), b1 = Wire.read(), b2 = Wire.read();
    Serial.print(F("A: tx_err=")); Serial.print(e);
    Serial.print(F(" got=")); Serial.print(n);
    Serial.print(F(" raw=")); Serial.print((int16_t)((b0 << 8) | b1));
    Serial.print(F(" cfg=0x")); Serial.print(b2, HEX);
    Serial.print(F(" (ready=")); Serial.print((b2 & 0x80) ? F("NO") : F("yes")); Serial.print(')');

    // 4 byte dump of the same finished conversion
    n = Wire.requestFrom((uint8_t)ADC_ADDR, (uint8_t)4);
    Serial.print(F("  4-byte read got=")); Serial.print(n); Serial.print(F(":"));
    while(Wire.available()) { Serial.print(F(" 0x")); Serial.print(Wire.read(), HEX); }

    // Method B: MCP342x library on the same channel
    long result = 0;
    MCP342x::Config status;
    adc.convert(MCP342x::Config(ch + 1, false, 16, 8));
    delay(100);
    MCP342x::error_t err = adc.read(result, status);
    Serial.print(F("  B: err=")); Serial.print((int)err);
    Serial.print(F(" raw=")); Serial.print(result);
    Serial.println();
  }
  Serial.println();
  delay(1000);
}
