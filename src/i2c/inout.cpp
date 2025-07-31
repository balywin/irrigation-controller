#include "inout.h"

#ifdef Adafruit
  Adafruit_PCF8574 pcf8574_R1;
  Adafruit_PCF8574 pcf8574_R2;
  Adafruit_PCF8574 pcf8574_I1;
  Adafruit_PCF8574 pcf8574_I2;
#else
  // Set relay output i2c address
  PCF8574 pcf8574_R1(0x24, I2C_SDA, I2C_SCL);
  PCF8574 pcf8574_R2(0x25, I2C_SDA, I2C_SCL);

  // Set digital input i2c address
  PCF8574 pcf8574_I1(0x22, I2C_SDA, I2C_SCL);
  PCF8574 pcf8574_I2(0x21, I2C_SDA, I2C_SCL);
#endif

#ifdef Adafruit
void init_pcf(Adafruit_PCF8574 *pcf, TwoWire *wire, uint8_t i2c_address, uint8_t mode, uint8_t value = HIGH) {
  String s;
  Serial.print("Init PCF8574 in mode ");
  Serial.print(mode);
  s = pcf->begin(i2c_address, wire) ? "OK" : "Error!";
  Serial.println(": " + s);
  for (uint8_t i = 0; i <= 7; i++) {
    pcf->pinMode(i, mode);
    pcf->digitalWrite(i, value);
  }
}

uint8_t init_pcfs() {
  init_pcf(&pcf8574_R1, &Wire, 0x24, OUTPUT, HIGH);
  init_pcf(&pcf8574_R2, &Wire, 0x25, OUTPUT, HIGH);
  init_pcf(&pcf8574_I1, &Wire, 0x22, INPUT);
  init_pcf(&pcf8574_I2, &Wire, 0x21, INPUT);
}
#else
bool config_pcf(PCF8574 *pcf, uint8_t mode, uint8_t value = HIGH) {
  Serial.print("PCF8574 init... ");
  for (uint8_t i = 0; i <= 7; i++) {
    pcf->pinMode(i, mode, value);
  }
  bool res = pcf->begin();
  Serial.println(res ? "OK" : "Error!");
  return res;
}

uint8_t init_pcfs() {
  uint8_t res = 0;
  res |= (!config_pcf(&pcf8574_R1, OUTPUT, HIGH) << 0);
  res |= (!config_pcf(&pcf8574_R2, OUTPUT, HIGH) << 1);
  res |= (!config_pcf(&pcf8574_I1, INPUT) << 2);
  res |= (!config_pcf(&pcf8574_I2, INPUT) << 3);
  return res;
}
#endif

#define LONG_DELAY_MS 300
#define SHORT_DELAY_MS 50
//#define SLOW_PCF_TEST
void test_pcf() {
  // debug cycle
#ifdef SLOW_PCF_TEST
  delay(1000);
  #ifdef Adafruit
    Serial.println("R2 all 0...");
    pcf8574_R2.digitalWriteByte(0x00);delay(LONG_DELAY_MS);
    pcf8574_R2.digitalWriteByte(0x55);delay(LONG_DELAY_MS);
    pcf8574_R2.digitalWriteByte(0xAA);delay(LONG_DELAY_MS);
    Serial.println("R2 all 1...");
    pcf8574_R2.digitalWriteByte(0xFF);delay(SHORT_DELAY_MS);
    Serial.println("R1 all 0...");
    pcf8574_R1.digitalWriteByte(0x00);delay(LONG_DELAY_MS);
    pcf8574_R1.digitalWriteByte(0x55);delay(LONG_DELAY_MS);
    pcf8574_R1.digitalWriteByte(0xAA);delay(LONG_DELAY_MS);
    Serial.println("R1 all 1...");
    pcf8574_R1.digitalWriteByte(0xFF);delay(SHORT_DELAY_MS);
  #else
    Serial.println("R2 all 0...");
    pcf8574_R2.digitalWriteAll({0,0,0,0,0,0,0,0});delay(LONG_DELAY_MS);
    pcf8574_R2.digitalWriteAll({0,1,0,1,0,1,0,1});delay(LONG_DELAY_MS);
    pcf8574_R2.digitalWriteAll({1,0,1,0,1,0,1,0});delay(LONG_DELAY_MS);
    Serial.println("R2 all 1...");
    pcf8574_R2.digitalWriteAll({1,1,1,1,1,1,1,1});delay(LONG_DELAY_MS);
    Serial.println("R1 all 0...");
    pcf8574_R1.digitalWriteAll({0,0,0,0,0,0,0,0});delay(LONG_DELAY_MS);
    pcf8574_R1.digitalWriteAll({0,1,0,1,0,1,0,1});delay(LONG_DELAY_MS);
    pcf8574_R1.digitalWriteAll({1,0,1,0,1,0,1,0});delay(LONG_DELAY_MS);
    Serial.println("R1 all 1...");
    pcf8574_R1.digitalWriteAll({1,1,1,1,1,1,1,1});delay(LONG_DELAY_MS);
  #endif  
#endif
  Serial.print("Rolling R2...");
  for (int8_t i = 7; i >= 0; i--) {
    pcf8574_R2.digitalWrite(i, LOW);
    Serial.print(i);Serial.print(" ");
    //Serial.print("LOW ");
    delay(SHORT_DELAY_MS);
    pcf8574_R2.digitalWrite(i, HIGH);
    //Serial.print("HIGH ");
  }
  Serial.println();
  Serial.print("Rolling R1...");
  for (int8_t i = 7; i >= 0; i--) {
    pcf8574_R1.digitalWrite(i, LOW);
    Serial.print(i);Serial.print(" ");
    //Serial.print("LOW ");
    delay(SHORT_DELAY_MS);
    pcf8574_R1.digitalWrite(i, HIGH);
    //Serial.print("HIGH ");
  }
  Serial.println();
}