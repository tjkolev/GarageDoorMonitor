#include <Arduino.h>
#include <TimeLib.h>
#include <main.h>
#include <pins.h>

#define MSG_BUFF_LEN 200
char msg[MSG_BUFF_LEN];

char textBuffer[TXT_BUFF_LEN];
void log(const char* format, ...)
{
  va_list args;
  va_start(args, format);

  snprintf(textBuffer, TXT_BUFF_LEN, "%lu ", millis());
  size_t txtLen = strlen(textBuffer);
  vsnprintf(textBuffer + txtLen, TXT_BUFF_LEN - txtLen, format, args);

  va_end(args);
  Serial.println(textBuffer);
}

const char* doorStateNames[] = {
  "Open",
  "Closed",
  "Ajar"
};

const char* getNamedDoorState(int doorState) {
  if(DOOR_OPEN <= doorState && doorState <= DOOR_AJAR) {
    return doorStateNames[doorState];
  }
  return "Unknown";
}

int getDoorState() {
  int doorDebounceStates[AppConfig.DebounceReadCount] = { DOOR_UNKNOWN };
  log("Debounce array: %d %d %d", doorDebounceStates[1], doorDebounceStates[2], doorDebounceStates[4]);

  while(true) { // need to get a definitive answer

    int rawVal = analogRead(POSITION_PIN);
    log("Position pin (%d) value: %d", POSITION_PIN, rawVal);

    int doorState = DOOR_UNKNOWN;
    for(int ndx = 0; ndx < DOOR_STATE_COUNT; ndx++) {
      if(AppConfig.SensorRangeValues[ndx][0] <= rawVal && rawVal <= AppConfig.SensorRangeValues[ndx][1]) {
        doorState = ndx;
        break;
      }
    }

    // debounce any value jitters
    bool debounced = false;

    if(doorState == DOOR_UNKNOWN) {
      log("No sensor range match for: %d", rawVal);
      // Notify
      int msgLen = snprintf((char*)msg, MSG_BUFF_LEN, "Sensor value %d falls in no valid range.", rawVal);
      sendNotification(IOT_EVENT_BAD_DATA, msg, msgLen);
    }
    else {
      // shift values left
      for(int n = 0; n < AppConfig.DebounceReadCount - 1; n++) {
        doorDebounceStates[n] = doorDebounceStates[n+1];
      }
      doorDebounceStates[AppConfig.DebounceReadCount - 1] = doorState;
      // check for same consecutive values
      debounced = true;
      for(int n = 0; n < AppConfig.DebounceReadCount - 1; n++) {
        if(doorDebounceStates[n] != doorState) {
          debounced = false;
          break;
        }
      }
    }

    if(debounced) {
      return doorState;
    }

    delay(AppConfig.DebounceReadPauseMs);
  }
}

bool doorShouldBeClosed(unsigned long openSinceMs) {

  if(openSinceMs != 0) {
    unsigned long doorOpenedForMs = millis() - openSinceMs;

    // if configured *don't close* the door if not opened min amount of time
    if(AppConfig.MinDoorOpenMs > 0 && doorOpenedForMs < AppConfig.MinDoorOpenMs) {
      log("Door is staying open. Min open time: %d, opened for: %d", AppConfig.MinDoorOpenMs, doorOpenedForMs);
      return false;
    }

    // if configured *close* the door if opened more than the max amount of time
    if(AppConfig.MaxDoorOpenMs > 0 && doorOpenedForMs > AppConfig.MaxDoorOpenMs) {
      log("Door should close. Max open time: %d, opened for: %d", AppConfig.MaxDoorOpenMs, doorOpenedForMs);
      return true;
    }
  }

  // check time of day
  if(timeSet != timeStatus()) {
    log("Door is staying open. Unreliable time.");
    sendNotification(IOT_EVENT_BAD_TIME);
    return false; // conservative choice
  }

  bool shouldClose = false;
  time_t t = now();
  int hhmm = hour(t) * 100 + minute(t);
  int keepClosedFrom = AppConfig.KeepClosedFromTo[0];
  int keepClosedTo = AppConfig.KeepClosedFromTo[1];
  if(keepClosedFrom > keepClosedTo) {
    // 2230 - 0600 - the usual case
    shouldClose = keepClosedFrom <= hhmm || hhmm <= keepClosedTo;
  }
  else {
    // 0900 - 1700
    shouldClose = keepClosedFrom <= hhmm && hhmm <= keepClosedTo;
  }

  log("Door should close: %s. Current time: %d. Configured closed time interval: [%d - %d]", (shouldClose ? "yes" : "no"), hhmm, keepClosedFrom, keepClosedTo);
  return shouldClose;
}

