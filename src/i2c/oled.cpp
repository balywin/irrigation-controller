#include "i2c/oled.h"

#ifdef LCD_GMG12864
  Oled_Class oled(
    U8G2_R0,
    16,  // SCL
    13,  // SI (MOSI)
    33,  // CS
    14,  // RS (DC)
    U8X8_PIN_NONE   // RSE (RESET)
  );
#else
Oled_Class oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif

void oled_fillRect(uint16_t x, uint16_t y, uint16_t w, uint8_t h, uint8_t col) {
#ifdef LCD_GMG12864
  oled.setDrawColor(col);  // 0 = background (black/off)
  oled.drawBox(x, y, w, h);
#else
  oled.fillRect(x, y, w, h, col);
#endif
}

void oled_drawBitmap(uint8_t x, uint8_t y, const uint8_t *bitmap, uint8_t w, uint8_t h, uint8_t col) {
#ifdef LCD_GMG12864
  oled.setDrawColor(col);
  oled.drawBitmap(x, y, (w + 7) / 8, h, bitmap);
#else
  oled.drawBitmap(x, y, bitmap, w, h, col);
#endif
}

#ifdef LCD_GMG12864
void init_lcd()
{
  oled.begin();
  Serial.println("LCD started.");
  oled.firstPage();
  oled.setFontPosTop();
  oled.setFont(u8g2_font_5x8_tf);
  oled.setDrawColor(1);
  oled.drawFrame(0, 0, 128, 64);
  oled_send();
  oled.setFontMode(1);
  delay(1000);
}
#else
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
  oled_setTextSize(1);
  oled.setTextColor(OLED_WHITE);
  oled_send();
}
#endif

void init_display()
{
#ifdef LCD_GMG12864
  init_lcd();
#else
  init_oled();
#endif
}

void oled_clear_line(uint8_t line, uint8_t size) {
  oled_clear_from(line, size);
}

void oled_clear_keep_last(uint8_t line, uint8_t size, uint8_t last) {
  oled_fillRect(0, 8 * line, 6 * size * (SCREEN_WIDTH / 6 - last), 8 * size, 0);
}

void oled_clear_from(uint8_t line, uint8_t size, uint8_t from, uint8_t len) {
  int16_t from_px = 6 * size * from;
  int16_t width = 6 * size * len;
  if (width > SCREEN_WIDTH - from_px) width = SCREEN_WIDTH - from_px;
  if (width + from_px >= SCREEN_WIDTH - 2) width = SCREEN_WIDTH - from_px;
  oled_fillRect(from_px, 8 * line, width, 8 * size, 0);
//  oled_setTextSize(size);
}

void oled_show(uint8_t line, const String& text, uint8_t size, bool clear) {
  if (clear) oled_clear_line(line, size);
  oled_show_at(0, line, text, size, false);
}

void oled_show_at(uint8_t pos, uint8_t line, const String& text, uint8_t size, bool clear) {
  if (clear)
    oled_clear_from(line, size, pos, text.length());
  oled_setTextSize(size);
#ifdef LCD_GMG12864
  oled.drawStr(6 * size * pos, 8 * line, text.c_str());
#else
  oled.setCursor(6 * size * pos, 8 * line);
  oled.setDrawColor(OLED_WHITE);
  oled.println(text);
  oled_send();
#endif
}

void oled_send()
{
#ifdef LCD_GMG12864
  oled.sendBuffer();
#else
  oled.display();
#endif
}

void oled_setTextSize(uint8_t size)
{
  oled.setDrawColor(OLED_WHITE);
#ifdef LCD_GMG12864
  switch (size) {
    case 3:
    case 2: oled.setFont(u8g2_font_VCR_OSD_mf); break;
    case 1:
    default: oled.setFont(u8g2_font_5x8_mf); break;
  }
#else
  oled.setTextSize(size);
#endif
}

void test_oled() {
  oled_setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("Line 012345678901234567890"));
  oled_send();
  Serial.println("Line 1");

  oled_setTextSize(2);
  oled.setCursor(0, 16);
  oled.println(F("Line 2"));
  oled_send();
  Serial.println("Line 2");
  delay(200);

  oled_setTextSize(1);
  oled.drawStrX2(0, 32, "Line 3x2");
  oled_send();
  Serial.println("Line 3");
}
