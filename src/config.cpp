#include <Arduino.h>
#include <TimeLib.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <main.h>

extern HTTPClient httpClient;
extern WiFiClient wifiClient;

ApplicationConfig AppConfig;

#define CONFIG_URL    IOT_API_BASE_URL "/config?deviceid=" DEVICE_ID

template <typename T>
void updateValue(const JsonObject &jconfig, const char* key, T &currentValue, int multiplier) {
  if (jconfig.containsKey(key)) {
    currentValue = jconfig[key].as<T>() * multiplier;
  }
}

template <typename T>
void updateValue(const JsonObject &jconfig, const char* key, T &currentValue) {
  if (jconfig.containsKey(key)) {
    currentValue = jconfig[key].as<T>();
  }
}

template <typename T>
void checkAndSwapValues(T& valMin, T& valMax, const char* nameMin, const char* nameMax) {
  if(valMax < valMin) {
    const char* logmsg = log("%s value %lu is less than %s %lu. Values will be swapped", nameMax, valMax, nameMin, valMin);

    T t = valMax;
    valMax = valMin;
    valMin = t;
    sendNotification(IOT_EVENT_CONFIG_ERROR, logmsg, -1);
  }
}

void parseConfig(const char* json) {
  StaticJsonBuffer<1024> jsonBuffer;
  const JsonObject& config = jsonBuffer.parseObject(json);
  if (!config.success()) {
    const char* logmsg = log("Failed to parse json:\n%s", json);
    sendNotification(IOT_EVENT_CONFIG_ERROR, logmsg, -1);
    return;
  }

  updateValue(config, "EnableControl", AppConfig.EnableControl);
  updateValue(config, "MainLoopSec", AppConfig.MainLoopMs, 1000);
  updateValue(config, "UpdateConfigSec", AppConfig.UpdateConfigMs, 1000);
  updateValue(config, "MaxClosingTries", AppConfig.MaxClosingTries);
  updateValue(config, "DoorClosingTimeSec", AppConfig.DoorClosingTimeMs, 1000);
  updateValue(config, "TimeBetweenClosingAttemptsMin", AppConfig.TimeBetweenClosingAttemptsMs, 60 * 1000);
  updateValue(config, "DoorClosingSwitchPressMs", AppConfig.DoorClosingSwitchPressMs);
  updateValue(config, "MaxDoorOpenMin", AppConfig.MaxDoorOpenMs, 60 * 1000);
  updateValue(config, "MinDoorOpenMin", AppConfig.MinDoorOpenMs, 60 * 1000);
  updateValue(config, "MinNotifyPeriodSec", AppConfig.MinNotifyPeriodMs, 1000);
  updateValue(config, "DebounceReadCount", AppConfig.DebounceReadCount);
  updateValue(config, "DebounceReadPauseMs", AppConfig.DebounceReadPauseMs);
  updateValue(config, "DebugLog", AppConfig.DebugLog);
  updateValue(config, "PostLog", AppConfig.PostLog);

  checkAndSwapValues(AppConfig.MinDoorOpenMs, AppConfig.MaxDoorOpenMs, "MinDoorOpenMs", "MaxDoorOpenMs");

  if (config.containsKey("KeepClosedFromTo")) {
    JsonArray &ja = config["KeepClosedFromTo"].as<JsonArray>();
    AppConfig.KeepClosedFromTo[0] = ja[0].as<int>();
    AppConfig.KeepClosedFromTo[1] = ja[1].as<int>();
  }

  if (config.containsKey("PinRangeDoorOpen")) {
    JsonArray &ja = config["PinRangeDoorOpen"].as<JsonArray>();
    AppConfig.SensorRangeValues[DOOR_OPEN][0] = ja[0].as<int>();
    AppConfig.SensorRangeValues[DOOR_OPEN][1] = ja[1].as<int>();
  }
  checkAndSwapValues(AppConfig.SensorRangeValues[DOOR_OPEN][0], AppConfig.SensorRangeValues[DOOR_OPEN][1], "PinRangeDoorOpen-from", "PinRangeDoorOpen-to");

  if (config.containsKey("PinRangeDoorClosed")) {
    JsonArray &ja = config["PinRangeDoorClosed"].as<JsonArray>();
    AppConfig.SensorRangeValues[DOOR_CLOSED][0] = ja[0].as<int>();
    AppConfig.SensorRangeValues[DOOR_CLOSED][1] = ja[1].as<int>();
  }
  checkAndSwapValues(AppConfig.SensorRangeValues[DOOR_CLOSED][0], AppConfig.SensorRangeValues[DOOR_CLOSED][1], "PinRangeDoorClosed-from", "PinRangeDoorClosed-to");

  if (config.containsKey("PinRangeDoorAjar")) {
    JsonArray &ja = config["PinRangeDoorAjar"].as<JsonArray>();
    AppConfig.SensorRangeValues[DOOR_AJAR][0] = ja[0].as<int>();
    AppConfig.SensorRangeValues[DOOR_AJAR][1] = ja[1].as<int>();
  }
  checkAndSwapValues(AppConfig.SensorRangeValues[DOOR_AJAR][0], AppConfig.SensorRangeValues[DOOR_AJAR][1], "PinRangeDoorAjar-from", "PinRangeDoorAjar-to");

  logd("Configuration pulled from %s", CONFIG_URL);
  logd(json);

  logd("Pin range values.");
  for(int ds = DOOR_OPEN; ds < DOOR_STATE_COUNT; ds++) {
    logd("Door state %d: %d - %d", ds, AppConfig.SensorRangeValues[ds][0], AppConfig.SensorRangeValues[ds][1]);
  }

  formatMillis(AppConfig.txtMinOpenTime, AppConfig.MinDoorOpenMs);
  formatMillis(AppConfig.txtMaxOpenTime, AppConfig.MaxDoorOpenMs);
}

const char* respHeaders[] = { "X-IoT-LocalTime" };

void SetTime() {
  String timeTxt = httpClient.header(respHeaders[0]);
  int year, month, date, hour, minute, second;
  sscanf(timeTxt.c_str(), "%4d%02d%02d%02d%02d%02d", &year, &month, &date, &hour, &minute, &second);
  setTime(hour, minute, second, date, month, year);
  if(timeSet != timeStatus()) {
    const char* logmsg = log("Failed to set time from header.");
    sendNotification(IOT_EVENT_CONFIG_ERROR, logmsg, -1);
  }
}

unsigned long lastConfigUpdate = 0;
void updateConfig(bool force) {
  unsigned long now = millis();
  if(!force && (now - lastConfigUpdate < AppConfig.UpdateConfigMs)) {
    return;
  }
  lastConfigUpdate = now;

  if(ensureWiFi()) {
    httpClient.setTimeout(10000);
    httpClient.begin(wifiClient, CONFIG_URL);
    httpClient.collectHeaders(respHeaders, 1);
    int code = httpClient.GET();
    if(code == 200) {
      SetTime();
      String body = httpClient.getString();
      parseConfig(body.c_str());
    }
    else {
      log("Cannot pull config. Http code %d", code);
    }
    httpClient.end();
  }
  else {
   log("Cannot pull config: no wifi.");
  }

}
