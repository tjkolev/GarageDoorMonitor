#include <Arduino.h>
#include <TimeLib.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <main.h>

#define GDOOR_PIN D1
#define BUZZER_PIN D2
#define LED_RED LED_BUILTIN // on the NodeMCU
#define LED_BLUE 2 // on the ESP

#define MSG_BUFF_LEN 200
uint8_t msg[MSG_BUFF_LEN];

ConfigParams configParams;
HTTPClient httpClient;
WiFiClient wifiClient;

void LedInfo_Connected() {
  digitalWrite(LED_BLUE, LOW); // Turn the LED on by making the voltage LOW
  delay(200);
  digitalWrite(LED_BLUE, HIGH); // Turn the LED off by making the voltage HIGH
}

void LedInfo_NotConnected() {
  digitalWrite(LED_RED, LOW); // Turn the LED on by making the voltage LOW
  delay(200);
  digitalWrite(LED_RED, HIGH); // Turn the LED off by making the voltage HIGH
}

bool checkWifi() {
  return WiFi.status() == WL_CONNECTED;
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

bool sendNotification(int eventId, uint8_t* msg = NULL, int msgLen = 0) {
  if(IOT_EVENT_NONE == eventId) {
    return true;
  }

  if(!checkWifi()) {
    Serial.println("Cannot notify: no wifi.");
    return false;
  }

  httpClient.setTimeout(10000);
  switch(eventId) {
    case IOT_EVENT_AUTO_CLOSING_DOOR:
      httpClient.begin(wifiClient, IOT_API_BASE_URL "/notify?eventid=gdoor_closing");
      break;
    case IOT_EVENT_CLOSING_FAILURE:
      httpClient.begin(wifiClient, IOT_API_BASE_URL "/notify?eventid=gdoor_err");
      break;
    case IOT_EVENT_BAD_DATA:
      httpClient.begin(wifiClient, IOT_API_BASE_URL "/notify?eventid=gdoor_baddata");
      break;
    case IOT_EVENT_CLOSED_DOOR:
      httpClient.begin(wifiClient, IOT_API_BASE_URL "/notify?eventid=gdoor_closed");
      break;
    case IOT_EVENT_RESET:
      httpClient.begin(wifiClient, IOT_API_BASE_URL "/notify?eventid=gdoor_reset");
      break;
    
    default:
      return false;
  }

  int code = httpClient.POST(msg, msgLen);
  if(code == 200){
    Serial.println("Notification sent.");
  }
  else {
    Serial.print("Failed to send notification. Http code ");Serial.println(code);
  }
  httpClient.end();

  return code == 200;
}


bool parseConfig(const char* json) {
  StaticJsonDocument<1024> jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, json);
  if (error) {
    Serial.println("Failed to parse json.");
    return false;
  }

  JsonObject jsonConfig = jsonDoc.as<JsonObject>();

  unsigned int pval;
  if ((pval = jsonConfig["MainLoopSec"])) configParams.MainLoopMs = pval * 1000;
  if ((pval = jsonConfig["UpdateConfigSec"])) configParams.UpdateConfigMs = pval * 1000;
  if ((pval = jsonConfig["CloseDoorSlowBeepCount"])) configParams.CloseDoorSlowBeepCount = pval;
  if ((pval = jsonConfig["CloseDoorFastBeepCount"])) configParams.CloseDoorFastBeepCount = pval;
  if ((pval = jsonConfig["MaxClosingTries"])) configParams.MaxClosingTries = pval;
  if ((pval = jsonConfig["DoorClosingTimeSec"])) configParams.DoorClosingTimeMs = pval * 1000;
  if ((pval = jsonConfig["TimeBetweenClosingAttemptsMin"])) configParams.TimeBetweenClosingAttemptsMin = pval;
  if ((pval = jsonConfig["MaxClosingAttempts"])) configParams.MaxClosingAttempts = pval;
  if ((pval = jsonConfig["DoorClosingSwitchPressMs"])) configParams.DoorClosingSwitchPressMs = pval;
  if ((pval = jsonConfig["MaxDoorOpenMin"])) configParams.MaxDoorOpenMs = (unsigned long) (pval * 60 * 1000);
  if ((pval = jsonConfig["MinDoorOpenMin"])) configParams.MinDoorOpenMs = (unsigned long) (pval * 60 * 1000);

  if (jsonConfig.containsKey("KeepClosedFromTo")) {
    configParams.KeepClosedFromTo[0] = jsonConfig["KeepClosedFromTo"][0];
    configParams.KeepClosedFromTo[1] = jsonConfig["KeepClosedFromTo"][1];
  }

  char lvlx[] = "Level_";
  Serial.println("Reading sensor level values.");
  for(int fl = DOOR_OPEN; fl < DOOR_STATE_COUNT; fl++) {
    lvlx[5] = (char) (fl + 48);
    Serial.println(lvlx);
    if (jsonConfig.containsKey(lvlx)) {
      configParams.SensorRangeValues[fl][0] = jsonConfig[lvlx][0];
      Serial.println(configParams.SensorRangeValues[fl][0]);
      configParams.SensorRangeValues[fl][1] = jsonConfig[lvlx][1];
      Serial.println(configParams.SensorRangeValues[fl][1]);
    }
  }

  Serial.println("Configuration updated from json.");
  return true;
}