void activateDoor() { // It is in fact toggle door. Calling this if door is closed will open it.
  closingDoorAlarm();

  digitalWrite(GDOOR_PIN, HIGH);
  delay(AppConfig.DoorClosingSwitchPressMs);
  digitalWrite(GDOOR_PIN, LOW);
}

bool closeDoor() {
  // let's try to close the door
  sendNotification(IOT_EVENT_AUTO_CLOSING_DOOR);
  for(int attempt = 1; attempt <= AppConfig.MaxClosingTries; attempt++) {
    log("Door close try: %d.", attempt);
    int doorState = getDoorState();
    if(DOOR_OPEN == doorState) {
      activateDoor();
    }
    // give it time to close, and check
    // if door hasn't closed, activating again will open the door.
    delay(AppConfig.DoorClosingTimeMs);
    doorState = getDoorState();
    if(DOOR_CLOSED == doorState) {
      log("Door is closed.");
      sendNotification(IOT_EVENT_CLOSED_DOOR);
      return true;
    }
  }

  return false;
}

unsigned long doorOpenedSinceMs = 0;
unsigned long lastCloseAttemptMs = 0;

void checkDoor() {
  int doorState = getDoorState();

  if(DOOR_CLOSED == doorState) {
    if(lastCloseAttemptMs != 0) {
      // door was found closed, when previously it had failed to.
      sendNotification(IOT_EVENT_CLOSED_DOOR);
    }
    lastCloseAttemptMs = 0;
    doorOpenedSinceMs = 0;
    return;
  }

  // Door is not closed
  if(0 == doorOpenedSinceMs) {
    // Door was just found open. Mark the time.
    doorOpenedSinceMs = millis();
    if(0 == doorOpenedSinceMs) {
      doorOpenedSinceMs = 1; // 0 is our magic number.
    }
    return;
  }

  if(!doorShouldBeClosed(doorOpenedSinceMs)) {
    lastCloseAttemptMs = 0;
    return;
  }

  if(lastCloseAttemptMs != 0 && millis() - lastCloseAttemptMs < AppConfig.TimeBetweenClosingAttemptsMs) {
    log("Too soon to try and close door again. ");
    return;
  }

  bool doorClosed = closeDoor();
  if(doorClosed) {
    lastCloseAttemptMs = 0;
    doorOpenedSinceMs = 0;
    return;
  }

  // door didn't close when it should've
  lastCloseAttemptMs = millis();
  if(0 == lastCloseAttemptMs)
    lastCloseAttemptMs = 1; // don't want to mess up the 'flag' overload.

  // notify
  int msgLen = snprintf(msg, MSG_BUFF_LEN, "Door state: %s. Next attempt in %d minutes.",
                    getNamedDoorState(doorState),
                    (int) AppConfig.TimeBetweenClosingAttemptsMs / 1000 / 60);
  sendNotification(IOT_EVENT_CLOSING_FAILURE, msg, msgLen);
}

void setupIO() {

  pinMode(POSITION_PIN, INPUT);

  pinMode(GDOOR_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(LED_BLUE_PIN, OUTPUT);
  digitalWrite(LED_BLUE_PIN, 1); // off
  pinMode(LED_RED_PIN, OUTPUT);
  digitalWrite(LED_RED_PIN, 1); // off
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);		 // Start the Serial communication to send messages to the computer
  delay(10);

  log("\nSetting up...");

  setupIO();
  ensureWiFi();

  updateConfig();

  log("Ready. Version: " GDOOR_MONITOR_VERSION);
}

unsigned long lastLoopRun = 0;
bool resetNotificationSent = false;
bool flipLedOnOff = false;
void loop() {

  unsigned long now = millis();
  if(now - lastLoopRun > AppConfig.MainLoopMs) {

    if(!resetNotificationSent) {
      // Here because sometimes wifi is not ready in startup.
      resetNotificationSent = sendNotification(IOT_EVENT_RESET);
    }

    updateConfig();
    checkDoor();

    lastLoopRun = now;
  }

  // Blink red/blue LED based on WiFi state
  if(now % 2000 == 0) { // Every two seconds
    int ledPin;
    if(wifiConnected()) {
      digitalWrite(LED_RED_PIN, 1); // off
      ledPin = LED_BLUE_PIN;
    }
    else{
      digitalWrite(LED_BLUE_PIN, 1);
      ledPin = LED_RED_PIN;
    }
    digitalWrite(ledPin, flipLedOnOff ? 0 : 1);
    flipLedOnOff = !flipLedOnOff;
  }

  yield();
}