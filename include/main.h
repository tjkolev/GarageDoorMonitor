#ifndef main_h
#define main_h

#include <sensitive.h>

#define GDOOR_MONITOR_VERSION   "21.11.22.1"

#define DOOR_UNKNOWN (-1)
#define DOOR_OPEN 0
#define DOOR_CLOSED 1
#define DOOR_AJAR 2
#define DOOR_STATE_COUNT 3

#define IOT_EVENT_NONE 0
#define IOT_EVENT_AUTO_CLOSING_DOOR 1
#define IOT_EVENT_BAD_TIME 2
#define IOT_EVENT_BAD_DATA 3
#define IOT_EVENT_RESET 4
#define IOT_EVENT_CLOSING_FAILURE 5
#define IOT_EVENT_CLOSED_DOOR 6

#define IOT_API_BASE_URL "http://" IOT_SERVICE_FQDN "/cgi-bin/luci/iot-helper/api"

struct ApplicationConfig {
  unsigned long MainLoopMs = 5 * 1000; // 5 seconds.
  unsigned long UpdateConfigMs = 60 * 1000; // 1 minute. Also keeps the clock.
  int MaxClosingTries = 2;
  unsigned long DoorClosingTimeMs = 20 * 1000; // 20 seconds for the door to close
  unsigned long TimeBetweenClosingAttemptsMs = 15 * 60 * 1000; // 15 minutes
  int MaxClosingAttempts = 4;
  unsigned long DoorClosingSwitchPressMs = 500;
  unsigned long MaxDoorOpenMs = 6 * 60 * 60 * 1000; // 6 hours max for door to stay open
  unsigned long MinDoorOpenMs = 5 * 60 * 1000;      // 5 minutes at least to stay open, so it doesn't go closing right away
  unsigned long MinNotifyPeriodMs = 5 * 60 * 1000;  // 5 minutes
  int DebounceReadCount = 5;
  int DebounceReadPauseMs = 500;

  int KeepClosedFromTo[2] = { 2200, 500 };

  int SensorRangeValues[DOOR_STATE_COUNT][2] = {
    { 900, 1024 },  // open switch on
    { 300, 700 },   // closed switch on
    { 0, 100 }       // ajar, no switch set
  };
};

extern ApplicationConfig AppConfig;

#define TXT_BUFF_LEN 400
extern char textBuffer[TXT_BUFF_LEN];
void log(const char* format, ...);

bool ensureWiFi();
bool wifiConnected();
bool updateConfig();
void closingDoorAlarm();
bool sendNotification(int eventId, char* msg = NULL, int msgLen = 0);

#endif // main_h