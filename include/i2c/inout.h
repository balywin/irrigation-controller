#include <Arduino.h>
#include <Wire.h>
#include <PCF8574.h>

#include "i2c/hw_config.h"

// Set relay output i2c address
extern PCF8574 pcf8574_R1;
extern PCF8574 pcf8574_R2;

// Set digital input i2c address
extern PCF8574 pcf8574_I1;
extern PCF8574 pcf8574_I2;

// State for each bit: 0-255 counters (max 255), and the filtered output
typedef struct {
    uint32_t counter[8];     // Counters for each bit
    uint32_t threshold[8];    // debounce treshold value for each bit. Set it on init.
    uint8_t  last_state;     // Last known filtered state
} FilterState;

uint8_t filter_inputs(uint8_t raw_input, FilterState *state);
uint8_t init_pcfs();
void test_pcf();

