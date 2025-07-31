#include <Wire.h>
#include "config.h"
//#define Adafruit

#ifdef Adafruit
    #include <Adafruit_PCF8574.h>

    // Set relay output i2c address
    extern Adafruit_PCF8574 pcf8574_R1;
    extern Adafruit_PCF8574 pcf8574_R2;

    // Set digital input i2c address
    extern Adafruit_PCF8574 pcf8574_I1;
    extern Adafruit_PCF8574 pcf8574_I2;
#else
    #include <PCF8574.h>
    // Set relay output i2c address
    extern PCF8574 pcf8574_R1;
    extern PCF8574 pcf8574_R2;

    // Set digital input i2c address
    extern PCF8574 pcf8574_I1;
    extern PCF8574 pcf8574_I2;
#endif

uint8_t init_pcfs();
void test_pcf();

