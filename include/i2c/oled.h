#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
extern Adafruit_SH1106G oled;

void init_oled();
void oled_show(uint8_t line, String text, uint8_t size = 1);
void oled_clear_line(uint8_t line, uint8_t size = 1);
void test_oled();