//#define DEV_BOARD_OLED
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

#ifdef WIFI_NO_ETHERNET
  // Replace with your network credentials
  #define AP_SSID "VivacarM"
  #define AP_PASSWORD "@Titi14#Papazov22%"
#endif
