//#ifndef HW_CONFIG_H
//#define HW_CONFIG_H

#define DEV_BOARD_OLED
#define OLED_SSD1306
#define WIFI_NO_ETHERNET

#ifdef DEV_BOARD_OLED
  #define I2C_SDA 5
  #define I2C_SCL 4
  #ifndef WIFI_NO_ETHERNET
    #define WIFI_NO_ETHERNET
  #endif
  #ifndef OLED_SSD1306
    #define OLED_SSD1306
  #endif
#else
  #define I2C_SDA 4
  #define I2C_SCL 5
#endif

// ----------------------------------------------------------------------------------------------------------------------------------------------

#define HIGH_LEVEL_PRESSURE      1800000L
#define LOW_LEVEL_PRESSURE       -240000L
// ______________ PCF DIGITAL INPUTS ___________________________
#define TANK_UPPER_LIMIT_SWITCH    1    // NO Switch to Gnd, i.e. 0 when FULL  - when 0, disable Well Pump
#define TANK_UPPER_MID_SWITCH      6    // NO Switch to Gnd, i.e. 0 when HIGH  - when 1, enable Well Pump
#define TANK_LOWER_MID_SWITCH      7    // NO Switch to Gnd, i.e. 1 when LOW   - when 0, enable Irrigation (Gras & Drip Pumps & Valves)
#define TANK_LOWER_LIMIT_SWITCH    2    // NO Switch to Gnd, i.e. 1 when EMPTY - when 1, disable Irrigation (Gras & Drip Pumps and close both Main Valves)
#define BUTTON_FILLING             3    // Manually start/stop filling (Well Pump) for ${FILLING_MAX_MINUTES} minutes
#define BUTTON_GRASS               4    // Manually start/stop grass system (PUMP_GRASS + MAIN_VALVE_GRASS + GRASS_ZONE_x) for ${GRASS_MAX_MINUTES} minutes
#define BUTTON_DRIP                5    // Manually start/stop drip  system (PUMP_DRIP  + MAIN_VALVE_DRIP  + DRIP_ZONE_x ) for ${DRIP_MAX_MINUTES}  minutes
#define BUTTON_MASK  ((1 << (BUTTON_DRIP-1)) | (1 << (BUTTON_FILLING-1)) | (1 << (BUTTON_GRASS-1)))

// ______________ PCF DIGITAL OUTPUTS ___________________________
// Pumps
#define PUMP_WELL           1        // IBO well pump              - filling the tank
#define PUMP_GRASS          2        // Gardena 4100 ground pump   - grass irrigation
#define PUMP_DRIP           3        // Jecod DCS 1200 30W pump    - dripping irrigation

// Dripping Magentic Valves
#define DRIP_ZONE_1         4
#define DRIP_ZONE_2         5
#define DRIP_ZONE_3         6

// Grass Magentic Valves
#define GRASS_ZONE_1       13
#define GRASS_ZONE_2       14
#define GRASS_ZONE_3       12
#define GRASS_ZONE_4        9
#define GRASS_ZONE_5       11
#define GRASS_ZONE_6       10

// Main Water Supply Valves
#define MAIN_VALVE_DRIP    15        // Main valve of drip system  - from Jecod DCS 1200
#define MAIN_VALVE_GRASS   16        // Main valve of grass system - from Gardena 4100

//#endif  //HW_CONFIG_H