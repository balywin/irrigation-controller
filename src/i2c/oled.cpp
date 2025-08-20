#include "i2c/oled.h"

Oled_Class oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

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
  oled_clear_from(line, size);
}

void oled_clear_keep_last(uint8_t line, uint8_t size, uint8_t last) {
  oled.fillRect(0, 8 * line, 6 * size * (SCREEN_WIDTH / 6 - last), 8 * size, 0);
}

void oled_clear_from(uint8_t line, uint8_t size, uint8_t from, uint8_t len) {
  oled.setTextSize(size);
  uint16_t width = 6 * size * len;
  if (width > SCREEN_WIDTH - 6 * size * from) width = SCREEN_WIDTH - 6 * size * from;
  oled.fillRect(6 * size * from, 8 * line, width, 8 * size, 0);
}

void oled_show(uint8_t line, String text, uint8_t size, bool clear) {
  if (clear) oled_clear_line(line, size);
  oled_show_at(0, line, text, size, false);
}

void oled_show_at(uint8_t pos, uint8_t line, String text, uint8_t size, bool clear) {
  oled.setTextSize(size);
  if (clear)
    oled_clear_from(line, size, pos, text.length());
  oled.setCursor(6 * size * pos, 8 * line);
  oled.println(text);
  oled.display();
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
