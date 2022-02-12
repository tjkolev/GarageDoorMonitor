#include <Arduino.h>
#include <TimeLib.h>
#include <main.h>
#include <pins.h>

char* formatMillis(char* buff, unsigned long milliseconds) {
  // returns the millisconds formatted as d.hh:mm:ss.lll
  unsigned long tmillis = milliseconds;
  int msecs = (int) (tmillis % 1000);

  unsigned long tsecs;
  int secs = (int)((tsecs = tmillis / 1000) % 60);

  unsigned long tmins;
  int mins = (int)((tmins = tsecs / 60) % 60);

  unsigned long thours;
  int hours = (int)((thours = tmins / 60) % 24);

  int days = (int)(thours / 24);

  sprintf(buff, "%d.%02d:%02d:%02d.%03d", days, hours, mins, secs, msecs);
  return buff;
}

#define LOG_BUFF_LEN 600
char logMsgBuffer[LOG_BUFF_LEN];
char millisFmtBuffer[24];
const char* log(const char* format, ...)
{
  va_list args;
  va_start(args, format);

  size_t txtLen;
  if(timeSet == timeStatus()) {
    time_t t = now();
    txtLen = snprintf(logMsgBuffer, LOG_BUFF_LEN, "%4d-%02d-%02d %02d:%02d:%02d ", year(t), month(t), day(t), hour(t), minute(t), second(t));
  }
  else {
    txtLen = snprintf(logMsgBuffer, LOG_BUFF_LEN, "%s ", formatMillis(millisFmtBuffer, millis()));
  }

  //size_t txtLen = strlen(textBuffer);
  vsnprintf(logMsgBuffer + txtLen, LOG_BUFF_LEN - txtLen, format, args);

  va_end(args);
  Serial.println(logMsgBuffer);
  postLog(logMsgBuffer);

  return logMsgBuffer;
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
  int doorDebounceStates[AppConfig.DebounceReadCount] = { DOOR_UNKNOWN, DOOR_UNKNOWN, DOOR_UNKNOWN, DOOR_UNKNOWN, DOOR_UNKNOWN };

  while(true) { // need to get a definitive answer

    int rawVal = analogRead(POSITION_PIN);
    logd("Position pin (%d) value: %d", POSITION_PIN, rawVal);

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
      const char* logmsg = log("Sensor value %d falls in no valid range.", rawVal);
      sendNotification(IOT_EVENT_BAD_DATA, logmsg, -1);
    }
    else {
      // shift values left
      for(int n = 0; n < AppConfig.DebounceReadCount - 1; n++) {
        doorDebounceStates[n] = doorDebounceStates[n+1];
      }
      doorDebounceStates[AppConfig.DebounceReadCount - 1] = doorState;
      // check for same consecutive values
      logd("Debounce array:[%d %d %d %d %d]", doorDebounceStates[0], doorDebounceStates[1], doorDebounceStates[2], doorDebounceStates[3], doorDebounceStates[4]);
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
    char buff[24];

    logd("Door opened for %s. Min open time: %s. Max open time: %s", formatMillis(buff, doorOpenedForMs), AppConfig.txtMinOpenTime, AppConfig.txtMaxOpenTime);
    // if configured *don't close* the door if not opened min amount of time
    if(AppConfig.MinDoorOpenMs > 0 && doorOpenedForMs < AppConfig.MinDoorOpenMs) {
      return false;
    }

    // if configured *close* the door if opened more than the max amount of time
    if(AppConfig.MaxDoorOpenMs > 0 && doorOpenedForMs > AppConfig.MaxDoorOpenMs) {
      return true;
    }
  }

  // check time of day
  if(timeSet != timeStatus()) {
    const char* logmsg = log("Door is staying open. Unreliable time.");
    sendNotification(IOT_EVENT_BAD_TIME, logmsg, -1);
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

  logd("Door should close: %s. Current time: %d. Configured closed time interval: [%d - %d]", (shouldClose ? "yes" : "no"), hhmm, keepClosedFrom, keepClosedTo);
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
    logd("Door close try: %d.", attempt);
    int doorState = getDoorState();
    if(DOOR_CLOSED != doorState) {
      activateDoor();
    }
    // give it time to close, and check
    // if door hasn't closed, activating again will open the door.
    logd("Waiting %d ms for door to move to closed position.", AppConfig.DoorClosingTimeMs);
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
  logd("Door state: %d", doorState);

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
    logd("Too soon to try and close door again. ");
    return;
  }

  if(!AppConfig.EnableControl) {
    logd("Door control is disabled.");
    char buff[24];
    const char* logmsg = log("Door has been opened for %s", formatMillis(buff, (millis() - doorOpenedSinceMs)));
    sendNotification(IOT_EVENT_CONTROL_DISABLED, logmsg, -1);
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
  const char* logmsg = log("Door state: %s. Next attempt in %d minutes.",
                    getNamedDoorState(doorState),
                    (int) AppConfig.TimeBetweenClosingAttemptsMs / 1000 / 60);
  sendNotification(IOT_EVENT_CLOSING_FAILURE, logmsg, -1);
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
  delay(100);

  log("\nSetting up...");

  setupIO();
  ensureWiFi();

  updateConfig(true);

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

    // Blink red/blue LED based on WiFi state
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

    lastLoopRun = now;
  }

  yield();
}
