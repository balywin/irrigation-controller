#include <Arduino.h>
#include <NTP.h>
#include "hw_config.h"

// Default configuration values
#define FILLING_MAX_MINUTES           15
#define GRASS_MAX_MINUTES             30      // test debug value
#define DRIP_MAX_MINUTES             120
#define LEAKAGE_DETECTOR_THRESHOLD     3      // When filling is started >3 times without any irrigation start, disable filling and log system alert (malfunction)

#define LEVEL_FILTERING_SECONDS          10   // hold the filling 10 seconds on level down
#define BUTTON_FILTERING_MS              50   // button debounce 50ms
#define GRASS_PUMP_START_DELAY_SECONDS    8   // Open the Main Valve, then 8 seconds later start the pump

#define MAX_NUMBER_OF_GRASS_ZONES        16   // Number of grass irrigation zones
#define MAX_NUMBER_OF_DRIP_ZONES         16   // Number of drip irrigation zones

// Internal logic timings
#define TIME_UPDATE_PERIOD_MS  1000UL
#define STATUS_SHOW_PERIOD_MS   200UL
#define INPUTS_SCAN_PERIOD_MS    10UL

typedef struct IrrigationConfig {
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
} IrrigationConfig;

// ____________________________________________________________________________________________

extern IrrigationConfig irrigationConfig;

#define setGrassMainValve(value)    setOutput(MAIN_VALVE_GRASS, !(value))
#define setDripMainValve(value)     setOutput(MAIN_VALVE_DRIP, !(value))
#define setPumpWell(value)          setOutput(PUMP_WELL, !(value))
#define setPumpGrass(value)         setOutput(PUMP_GRASS, !(value))
#define setPumpDrip(value)          setOutput(PUMP_DRIP, !(value))
#define getPumpWell()               !getOutput(PUMP_WELL)
#define getPumpGrass()              !getOutput(PUMP_GRASS)
#define getPumpDrip()               !getOutput(PUMP_DRIP)

void ScanPCFInputs();
void setOutput(uint8_t output_number, bool value);
bool getOutput(uint8_t output_number);
bool getInput(uint8_t input_number);
void handleButtons();
void handleLevelSwitches();
void setup_NTP();
void adjustRtc(NTP *ntp);
void applyConfig();
void checkForDefects();
void showDiagInfo();
void controlOutputs();