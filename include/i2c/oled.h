#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "hw_config.h"

#ifdef LCD_GMG12864
  #include <U8g2lib.h>
  #define Oled_Class U8G2_ST7565_NHD_C12864_F_4W_SW_SPI
  #define OLED_WHITE 1
#else
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
#endif

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

extern Oled_Class oled;

void init_display();
void oled_show(uint8_t line, const String& text, uint8_t size = 1, bool clear = true);
void oled_show_at(uint8_t pos, uint8_t line, const String& text, uint8_t size = 1, bool clear = true);
void oled_clear_line(uint8_t line, uint8_t size = 1);
void oled_clear_from(uint8_t line, uint8_t size = 1, uint8_t from = 0, uint8_t len = SCREEN_WIDTH / 6);
void oled_clear_keep_last(uint8_t line, uint8_t size, uint8_t last);
void oled_setTextSize(uint8_t size);
void oled_fillRect(uint16_t x, uint16_t y, uint16_t w, uint8_t h, uint8_t col);
void oled_drawBitmap(uint8_t x, uint8_t y, const uint8_t *bitmap, uint8_t w, uint8_t h, uint8_t col);
void oled_send();
void test_oled();
