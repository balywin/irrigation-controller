#include <Arduino.h>
#include <NTP.h>
#include "hw_config.h"

#define FILLING_MAX_MINUTES           20
#define GRASS_MAX_MINUTES             30
#define DRIP_MAX_MINUTES             120
#define LEAKAGE_DETECTOR_THRESHOLD     3      // When filling is started >3 times without any irrigation start, disable filling and log system alert (malfunction)

#define LEVEL_FILTERING_SECONDS          12   // hold the filling 30 seconds on level down
#define BUTTON_FILTERING_MS              50   // hold the filling 60 seconds on level down
#define GRASS_PUMP_START_DELAY_SECONDS    8   // Open the Main Valve, then 8 seconds later start the pump


typedef struct IrrigationConfig {
    uint16_t fillingMaxMinutes = FILLING_MAX_MINUTES;  // Maximum filling time in minutes
    uint16_t grassMaxMinutes = GRASS_MAX_MINUTES;    // Maximum grass irrigation time in minutes
    uint16_t dripMaxMinutes = DRIP_MAX_MINUTES;      // Maximum drip irrigation time in minutes
    uint8_t leakageDetectorThreshold = LEAKAGE_DETECTOR_THRESHOLD; // Threshold for leakage detection
    uint8_t levelFilteringSeconds = LEVEL_FILTERING_SECONDS; // Seconds for level filtering
    uint8_t buttonFilteringMs = BUTTON_FILTERING_MS; // Milliseconds for button filtering
    uint8_t grassPumpStartDelaySeconds = GRASS_PUMP_START_DELAY_SECONDS; // Delay before starting grass pump after opening main valve
    int32_t highLevelPressure = HIGH_LEVEL_PRESSURE; // High level pressure threshold
    int32_t lowLevelPressure = LOW_LEVEL_PRESSURE;   // Low level pressure threshold
} IrrigationConfig;

// ____________________________________________________________________________________________

extern IrrigationConfig irrigationConfig;

void ScanInputs();
void setGrassMainValve(bool value);
void setDripMainValve(bool value);
void setPumpWell(bool value);   // pump in the well
void setPumpGrass(bool value);  // pump of grass system
void setPumpDrip(bool value);   // pump of drip system
bool getPumpWell();
bool getPumpGrass();
bool getPumpDrip();

void setOutput(uint8_t output_number, bool value);
bool getOutput(uint8_t output_number);
bool getInput(uint8_t input_number);
void handleButtons(uint8_t filtered);
void setup_NTP();
void adjustRtc(NTP *ntp);
void applyConfig();

