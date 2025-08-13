#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "hw_config.h"

// Define OLED_SSD1306 to use SSD1306, SH1106 is default
#ifdef OLED_SSD1306
  #include <Adafruit_SSD1306.h>
  #define Oled_Class Adafruit_SSD1306
  #define OLED_WHITE SSD1306_WHITE
#else
  #include <Adafruit_SH110X.h>
  #define Oled_Class Adafruit_SH1106G
  #define OLED_WHITE SH110X_WHITE
#endif

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

extern Oled_Class oled;

void init_oled();
void oled_show(uint8_t line, String text, uint8_t size = 1);
void oled_clear_line(uint8_t line, uint8_t size = 1);
void test_oled();