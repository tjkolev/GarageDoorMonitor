#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <main.h>

HTTPClient httpClient;
WiFiClient wifiClient;

#define EVENT_TYPE_INFO     "Info"
#define EVENT_TYPE_WARN     "Warning"
#define EVENT_TYPE_CRITICAL "Critical"

#define MSG_TYPE_LEN      12
#define MSG_SUBJECT_LEN   80
#define MSG_MESSAGE_LEN   400
struct NotifyMessage {
  char Type[MSG_TYPE_LEN];
  char Subject[MSG_SUBJECT_LEN];
  char Message[MSG_MESSAGE_LEN];
} EventMessage;
#define EVENT_MSG_JSON_SIZE (JSON_OBJECT_SIZE(3))

size_t SerializeMessageBody(const NotifyMessage& msgBody, char* json, size_t maxSize) {
    StaticJsonBuffer<EVENT_MSG_JSON_SIZE> jsonBuffer;
    JsonObject& jsonDoc = jsonBuffer.createObject();
    jsonDoc["type"] = msgBody.Type;
    jsonDoc["subject"] = msgBody.Subject;
    jsonDoc["message"] = msgBody.Message;
    return jsonDoc.printTo(json, maxSize);
}

NotifyMessage& createEventMessage(int eventId, const char* msg = NULL, int msgLen = 0) {

  strcpy(EventMessage.Type, EVENT_TYPE_INFO);

  switch(eventId) {
    case IOT_EVENT_AUTO_CLOSING_DOOR:
      strcpy(EventMessage.Subject, "Garage door auto closing");
      strcpy(EventMessage.Message, "The garage door was found open when it should have been closed.\n");
      break;
    case IOT_EVENT_BAD_DATA:
      strcpy(EventMessage.Subject, "Garage door getting bad data");
      strcpy(EventMessage.Message, "See message below from monitor.\n");
      break;
    case IOT_EVENT_BAD_TIME:
      strcpy(EventMessage.Subject, "Garage door lost time");
      strcpy(EventMessage.Message, "The current time can not be reliably determined. Functions that rely on correct current time will be suspended.\n");
      break;
    case IOT_EVENT_CLOSING_FAILURE:
      strcpy(EventMessage.Type, EVENT_TYPE_WARN);
      strcpy(EventMessage.Subject, "Garage door closing failure'");
      strcpy(EventMessage.Message, "There was an error trying to close the garage door.\n");
      break;
    case IOT_EVENT_CLOSED_DOOR:
      strcpy(EventMessage.Type, EVENT_TYPE_CRITICAL);
      strcpy(EventMessage.Subject, "Garage door is now closed");
      strcpy(EventMessage.Message, "The garage door is now closed as expected.\n");
      break;
    case IOT_EVENT_RESET:
      strcpy(EventMessage.Subject, "Garage door monitor reset");
      strcpy(EventMessage.Message, "The garage door monitor has been reset. This could be due to power cycle, or code crash.\n");
      break;
    case IOT_EVENT_CONFIG_ERROR:
      strcpy(EventMessage.Subject, "Failure parsing configuration");
      strcpy(EventMessage.Message, "Failed to parse the configuration data retrieved from the server.\n");
      break;
    case IOT_EVENT_CONTROL_DISABLED:
      strcpy(EventMessage.Subject, "Door control is disabled");
      strcpy(EventMessage.Message, "Door would have closed by now, but control has been disabled.\n");
      break;

    default:
      strcpy(EventMessage.Subject, "Unknown event");
      strcpy(EventMessage.Message, "Message for an unknown event: ");
      sprintf(EventMessage.Message + strlen(EventMessage.Message), "%d\n", eventId);
      break;
  }

  if(msgLen == -1) {
    msgLen = strlen(msg);
  }
  if(msgLen > 0) {
    strncat(EventMessage.Message, msg, msgLen);
  }
  return EventMessage;
}

unsigned long lastNotifyTime = 0;
int lastNotifiedEventId = IOT_EVENT_NONE;

#define NOTIFY_URL          IOT_API_BASE_URL "/notify"
#define JSON_BUFFER_SIZE    1024

char jsonText[JSON_BUFFER_SIZE];
char msgBuffer[MSG_MESSAGE_LEN];

bool sendNotification(int eventId, const char* msg, int msgLen) {

  unsigned long now = millis();
  if((eventId == lastNotifiedEventId) && (now - lastNotifyTime < AppConfig.MinNotifyPeriodMs)) {
    return true;
  }
  lastNotifyTime = now;
  lastNotifiedEventId = eventId;

  if(NULL != msg) {
    // make a copy of the msg, as it could be the log buffer, and log() will mess it up.
    strncpy(msgBuffer, msg, MSG_MESSAGE_LEN);
    msgBuffer[MSG_MESSAGE_LEN-1] = '\0';
  }
  else {
    msgBuffer[0] = '\0';
    msgLen = 0;
  }

  if(!ensureWiFi()) {
    log("Cannot notify: no wifi.");
    return false;
  }

  NotifyMessage& msgToSend = createEventMessage(eventId, msgBuffer, msgLen);
  size_t jsonSize = SerializeMessageBody(msgToSend, jsonText, JSON_BUFFER_SIZE);

  bool result = false;

  httpClient.begin(wifiClient, NOTIFY_URL);
  httpClient.setTimeout(10000);
  int code = httpClient.POST((const uint8_t*)jsonText, jsonSize);
  httpClient.end();

  if(code == 200){
    logd("Notification sent.\n%s", jsonText);
    result = true;
  }
  else {
    log("Failed to send notification, http code %d\n%s", code, jsonText);
  }

  return result;
}

#define LOG_URL    IOT_API_BASE_URL "/log?deviceid=" DEVICE_ID
void postLog(const char* logMsg) {

  // NOTE: do not call any functions that call log() themselves!
  if(!AppConfig.PostLog) {
    return;
  }

  if(!wifiConnected()) {
    // can't use log() calls here
    Serial.println("Cannot post log: no wifi.");
    return;
  }

  httpClient.begin(wifiClient, LOG_URL);
  httpClient.setTimeout(500);
  int code = httpClient.POST((const uint8_t*)logMsg, strlen(logMsg));
  httpClient.end();
  if(code != 200){
    Serial.printf("Posting log message failed: %s\n", jsonText);
  }
  else {
    if(AppConfig.DebugLog) {
      Serial.printf("Posted message to %s\n", LOG_URL);
    }
  }
}
