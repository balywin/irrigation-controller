#include <Arduino.h>
#include <Wire.h>
#include <PCF8574.h>

#include "hw_config.h"

// Set relay output i2c address
extern PCF8574 pcf8574_R1;
extern PCF8574 pcf8574_R2;

// Set digital input i2c address
extern PCF8574 pcf8574_I1;
extern PCF8574 pcf8574_I2;

// State for each bit: 0-255 counters (max 255), and the filtered output
typedef struct {
  uint32_t counter[16];     // Counters for each bit
  uint32_t threshold[16];   // debounce threshold value for each bit. Set it on init.
  uint16_t last_state;     // Last known filtered state
} FilterState;

uint16_t filter_inputs(uint16_t raw_input, FilterState *state);
uint8_t init_pcfs();
void test_pcf();

