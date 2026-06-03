#pragma once

#include <ArduinoJson.h>

#define MAX_SCHEDULES_PER_AREA 8
#define MAX_START_TIMES        8
#define NUM_SCHED_AREAS        3  // 0=grass, 1=drip, 2=filling

struct ParsedStartTime {
    uint8_t hour;
    uint8_t minute;
};

struct ParsedSchedule {
    bool     enabled;
    uint8_t  daysMask;       // bit 1=Mon .. bit 7=Sun
    uint8_t  startTimeCount;
    ParsedStartTime startTimes[MAX_START_TIMES];
    uint32_t durationMinutes;
    JsonVariant zonesRef;
};

struct ParsedArea {
    bool    enabled;
    uint8_t scheduleCount;
    ParsedSchedule schedules[MAX_SCHEDULES_PER_AREA];
};

struct ParsedScheduleCache {
    ParsedArea areas[NUM_SCHED_AREAS]; // 0=grass, 1=drip, 2=filling
};

void loadSchedule();
