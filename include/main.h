#include "i2c/hw_config.h"

#define FILLING_MAX_MINUTES           40
#define GRASS_MAX_MINUTES             20
#define DRIP_MAX_MINUTES             120
#define LEAKAGE_DETECTOR_TRESHOLD      3      // When filling is started >3 times without any irrigation start, disable filling and log system alert (malfunction)

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
#define PUMP_GRASS_OUTPUT_NUMBER       2        // Gardena 4100 gound pump    - grass irrigation
#define PUMP_DRIP_OUTPUT_NUMBER        3        // Jecod DCS 1200 30W pump    - dripping irrigation
#define MAIN_VALVE_GRASS               4        // Main valve of grass system - from Gardena 4100
#define MAIN_VALVE_DRIP                5        // Main valve of drip system  - from Jecod DCS 1200

#define GRASS_ZONE_1       9
#define GRASS_ZONE_2      10
#define GRASS_ZONE_3      11
#define GRASS_ZONE_4      12
#define GRASS_ZONE_5      13
#define DRIP_ZONE_1       14
#define DRIP_ZONE_2       15
#define DRIP_ZONE_3       16


// ____________________________________________________________________________________________

void ScanInputs();
void setPumpWell(bool value);   // pump in the well
void setPumpGrass(bool value);  // pump of grass system
void setPumpDrip(bool value);   // pump of drip system

void setOutput(uint8_t output_number, bool value);
bool getOutput(uint8_t output_number);
bool getInput(uint8_t input_number);
void handleButtons(uint8_t filtered);

// extern bool WT32_ETH01_eth_connected;
// extern void WT32_ETH01_onEvent();
// extern void WT32_ETH01_waitForConnect();
//extern bool WT32_ETH01_isConnected();