const char* respHeaders[] = { "X-IoT-LocalTime" };
unsigned long nextUpdateConfigMs = 0;
void updateConfig() {
  if(millis() < nextUpdateConfigMs) {
    return;
  }

  bool configWasUpdated = false;
  if(checkWifi()) {
    Serial.println("Updating configuration");
    httpClient.setTimeout(10000);
    httpClient.begin(wifiClient, IOT_API_BASE_URL "/config?deviceid=gdoor");
    httpClient.collectHeaders(respHeaders, 1);
    int code = httpClient.GET();
    if(code == 200) {
      String timeTxt = httpClient.header(respHeaders[0]);
      Serial.println(timeTxt);
      int year, month, date, hour, minute, second;
      sscanf(timeTxt.c_str(), "%4d%02d%02d%02d%02d%02d", &year, &month, &date, &hour, &minute, &second);
      //Serial.println(year);Serial.println(month);Serial.println(date);Serial.println(hour);Serial.println(minute);Serial.println(second);
      setTime(hour, minute, second, date, month, year);
      if(timeSet != timeStatus()) {
        Serial.println("Failed to set time from config.");
      }

      String body = httpClient.getString();
      if(parseConfig(body.c_str())) {
        configWasUpdated = true;
      }
    }
    else {
      Serial.print("Cannot pull config. Http code ");Serial.println(code);
    }
    httpClient.end();
  }
  else {
    Serial.println("Cannot pull config: no wifi.");
  }

  nextUpdateConfigMs = millis() + (configWasUpdated ? configParams.UpdateConfigMs : 60 * 1000);
  Serial.print("Next config update: ");Serial.println(nextUpdateConfigMs / 1000);
}

int getDoorState() {
  int rawVal = analogRead(PIN_A0);
  Serial.print(millis()/1000);Serial.print(" pin_A0: ");Serial.println(rawVal);

  for(int ndx = 0; ndx < DOOR_STATE_COUNT; ndx++) {
    if(configParams.SensorRangeValues[ndx][0] <= rawVal && rawVal <= configParams.SensorRangeValues[ndx][1]) {
      return ndx;
    }
  }

  Serial.print("No sensor range match for: "); Serial.println(rawVal);

  // Notify
  int msgLen = snprintf((char*)msg, MSG_BUFF_LEN, "Sensor value %d falls in no valid range.", rawVal);
  sendNotification(IOT_EVENT_BAD_DATA, msg, msgLen);

  return DOOR_UNKNOWN;
}

unsigned long doorOpenedSinceMs = 0;
bool doorShouldBeClosed() {

  if(doorOpenedSinceMs != 0) {
    unsigned long doorOpenedForMs = millis() - doorOpenedSinceMs;
    
    // if configured *don't close* the door if not opened min amount of time
    if(configParams.MinDoorOpenMs > 0 && doorOpenedForMs < configParams.MinDoorOpenMs) {
      return false;
    }

    // if configured *close* the door if opened more than the max amount of time
    if(configParams.MaxDoorOpenMs > 0 && doorOpenedForMs > configParams.MaxDoorOpenMs) {
      return true;
    }
  }

  if(timeSet != timeStatus()) {
    if(0 == (millis()/1000/60/60) % 6) {
      // Notify
      int msgLen = snprintf((char*)msg, MSG_BUFF_LEN, "Time can not be reliably determined.");
      sendNotification(IOT_EVENT_BAD_DATA, msg, msgLen);
    }
    return false; // conservative choice
  }

  bool shouldClose = false;
  time_t t = now();
  int hhmm = hour(t) * 100 + minute(t);
  if(configParams.KeepClosedFromTo[0] > configParams.KeepClosedFromTo[1]) {
    shouldClose = configParams.KeepClosedFromTo[0] <= hhmm || hhmm <= configParams.KeepClosedFromTo[1];
  }
  else {
    shouldClose = configParams.KeepClosedFromTo[0] <= hhmm && hhmm <= configParams.KeepClosedFromTo[1];
  }

  return shouldClose;
}

void alertOn() {
  digitalWrite(LED_RED, LOW);
  digitalWrite(BUZZER_PIN, HIGH);
}

void alertOff() {
  digitalWrite(LED_RED, HIGH);
  digitalWrite(BUZZER_PIN, LOW);
}

void closeDoor() {
  for(int n = 0; n < configParams.CloseDoorSlowBeepCount; n++) {
    alertOn();
    delay(2000);
    alertOff();
    delay(1000);
  }
  for(int n = 0; n < configParams.CloseDoorFastBeepCount; n++) {
    alertOn();
    delay(200);
    alertOff();
    delay(100);
  }

  digitalWrite(GDOOR_PIN, HIGH);
  delay(configParams.DoorClosingSwitchPressMs);
  digitalWrite(GDOOR_PIN, LOW);
}

