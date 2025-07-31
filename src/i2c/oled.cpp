#include "i2c/oled.h"

#ifdef OLED_SSD1306
  Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
  #define OLED_WHITE SSD1306_WHITE
#else
  Adafruit_SH1106G oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
  #define OLED_WHITE SH110X_WHITE
#endif

void init_oled() {
  Serial.print("Starting OLED ... ");
#ifdef OLED_SSD1306
  if(!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, true)) {
    Serial.println(F("SSD1306 init on 0x3C failed. Program Halted!"));
    while(true);
  }
#else
  if(!oled.begin(0x3C, true)) {
    Serial.println(F("SH1106 init on 0x3C failed. Program Halted!"));
    while(true);
  }
#endif  
  Serial.println("OLED started.");

  oled.clearDisplay();
  oled.setRotation(0);
  oled.setTextSize(1);
  oled.setTextColor(OLED_WHITE);
  oled.display();
}

void oled_clear_line(uint8_t line, uint8_t size) {
  oled.setTextSize(size);
  oled.fillRect(0, 8*line, SCREEN_WIDTH, 8*size, 0);
}

void oled_show(uint8_t line, String text, uint8_t size) {
  oled_clear_line(line, size);
  oled.setCursor(0, 8*line);oled.println(text);oled.display();
}

void test_oled() {
  oled.setCursor(0, 0);
  oled.println(F("Line 012345678901234567890"));
  oled.display();
  Serial.println("Line 1");
  delay(1200);

  oled.setCursor(0, 8);
  oled.setTextSize(2);
  oled.println(F("Line 2"));
  oled.display();
  Serial.println("Line 2");
  delay(200);

  oled.setCursor(0, 24);
  oled.setTextSize(3);
  oled.println(F("Line 3"));
  oled.display();
  Serial.println("Line 3");
}
