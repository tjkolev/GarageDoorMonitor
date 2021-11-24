#include <Arduino.h>
#include <TimeLib.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <main.h>

extern HTTPClient httpClient;
extern WiFiClient wifiClient;

ApplicationConfig AppConfig;

#define CONFIG_URL    IOT_API_BASE_URL "/config?deviceid=sump"

template <typename T>
bool updateValue(T newValue, T &currentValue, int multiplier = 1000) {
  if (newValue) {
    T cfgValue = newValue * multiplier;
    if(cfgValue != currentValue) {
      currentValue = cfgValue;
      return true;
    }
  }
  return false;
}

bool parseConfig(const char* json) {
  StaticJsonBuffer<1024> jsonBuffer;
  JsonObject& config = jsonBuffer.parseObject(json);
  if (!config.success()) {
    log("Failed to parse json:\n%s", json);
    return false;
  }

  bool updated = updateValue(config["MainLoopSec"].as<unsigned long>(), AppConfig.MainLoopMs);
  updated |= updateValue(config["UpdateConfigSec"].as<unsigned long>(), AppConfig.UpdateConfigMs);
  updated |= updateValue(config["MaxClosingTries"].as<int>(), AppConfig.MaxClosingTries, 1);
  updated |= updateValue(config["DoorClosingTimeSec"].as<unsigned long>(), AppConfig.DoorClosingTimeMs);
  updated |= updateValue(config["TimeBetweenClosingAttemptsMin"].as<unsigned long>(), AppConfig.TimeBetweenClosingAttemptsMs, 60 * 1000);
  updated |= updateValue(config["MaxClosingAttempts"].as<int>(), AppConfig.MaxClosingAttempts, 1);
  updated |= updateValue(config["DoorClosingSwitchPressMs"].as<unsigned long>(), AppConfig.DoorClosingSwitchPressMs, 1);
  updated |= updateValue(config["MaxDoorOpenMin"].as<unsigned long>(), AppConfig.MaxDoorOpenMs, 60 * 1000);
  updated |= updateValue(config["MinDoorOpenMin"].as<unsigned long>(), AppConfig.MinDoorOpenMs, 60 * 1000);
  updated |= updateValue(config["MinNotifyPeriodSec"].as<unsigned long>(), AppConfig.MinNotifyPeriodMs);

  if (config.containsKey("KeepClosedFromTo")) {
    updated |= updateValue(config["KeepClosedFromTo"].as<int>(), AppConfig.KeepClosedFromTo[0], 1);
    updated |= updateValue(config["KeepClosedFromTo"].as<int>(), AppConfig.KeepClosedFromTo[1], 1);
  }

  if (config.containsKey("PinRangeDoorOpen")) {
    updated |= updateValue(config["PinRangeDoorOpen"].as<int>(), AppConfig.SensorRangeValues[DOOR_OPEN][0], 1);
    updated |= updateValue(config["PinRangeDoorOpen"].as<int>(), AppConfig.SensorRangeValues[DOOR_OPEN][1], 1);
  }
  if (config.containsKey("PinRangeDoorClosed")) {
    updated |= updateValue(config["PinRangeDoorClosed"].as<int>(), AppConfig.SensorRangeValues[DOOR_CLOSED][0], 1);
    updated |= updateValue(config["PinRangeDoorClosed"].as<int>(), AppConfig.SensorRangeValues[DOOR_CLOSED][1], 1);
  }
  if (config.containsKey("PinRangeDoorAjar")) {
    updated |= updateValue(config["PinRangeDoorAjar"].as<int>(), AppConfig.SensorRangeValues[DOOR_AJAR][0], 1);
    updated |= updateValue(config["PinRangeDoorAjar"].as<int>(), AppConfig.SensorRangeValues[DOOR_AJAR][1], 1);
  }

  log("Pin range values.");
  for(int ds = DOOR_OPEN; ds < DOOR_STATE_COUNT; ds++) {
    log("Door state %d: %d - %d", ds, AppConfig.SensorRangeValues[ds][0], AppConfig.SensorRangeValues[ds][1]);
  }

  log("Configuration pulled from %s - %s", CONFIG_URL, (updated ? "updated:": "no changes."));
  if(updated) {
    log(json);
  }

  return updated;
}

const char* respHeaders[] = { "X-IoT-LocalTime" };

void SetTime() {
  String timeTxt = httpClient.header(respHeaders[0]);
  Serial.println(timeTxt);
  int year, month, date, hour, minute, second;
  sscanf(timeTxt.c_str(), "%4d%02d%02d%02d%02d%02d", &year, &month, &date, &hour, &minute, &second);
  //Serial.println(year);Serial.println(month);Serial.println(date);Serial.println(hour);Serial.println(minute);Serial.println(second);
  setTime(hour, minute, second, date, month, year);
  if(timeSet != timeStatus()) {
    Serial.println("Failed to set time from config.");
  }
}

unsigned long lastConfigUpdate = 0;
bool updateConfig() {
  unsigned long now = millis();
  if(now - lastConfigUpdate < AppConfig.UpdateConfigMs) {
    return false;
  }
  lastConfigUpdate = now;

  bool result = false;
  if(ensureWiFi()) {
    httpClient.setTimeout(10000);
    httpClient.begin(wifiClient, CONFIG_URL);
    httpClient.collectHeaders(respHeaders, 1);
    int code = httpClient.GET();
    if(code == 200) {
      SetTime();
      String body = httpClient.getString();
      result = parseConfig(body.c_str());
    }
    else {
      log("Cannot pull config. Http code %d", code);
    }
    httpClient.end();
  }
  else {
   log("Cannot pull config: no wifi.");
  }

  return result;
}
