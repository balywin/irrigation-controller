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


#define HIGH_LEVEL_PRESSURE      1800000L
#define LOW_LEVEL_PRESSURE       -240000L
// ______________ PCF DIGITAL INPUTS ___________________________
#define TANK_UPPER_SWITCH_INPUT_NUMBER 1        // NO Switch to Gnd, i.e. 0 when FULL  - to disable Well Pump
#define TANK_LOWER_SWITCH_INPUT_NUMBER 2        // NO Switch to Gnd, i.e. 1 when EMPTY - to disable Gras & Drip Pumps and close both Main Valves
#define BUTTON_FILLING    3        // Manually start/stop filling (Well Pump) for ${FILLING_MAX_MINUTES} minutes
#define BUTTON_GRASS      4        // Manually start/stop grass system (PUMP_GRASS + MAIN_VALVE_GRASS + GRASS_ZONE_x) for ${GRASS_MAX_MINUTES} minutes
#define BUTTON_DRIP       5        // Manually start/stop drip  system (PUMP_DRIP  + MAIN_VALVE_DRIP  + DRIP_ZONE_x ) for ${DRIP_MAX_MINUTES}  minutes
#define BUTTON_MASK       ((1 << (BUTTON_DRIP-1)) | (1 << (BUTTON_FILLING-1)) | (1 << (BUTTON_GRASS-1)))

// ______________ PCF DIGITAL OUTPUTS ___________________________
#define PUMP_WELL_OUTPUT_NUMBER        1        // IBO well pump              - filling the tank
#define PUMP_GRASS_OUTPUT_NUMBER       2        // Gardena 4100 ground pump   - grass irrigation
#define PUMP_DRIP_OUTPUT_NUMBER        3        // Jecod DCS 1200 30W pump    - dripping irrigation
#define MAIN_VALVE_GRASS              16        // Main valve of grass system - from Gardena 4100
#define MAIN_VALVE_DRIP               15        // Main valve of drip system  - from Jecod DCS 1200

#define GRASS_ZONE_1      13
#define GRASS_ZONE_2      14
#define GRASS_ZONE_3      12
#define GRASS_ZONE_4       9
#define GRASS_ZONE_5      11
#define GRASS_ZONE_6      10
#define DRIP_ZONE_1        4
#define DRIP_ZONE_2        5
#define DRIP_ZONE_3        6

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
void checkConnection();
void applyConfig();

// extern bool WT32_ETH01_eth_connected;
// extern void WT32_ETH01_onEvent();
// extern void WT32_ETH01_waitForConnect();
//extern bool WT32_ETH01_isConnected();