unsigned long nextCloseAttemptMs = 0;
int closeAttemptCount = 0;
void checkDoor() {
  int doorState = getDoorState();
  if(DOOR_AJAR == doorState) {
    // Take no action. Door most likely in transition.
    // Perhaps next time around it will be Open or Closed.
    return;
  }

  if(DOOR_CLOSED == doorState) {
    if(nextCloseAttemptMs != 0) {
      // door was found closed, when previously it had failed to.
      sendNotification(IOT_EVENT_CLOSED_DOOR);
    }
    nextCloseAttemptMs = 0;
    closeAttemptCount = 0;
    doorOpenedSinceMs = 0;
    return;
  }

  if(DOOR_OPEN == doorState && 0 == doorOpenedSinceMs) {
    // Door was just found open. Mark.
    doorOpenedSinceMs = millis();
    if(0 == doorOpenedSinceMs)
      doorOpenedSinceMs = 1;
    return;
  }

  if(!doorShouldBeClosed()) {
    nextCloseAttemptMs = 0;
    closeAttemptCount = 0;
    return;
  }
  
  if(nextCloseAttemptMs != 0 && millis() < nextCloseAttemptMs) {
    Serial.print("Too soon to try and close door again. ");Serial.println(nextCloseAttemptMs);
    return;
  }

  if(configParams.MaxClosingAttempts > 0 && closeAttemptCount >= configParams.MaxClosingAttempts) {
    Serial.print("Not going to try and close the door any more. Tried: ");Serial.println(closeAttemptCount);
    return;
  }

  // let's try to close the door
  closeAttemptCount++;
  sendNotification(IOT_EVENT_AUTO_CLOSING_DOOR);
  for(int attempt = 1; attempt <= configParams.MaxClosingTries; attempt++) {
    Serial.print("Door close try: ");Serial.println(attempt);
    closeDoor();
    // give it time to close, and check
    // if door hasn't closed, activating again will open the door.
    delay(configParams.DoorClosingTimeMs);
    doorState = getDoorState();
    if(DOOR_CLOSED == doorState) {
      Serial.println("Door was closed.");
      // notify
      sendNotification(IOT_EVENT_CLOSED_DOOR);
      nextCloseAttemptMs = 0;
      closeAttemptCount = 0;
      return;
    }
  }

  // door didn't close when it should've
  nextCloseAttemptMs = millis() + (unsigned long) (configParams.TimeBetweenClosingAttemptsMin * 60 * 1000);
  if(0 == nextCloseAttemptMs)
    nextCloseAttemptMs = 1; // don't want to mess up the 'flag' overload.
  Serial.print("Next close attempt: ");Serial.println(nextCloseAttemptMs);

  // notify
  if(configParams.MaxClosingAttempts > 0 && closeAttemptCount >= configParams.MaxClosingAttempts) {
    int msgLen = snprintf((char*)msg, MSG_BUFF_LEN, "Door state: %s. This was the last (%d) attempt to try and close the door.", 
                      getNamedDoorState(doorState), 
                      closeAttemptCount);
    sendNotification(IOT_EVENT_CLOSING_FAILURE, msg, msgLen);
  }
  else {
    int msgLen = snprintf((char*)msg, MSG_BUFF_LEN, "Door state: %s. Next attempt in %d minutes.", 
                      getNamedDoorState(doorState), 
                      configParams.TimeBetweenClosingAttemptsMin);
    sendNotification(IOT_EVENT_CLOSING_FAILURE, msg, msgLen);
  }

}

void setup() {
  // put your setup code here, to run once:
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, LOW); //Turn on
  digitalWrite(LED_BLUE, LOW);

  Serial.begin(115200);		 // Start the Serial communication to send messages to the computer
	delay(10);
	Serial.println('\n');

  pinMode(GDOOR_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  Serial.println("Setting up Wifi.");
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  IPAddress ip(NETWORK_IP);
  IPAddress gateway(NETWORK_GATEWAY);
  IPAddress subnet(NETWORK_SUBNET);
  WiFi.config(ip, gateway, subnet);
  WiFi.begin(WIFI_NETWORK, WIFI_PASSWORD);
  delay(60 * 1000);
  Serial.println("Setup done.");
  digitalWrite(LED_RED, HIGH); // Turn off
  digitalWrite(LED_BLUE, HIGH);
}

bool resetNotificationSent = false;
unsigned long nextLoopMs = 0;
void loop() {
  // put your main code here, to run repeatedly:
  if(millis() >= nextLoopMs)
  {
    checkWifi() ? LedInfo_Connected() : LedInfo_NotConnected();
    if(!resetNotificationSent) {
      resetNotificationSent = sendNotification(IOT_EVENT_RESET);
    }

    updateConfig();
    checkDoor();
    nextLoopMs = millis() + configParams.MainLoopMs;
  }

  yield();
}