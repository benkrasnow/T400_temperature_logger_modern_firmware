/*
 * Function to turn power on and off
 */

#include <avr/sleep.h>
#include "t400.h"

#ifndef POWER_H
#define POWER_H

namespace Power {
  inline void setup() {
    // Hold the board's power latch on (PD5 low; high turns the board off). The T400 bootloader
    // also does this, but do not rely on it: if the latch were ever released, the board powers off.
    pinMode(PWR_ONOFF_PIN, OUTPUT);
    digitalWrite(PWR_ONOFF_PIN, LOW);

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  }
  
  // Put the processor into sleep mode
  inline void sleep() {
    cli();
    sleep_enable();
    sei();
    sleep_cpu();
    /* wake up here */
    sleep_disable();
  }
  
  // Turn off the power to the board
  inline void shutdown() {
    // Wait until the power button has been released
    while(digitalRead(BUTTON_POWER_PIN) == HIGH) {};
    
    // Then wait a little longer just to be safe
    delay(200);
    
    //Then turn off the power
    digitalWrite(PWR_ONOFF_PIN, HIGH);
  }
}

#endif
