#include "i2c/inout.h"

// Set relay output i2c address
PCF8574 pcf8574_R1(0x24, I2C_SDA, I2C_SCL);
PCF8574 pcf8574_R2(0x25, I2C_SDA, I2C_SCL);

// Set digital input i2c address
PCF8574 pcf8574_I1(0x22, I2C_SDA, I2C_SCL);
PCF8574 pcf8574_I2(0x21, I2C_SDA, I2C_SCL);

void slow_pcf_test();

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
  res |= (!config_pcf(&pcf8574_I1, INPUT_PULLUP) << 2);
  res |= (!config_pcf(&pcf8574_I2, INPUT_PULLUP) << 3);
  return res;
}

#define LONG_DELAY_MS 300
#define SHORT_DELAY_MS 50
//#define SLOW_PCF_TEST
void test_pcf() {
  // debug cycle

  slow_pcf_test();

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

// Call this function periodically with the current port state (e.g., every 5ms)
uint16_t filter_inputs(uint16_t raw_input, FilterState *state) {
  for (uint8_t i = 0; i < 16; i++) {
    bool new_bit = (raw_input >> i) & 1;
    bool last_bit = (state->last_state >> i) & 1;

    if (new_bit == last_bit) {
      state->counter[i] = millis();  // reset counter since value matches output
    } else {
      if (millis() - state->counter[i] >= state->threshold[i]) {
        // Accept the new value after <threshold> consecutive reads
        if (new_bit)
          state->last_state |= (1 << i);
        else
          state->last_state &= ~(1 << i);
        state->counter[i] = millis();  // reset counter after accepting the new value
      }
    }
  }
  return state->last_state;
}

void slow_pcf_test() {
#ifdef SLOW_PCF_TEST
  delay(1000);
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
}