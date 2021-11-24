#include <Arduino.h>
#include <main.h>
#include <pins.h>

#define CLOSE_DOOR_SLOW_BEEP_COUNT    4
#define CLOSE_DOOR_FAST_BEEP_COUNT    12

// Using an active buzzer.
// Blocking beep. Mind the for how long before this returns.
void beepPattern(int beepCount, int beepLengthMs, int restMs) {

  digitalWrite(LED_RED_PIN, 0); //on

  for(int n = 0; n < beepCount; n++) {
    if(n > 0) {
      delay(restMs);
    }
    digitalWrite(BUZZER_PIN, 1);
    delay(beepLengthMs);
    digitalWrite(BUZZER_PIN, 0);
  }
}

void closingDoorAlarm() {
  beepPattern(CLOSE_DOOR_SLOW_BEEP_COUNT, 2000, 1000);
  beepPattern(CLOSE_DOOR_FAST_BEEP_COUNT, 200, 100);
}
