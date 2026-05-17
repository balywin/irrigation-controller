#pragma once

#include <ArduinoJson.h>
#include <NTP.h>
#include "hw_config.h"

// Default configuration values
#define FILLING_MAX_MINUTES            9
#define GRASS_MAX_MINUTES             20      // test debug value
#define DRIP_MAX_MINUTES             120
#define LEAKAGE_DETECTOR_THRESHOLD     3      // When filling is started >3 times without any irrigation start, disable filling and log system alert (malfunction)

#define LEVEL_FILTERING_SECONDS          10   // hold the filling 10 seconds on level down
#define BUTTON_FILTERING_MS              50   // button debounce 50ms
#define GRASS_PUMP_START_DELAY_SECONDS    7   // Open the Main Valve, then 7 seconds later start the pump

#define MAX_NUMBER_OF_GRASS_ZONES         6   // Number of grass irrigation zones
#define MAX_NUMBER_OF_DRIP_ZONES         16   // Number of drip irrigation zones
#define MAX_ZONE_GROUPS                  16   // Max zone groups per area per run
#define MAX_ZONES_PER_GROUP               8   // Max zones within one group

// Internal logic timings
#define TIME_UPDATE_PERIOD_MS    (1000UL)
#define STATUS_SHOW_PERIOD_MS     (200UL)
#define INPUTS_SCAN_PERIOD_MS      (10UL)
#define PRESSURE_SCAN_PERIOD_MS   (200UL)

typedef struct ControllerConfig {
    uint16_t fillingMaxMinutes = FILLING_MAX_MINUTES;                       // Maximum filling time in minutes
    uint16_t grassMaxMinutes   = GRASS_MAX_MINUTES;                         // Maximum grass irrigation time in minutes
    uint16_t dripMaxMinutes    = DRIP_MAX_MINUTES;                          // Maximum drip irrigation time in minutes
    uint8_t leakageDetectorThreshold = LEAKAGE_DETECTOR_THRESHOLD;          // Threshold for leakage detection
    uint8_t levelFilteringSeconds = LEVEL_FILTERING_SECONDS;                // Seconds for level filtering
    uint8_t buttonFilteringMs     = BUTTON_FILTERING_MS;                    // Milliseconds for button filtering
    uint8_t grassPumpStartDelaySeconds = GRASS_PUMP_START_DELAY_SECONDS;    // Delay before starting grass pump after opening main valve
    int32_t highLevelPressure    = HIGH_LEVEL_PRESSURE;                     // High level pressure threshold
    int32_t lowLevelPressure     = LOW_LEVEL_PRESSURE;                      // Low level pressure threshold
    uint8_t numberOfGrassZones   = MAX_NUMBER_OF_GRASS_ZONES;               // Max. number of grass irrigation zones
    uint8_t numberOfDripZones    = MAX_NUMBER_OF_DRIP_ZONES;                // Max. number of drip irrigation zones
} ControllerConfig;

// Per-run zone-group configuration. Populated by WS start command or schedule
// engine. count==0 means legacy flat cycling (hardware-button fallback).
struct ZoneRunConfig {
    uint8_t  count;                                    // number of groups
    uint8_t  activeIdx;                                // currently-running group (0-based)
    uint32_t groupMs;                                  // ms allotted per group
    uint8_t  sizes[MAX_ZONE_GROUPS];                   // zones in each group
    uint8_t  zoneIds[MAX_ZONE_GROUPS][MAX_ZONES_PER_GROUP]; // 1-based zone IDs
    uint32_t groupElapsedMs[MAX_ZONE_GROUPS];          // accumulated ms per group (manual switches)
};

// ____________________________________________________________________________________________

extern ControllerConfig controllerConfig;
extern JsonDocument scheduleJson;
extern uint32_t fillingMaxMs;
extern uint32_t grassMaxMs;
extern uint32_t dripMaxMs;

#ifdef DEV_BOARD_OLED
#define setGrassMainValve(value)
#define setDripMainValve(value)
#define setPumpWell(value)
#define setPumpGrass(value)
#define setPumpDrip(value)

#define getGrassMainValve()         true
#define getDripMainValve()          true
#define getPumpWell()               true
#define getPumpGrass()              false
#define getPumpDrip()               false
#else
#define setGrassMainValve(value)    setOutput(MAIN_VALVE_GRASS, !(value))
#define setDripMainValve(value)     setOutput(MAIN_VALVE_DRIP, !(value))
#define setPumpWell(value)          setOutput(PUMP_WELL, !(value))
#define setPumpGrass(value)         setOutput(PUMP_GRASS, !(value))
#define setPumpDrip(value)          setOutput(PUMP_DRIP, !(value))

#define getGrassMainValve()         !getOutput(MAIN_VALVE_GRASS)
#define getDripMainValve()          !getOutput(MAIN_VALVE_DRIP)
#define getPumpWell()               !getOutput(PUMP_WELL)
#define getPumpGrass()              !getOutput(PUMP_GRASS)
#define getPumpDrip()               !getOutput(PUMP_DRIP)
#endif

void ScanPCFInputs();
void setOutput(uint8_t output_number, bool value);
bool getOutput(uint8_t output_number);
bool getInput(uint8_t input_number);
void handleButtons();
void handleLevelSwitches();
void setup_NTP();
void adjustRtc(NTP *ntp_v);
void applyConfig();
void checkForDefects();
void showDiagInfo();
void controlOutputs();
void changeGrassZone(int8_t step);
void startGrassIrrigation();
void stopGrassIrrigation();
void startDripIrrigation();
void stopDripIrrigation();
void startFilling();
void stopFilling();
bool isGrassIrrigating();
bool isDripIrrigating();
bool isFillingActive();
bool isFillingEnabled();
bool isDrainingDisabled();
int8_t getGrassZoneIndex();
uint32_t getPressureRawValue();
uint8_t getWaterLevelPercent();
bool getPumpWellActive();
bool getPumpGrassActive();
bool getPumpDripActive();
bool getGrassMainValveActive();
bool getDripMainValveActive();
uint32_t getGrassRemainingMs();
uint32_t getGrassGroupRemainingMs();
uint32_t getDripRemainingMs();
uint32_t getDripGroupRemainingMs();
uint32_t getFillingRemainingMs();
// Zone group management — called by WS protocol and schedule engine.
void setGrassZoneGroups(const ZoneRunConfig& cfg);
void setDripZoneGroups(const ZoneRunConfig& cfg);
const ZoneRunConfig& getGrassRunConfig();
const ZoneRunConfig& getDripRunConfig();
void switchGrassGroup(uint8_t newIdx);
void switchDripGroup(uint8_t newIdx);
// Wall-clock time from RTC (or NTP if no RTC). Returns false if time unknown.
// dow: 0=Sunday, 1=Monday … 6=Saturday (same as RTClib).
bool getRtcTime(uint8_t& hour, uint8_t& minute, uint8_t& dow);