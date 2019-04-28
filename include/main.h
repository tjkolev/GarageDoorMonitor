#ifndef main_h
#define main_h

#include <sensitive.h>

#define DOOR_UNKNOWN (-1)
#define DOOR_OPEN 0
#define DOOR_CLOSED 1
#define DOOR_AJAR 2
#define DOOR_STATE_COUNT 3

#define IOT_EVENT_NONE 0
#define IOT_EVENT_AUTO_CLOSING_DOOR 1
#define IOT_EVENT_UNKNOWN_STATE 2
#define IOT_EVENT_BAD_DATA 3
#define IOT_EVENT_RESET 4
#define IOT_EVENT_CLOSING_FAILURE 5
#define IOT_EVENT_CLOSED_DOOR 6

#define IOT_API_BASE_URL "http://" ROUTER_IP "/cgi-bin/luci/iot-helper/api"

struct ConfigParams {
  unsigned long MainLoopMs = 15 * 1000;
  unsigned long UpdateConfigMs = 60 * 1000; // also keeps the clock.
  int CloseDoorSlowBeepCount = 4;
  int CloseDoorFastBeepCount = 12;
  int MaxClosingTries = 2;
  unsigned long DoorClosingTimeMs = 20 * 1000;
  int TimeBetweenClosingAttemptsMin = 5;
  int MaxClosingAttempts = 4;
  unsigned long DoorClosingSwitchPressMs = 500;
  unsigned long MaxDoorOpenMs = 6 * 60 * 60 * 1000; // 6 hours max for door to stay open
  unsigned long MinDoorOpenMs = 5 * 60 * 1000;      // 5 minutes at least to stay open, so it doesn't go closing right away

  int KeepClosedFromTo[2] = { 2200, 500 };

  int SensorRangeValues[DOOR_STATE_COUNT][2] = {
    { 900, 1024 },  // open switch on
    { 300, 700 },   // closed switch on
    { 0, 100 }       // ajar, no switch set
  };
};

#endif // main_h