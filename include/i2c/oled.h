#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "i2c/hw_config.h"

// Define OLED_SSD1306 to use SSD1306, SH1106 is default

#ifdef OLED_SSD1306
  #include <Adafruit_SSD1306.h>
#else
  #include <Adafruit_SH110X.h>
#endif

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#ifdef OLED_SSD1306
  extern Adafruit_SSD1306 oled;
#else
  extern Adafruit_SH1106G oled;
#endif

void init_oled();
void oled_show(uint8_t line, String text, uint8_t size = 1);
void oled_clear_line(uint8_t line, uint8_t size = 1);
void test_oled();