#include "main.h"
#include "file_utils.h"
#include "hw_config.h"
#include "board_info.h"

#define DEBUG_ETHERNET_WEBSERVER_PORT       Serial
#define NTP_DBG_PORT                        Serial

// Debug Level from 0 to 4
#define _ETHERNET_WEBSERVER_LOGLEVEL_ 3
#define _NTP_LOGLEVEL_                3

#include <NTP.h>
#include <WiFiUdp.h>
#include <RTClib.h>
#include <HX710B.h>

#include "i2c/inout.h"
#include "i2c/oled.h"
#include "network.h"

RTC_DS3231 rtc;
WiFiUDP ntpUDP;
NTP ntp(ntpUDP);

HX710B pressureSensor(HX_SCK_PIN, HX_DAT_PIN);

IrrigationConfig irrigationConfig;

uint32_t fillingMaxMs;
uint32_t grassMaxMs;
uint32_t dripMaxMs;
uint32_t levelFilteringMsThreshold;
uint32_t buttonFilteringCounterThreshold;

// ------------- Time --------------------------------
unsigned long currentTime = millis();
// Previous times
unsigned long lastTimeShowTime    = 0;
unsigned long lastTimeShowLevel   = 0;
unsigned long lastTimeShowInputs  = 0;
unsigned long lastTimeInputsScanned = 0;
unsigned long lastTimeGrassIrrigationRequested = 0;
unsigned long lastTimeDripIrrigationRequested = 0;
uint32_t oldTusCnt;
uint32_t oldTlsCnt;
bool prevGrassIrrigationState = false;
uint32_t grassPumpStartTime;
uint32_t diag = NO_DEFECT;
uint32_t prevDiag = diag;

uint8_t pcf_init_code;
uint16_t previousFilteredState;
uint16_t iState = 0xFFFF;
uint16_t iFiltered;
// -------------- Pump related -----------------------
uint32_t pressureRaw = 0;
bool fillingEnabled = false;
bool prevFillingEnabled = false;
uint32_t leakageDetectorCounter = 0;
bool drainingDisabled = true;
bool fillingRequested = false;
bool grassIrrigationRequested = false;
bool dripIrrigationRequested = false;
bool level_1 = false;
bool level_2 = false;
bool level_3 = false;
bool level_4 = false;

#ifdef LEVEL_SIMULATOR
  uint8_t levelCounter = 0;
#endif

const unsigned char bidon[] PROGMEM = {
  0x7F, 0xFE, 0x3F, 0xFC, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
  0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
  0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0xFF, 0xFF
};

FilterState filterState = {0};
uint16_t lastButState = BUTTON_MASK;

bool timeSet = false;
bool rtcReady = false;
bool timeBlink = false;

JsonDocument config;
JsonDocument schedule;

uint8_t grass_zone_index;
uint8_t grassZones[MAX_NUMBER_OF_GRASS_ZONES];
uint8_t dripZones[MAX_NUMBER_OF_DRIP_ZONES];

void applyJsonConfig(const JsonDocument& doc) {
  irrigationConfig.numberOfGrassZones = doc["number_of_grass_zones"] | MAX_NUMBER_OF_GRASS_ZONES;
  irrigationConfig.numberOfDripZones = doc["number_of_drip_zones"] | MAX_NUMBER_OF_DRIP_ZONES;
  irrigationConfig.fillingMaxMinutes = doc["filling_max_minutes"] | FILLING_MAX_MINUTES;
  irrigationConfig.grassMaxMinutes   = doc["grass_max_minutes"]   | GRASS_MAX_MINUTES;
  irrigationConfig.dripMaxMinutes    = doc["drip_max_minutes"]    | DRIP_MAX_MINUTES;
  irrigationConfig.leakageDetectorThreshold = doc["leakage_detector_threshold"] | LEAKAGE_DETECTOR_THRESHOLD;
  irrigationConfig.levelFilteringSeconds = doc["level_filtering_seconds"] | LEVEL_FILTERING_SECONDS;
  irrigationConfig.buttonFilteringMs = doc["button_filtering_ms"] | BUTTON_FILTERING_MS;
  irrigationConfig.grassPumpStartDelaySeconds = doc["grass_pump_start_delay_seconds"] |  GRASS_PUMP_START_DELAY_SECONDS;
  irrigationConfig.highLevelPressure = doc["high_level_pressure"] | HIGH_LEVEL_PRESSURE;
  irrigationConfig.lowLevelPressure = doc["low_level_pressure"] | LOW_LEVEL_PRESSURE;
  applyConfig();
}

void applyConfig() {
  fillingMaxMs = irrigationConfig.fillingMaxMinutes * 60 * 1000UL; // 20 minutes
  grassMaxMs   = irrigationConfig.grassMaxMinutes   * 60 * 1000UL; // 30 minutes
  dripMaxMs    = irrigationConfig.dripMaxMinutes    * 60 * 1000UL;  // 120 minutes

  levelFilteringMsThreshold = (irrigationConfig.levelFilteringSeconds * 1000UL);
  filterState.threshold[TANK_UPPER_LIMIT2_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_UPPER_LIMIT1_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_UPPER_MID_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_LOWER_MID_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_LOWER_LIMIT_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[BUTTON_FILLING - 1] = irrigationConfig.buttonFilteringMs;
  filterState.threshold[BUTTON_GRASS - 1] = irrigationConfig.buttonFilteringMs;
  filterState.threshold[BUTTON_DRIP - 1] = irrigationConfig.buttonFilteringMs;

  if (irrigationConfig.numberOfGrassZones <= 6) {
    // Apply specific configuration for 6 or fewer grass zones
    grassZones[0] = GRASS_ZONE_1;
    grassZones[1] = GRASS_ZONE_2;
    grassZones[2] = GRASS_ZONE_3;
    grassZones[3] = GRASS_ZONE_4;
    grassZones[4] = GRASS_ZONE_5;
    grassZones[5] = GRASS_ZONE_6;
  }

  Serial.print("Level debounce seconds: ");Serial.println(irrigationConfig.levelFilteringSeconds);
  Serial.print("Button debounce ms: ");Serial.println(irrigationConfig.buttonFilteringMs);
}

void showPressure(uint8_t code, uint8_t line, uint8_t size) {
  char pressure[24];
  if ( code != HX710B_OK ) {
    sprintf(pressure, "err: %d", code);
    oled_show(line, pressure, size);
    oled_show(line, pressure, size);
    Serial.println("Error reading pressure");
  } else {
    sprintf(pressure, "%d ", ((long) pressureRaw) / 1024);
    oled_show(line, pressure, size);
    Serial.print("Pressure raw value: ");
    Serial.println((long) pressureRaw);
  }
}

void showTime() {
  char tm[12];
  char temp[24];
  char pcf_status = pcf_init_code ? 'E' : ' ';
  if (rtcReady) {
    sprintf(tm, "%02u:%02u:%02u %c", rtc.now().hour(), rtc.now().minute(), rtc.now().second(), pcf_status);
    sprintf(temp, "%.2f%cC     ", rtc.getTemperature(), (char) 0xF7);
    temp[11] = '\0';
  } else {
    sprintf(tm, "%02u:%02u:%02u %c", ntp.hours(), ntp.minutes(), ntp.seconds(), pcf_status);
    sprintf(temp, "--.-- %cC     ", (char) 0xF7);
    temp[11] = '\0';
  }
  oled_show_at(0, 0, temp);
  oled_show_at(11, 0, timeSet || timeBlink ? tm : "           ");
  timeBlink = !timeBlink;
}

void showStates() {
  char tm[14];
  char states[24];
  char pumpStates[14];
  int32_t irrigationRemainingMinutes = (millis() - lastTimeGrassIrrigationRequested) / 60000L;
  sprintf(states, "%04X %d%d %d%d G%02d L%d ", iState, fillingRequested, fillingEnabled, 
          grassIrrigationRequested, drainingDisabled, irrigationRemainingMinutes, leakageDetectorCounter);
  //Serial.println(states);
  oled_show(7, states, 1);

  uint32_t tusCnt = (millis() - filterState.counter[TANK_UPPER_LIMIT1_SWITCH - 1])/100;
  uint32_t tlsCnt = (millis() - filterState.counter[TANK_LOWER_LIMIT_SWITCH - 1])/100;
//  sprintf(pumpStates, "%c%c%c", getPumpWell() ? 'F' : ' ', getPumpGrass ? 'G' : ' ', getPumpDrip ? 'D' : ' ');
  sprintf(pumpStates, "%c  %s%c  ",
    fillingRequested ? 'F' : ' ',
    grassIrrigationRequested ? "G" + String(grass_zone_index) : "   ", 
    dripIrrigationRequested ? 'D' : ' ');
  if (tusCnt > 1) {
    if (tusCnt != oldTusCnt) {
      pumpStates[3] = 0;
      sprintf(states, "%s U%02u", pumpStates, tusCnt/10);
      oled_show_at(0, 4, states, 2);
    }
  } else if (tlsCnt != oldTlsCnt) {
    if (tlsCnt > 1) {
      pumpStates[3] = 0;
      sprintf(states, "%s L%02u", pumpStates, tlsCnt/10);
      oled_show_at(0, 4, states, 2);
    }
    oldTlsCnt = tlsCnt;
  } else {
    oled_show_at(0, 4, pumpStates, 2);
  }
  oldTusCnt = tusCnt;

  if (filterState.last_state != previousFilteredState) {
    previousFilteredState = filterState.last_state;
    sprintf(tm, "%02u:%02u:%02u - filtState: ", rtc.now().hour(), rtc.now().minute(), rtc.now().second());
    Serial.print(tm);
    Serial.println(previousFilteredState, HEX);
  }

  sprintf(states, "%u", level_4 ? 4 : level_3 ? 3 : level_2 ? 2 : level_1 ? 1 : 0);
  oled.fillRect(6 * 2 * 9, 8 * 3, 20, 8 * 3, 0); oled.drawBitmap(6 * 2 * 9, 8 * 3, bidon, 16, 24, OLED_WHITE);
  oled.setCursor(6 * 2 * 9 + 3, 8 * 3 + 5); oled.setTextSize(2); oled.println(states); oled.display();
  //Serial.print("Tank Level: "); Serial.println(states);

}

void printTestValues(const JsonDocument& doc) {
  // Read values
  const char* deviceName = doc["device_name"];
  int interval = doc["interval"];
  bool enabled = doc["enabled"];

  // Print values
  Serial.println("Config loaded:");
  Serial.printf("  - Device Name: %s\n", deviceName);
  Serial.printf("  - Number of Grass Zones: %d\n", irrigationConfig.numberOfGrassZones);
  Serial.printf("  - Enabled: %s\n", enabled ? "true" : "false");
}

void setup() {
  Serial.begin(115200);
  //-------------------------------------
  while (!Serial)
    delay(100);

  printBoardInfo();
  initFs();
  loadFile(config, "/app_config.json");
  applyJsonConfig(config);
  printTestValues(config);

  loadFile(schedule, "/schedule.json");
  // Set I2C pins
  Wire.setPins(I2C_SDA, I2C_SCL);

  if (rtc.begin()) {
    rtcReady = true;
    timeSet = !rtc.lostPower();
  } else {
    Serial.println(" *** Error initializing RTC ***");
    Serial.flush();
  }
  // Init OLED
  Serial.println("Init OLED...");
  init_oled();
//  test_oled();
//#ifndef DEV_BOARD_OLED 
  // Init PCFs
  pcf_init_code = init_pcfs();
  if (pcf_init_code) diag |= PCF_INIT_FAILED;
  String s = "Init PCFs... " + (pcf_init_code == 0 ? "OK" : "Error " + String(pcf_init_code, HEX));
  Serial.println(s);oled_show(0, s);
  uint8_t code = pressureSensor.init();
  s = "Init H710B... " + (code == HX710B_OK ? "OK" : "Error " + String(code, HEX));
  Serial.println(s);oled_show(0, s);

  test_pcf();
//#endif

  networkInit();
  oled_show(1, "Network started.");

  applyConfig();
} 

void closeGrassValves() {
  for (uint8_t i = 0; i < irrigationConfig.numberOfGrassZones; i++) {
    setOutput(grassZones[i], true);
  }
}

void closeDripValves() {
  for (uint8_t i = 0; i < irrigationConfig.numberOfDripZones; i++) {
    setOutput(dripZones[i], true);
  }
}

void loop() {
  if (checkConnection()) {       // If just got connected
    setup_NTP();  // This also updates the time
    Serial.println("NTP setup complete.");
    if (ntp.epoch() > (24 * 60 * 60)) {
      adjustRtc(&ntp);
    }
  }
  if (getNetworkIsConnected() && ntp.update()) {
    Serial.print("Time synced: " + String(ntp.formattedTime("%T")) + " , ");
    adjustRtc(&ntp);
    ntp.updateInterval(60 * 60 * 1000);     // initially on 1m, after the time is set update the interval to 1h
  }
  currentTime = millis();
  if ((currentTime - lastTimeShowTime) >= (TIME_UPDATE_PERIOD_MS/(2-uint8_t(timeSet)))) {
    showTime();
    lastTimeShowTime = currentTime;

    if (grassIrrigationRequested) {
      if ((currentTime - lastTimeGrassIrrigationRequested) >= grassMaxMs) {
        grassIrrigationRequested = false;
        closeGrassValves();
        Serial.println("Grass irrigation completed in " + String(grassMaxMs / 60000UL) + " minutes");
      } else {
        grass_zone_index = 1 + (currentTime - lastTimeGrassIrrigationRequested) * (irrigationConfig.numberOfGrassZones-1) / grassMaxMs;
        for (uint8_t i = 1; i < irrigationConfig.numberOfGrassZones; i++) {
          setOutput(grassZones[i], i != grass_zone_index);
        }
      }
    }

    if ((dripIrrigationRequested && ((currentTime - lastTimeDripIrrigationRequested) >= dripMaxMs))) {
      dripIrrigationRequested = false;
      closeDripValves();
      Serial.println("Drip irrigation completed in " + String(dripMaxMs / 60000UL) + " minutes");
    }
    #ifdef LEVEL_SIMULATOR
      levelCounter = (++levelCounter % 5);
      level_1 = (levelCounter == 1);
      level_2 = (levelCounter == 2);
      level_3 = (levelCounter == 3);
      level_4 = (levelCounter == 4);
      lastTimeInputsScanned = currentTime;
    #endif
  }
  currentTime = millis();
  if ((currentTime - lastTimeInputsScanned) >= INPUTS_SCAN_PERIOD_MS) {
    ScanPCFInputs();
    lastTimeInputsScanned = currentTime;
  }
  // Pressure sensor
  // currentTime = millis();
  // if ((currentTime - lastTimeShowLevel) >= INPUTS_UPDATE_PERIOD_MS) {
  //   uint8_t code = pressureSensor.read(&pressureRaw, 500UL);
  //   showPressure(code, 4, 2);
  //   lastTimeShowLevel = currentTime;
  // }

  // if ((long)pressureRaw > HIGH_LEVEL_PRESSURE) {
  //   fillingEnabled = false;
  // }
  currentTime = millis();
  if ((currentTime - lastTimeShowInputs) >= (STATUS_SHOW_PERIOD_MS)) {
    showStates();
    showDiagInfo();
    lastTimeShowInputs = currentTime;
  }
  controlOutputs();

  networkLoop();
}

void controlOutputs() {
  // Control the outputs based on the current state
  if (!pcf_init_code) {
    setPumpWell(fillingRequested && fillingEnabled);
  }
  bool grassIrrigationState = grassIrrigationRequested && !drainingDisabled;
  if (grassIrrigationState != prevGrassIrrigationState) {
    if (grassIrrigationState) {
      grassPumpStartTime = millis();
    }
    prevGrassIrrigationState = grassIrrigationState;
  }
  bool delayTimePassed = (millis() - grassPumpStartTime > GRASS_PUMP_START_DELAY_SECONDS * 1000UL);
  if (!pcf_init_code) {
    setPumpGrass(grassIrrigationState && delayTimePassed);
    setGrassMainValve(grassIrrigationState);
  }
}

void setOutput(uint8_t output_number, bool value) {
  // output_number as marked on the PCB - from 1 to 16
  if ((output_number < 1) || (output_number > 16))  
    return;
  if (output_number <= 8) {
    pcf8574_R1.digitalWrite(output_number - 1, value ? HIGH : LOW);
  } else {
    pcf8574_R2.digitalWrite(output_number - 9, value ? HIGH : LOW);
  }
}

bool getOutput(uint8_t output_number) {
  // output_number as marked on the PCB - from 1 to 16
  if ((output_number < 1) || (output_number > 16))  
    return false;
    
  if (output_number <= 8) 
    return pcf8574_R1.digitalRead(output_number - 1) ? true : false;
  else 
    return pcf8574_R2.digitalRead(output_number - 9) ? true : false;
}

bool getInput(uint8_t input_number) {
  // output_number as marked on the PCB - from 1 to 16
  if ((input_number < 1) || (input_number > 16))  
    return false;
    
  if (input_number <= 8) 
    return pcf8574_I1.digitalRead(input_number - 1, true) ? true : false;
  else 
    return pcf8574_I2.digitalRead(input_number - 9, true) ? true : false;
}

void setup_NTP() {
  ntp.ruleDST("EEST", Last, Sun, Mar, 2, 180); // last sunday in march 2:00, timezone +180min (+2 GMT + 1h summertime offset)
  ntp.ruleSTD("EET", Last, Sun, Oct, 3, 120);  // last sunday in october 3:00, timezone +120min (+2 GMT)
  ntp.begin();
}

void adjustRtc(NTP *ntp) {
    if (rtcReady) {
      Serial.print("Adjusting RTC ... ");
      rtc.adjust(DateTime(ntp->year(), ntp->month(), ntp->day(), ntp->hours(), ntp->minutes(), ntp->seconds()));
      Serial.println("done.");
    } else {
      Serial.println("RTC not ready to be adjusted.");
    }
//    rtc.adjust(ntp->epoch());
    timeSet = true;
}

void showDiagInfo() {
  char ds[24];
  if (diag != prevDiag) {
    sprintf(ds, "DIAG: 0x%04X", diag);
    oled_show(3, ds);
    Serial.print("Diagnostic flags: 0x"); Serial.println(diag, HEX);
    if (diag == NO_DEFECT) {
      Serial.println("No defects detected");
    }
    if (diag & L2_DEFECT) {
      Serial.println(" **** L2 defect detected ****. Irrigation disabled.");
    }
    if (diag & L3_DEFECT) {
      Serial.println(" **** L3 defect detected ****. Irrigation disabled.");
    }
    if (diag & L4_DEFECT) {
      Serial.println(" **** L4 defect detected ****. Filling disabled.");
    }
    if (diag & LEAK_DEFECT) {
      Serial.println(" **** Leakage detected ****. Automatic filling cancelled.");
    }
    prevDiag = diag;
  }
}

void checkForDefects() {
  if (level_2 && !level_1) {
    drainingDisabled = true;
    grassIrrigationRequested = false;
    dripIrrigationRequested = false;
    diag |= L2_DEFECT;
  }
  if (level_3 && (!level_2 || !level_1)) {
    drainingDisabled = true;
    grassIrrigationRequested = false;
    dripIrrigationRequested = false;
    diag |= L3_DEFECT;
  }
  if (level_4 && (!level_3 || !level_2 || !level_1)) {
    fillingEnabled = false;
    fillingRequested = false;
    diag |= L4_DEFECT;
  }
  if (fillingEnabled && (fillingEnabled != prevFillingEnabled)) {
    leakageDetectorCounter++;
    if (leakageDetectorCounter > LEAKAGE_DETECTOR_THRESHOLD) {
      fillingRequested = false;
      diag |= LEAK_DEFECT;
    }
  }
  prevFillingEnabled = fillingEnabled;
}

bool getFilteredInput(uint8_t inputNumber) {
  if ((inputNumber < 1) || (inputNumber > 16)) return false;
  return iFiltered & (1 << (inputNumber - 1));
}

void ScanPCFInputs()
{

  char line[24];
  if (!pcf_init_code) {
    iState = pcf8574_I1.digitalReadAll() | (pcf8574_I2.digitalReadAll() << 8);
  } 
  iFiltered = filter_inputs(iState, &filterState); 

  handleButtons();
  handleLevelSwitches();

  checkForDefects();
}

void handleLevelSwitches() {  
#ifndef LEVEL_SIMULATOR
  level_1 = !getFilteredInput(TANK_LOWER_LIMIT_SWITCH);
  level_2 = !getFilteredInput(TANK_LOWER_MID_SWITCH);
  level_3 = !getFilteredInput(TANK_UPPER_MID_SWITCH);
  level_4 = (!getFilteredInput(TANK_UPPER_LIMIT1_SWITCH)) || (!getFilteredInput(TANK_UPPER_LIMIT2_SWITCH));
#endif  

  if (!level_1) drainingDisabled = true;
  if (level_2) drainingDisabled = false;
  if (!level_3) fillingEnabled = true;
  if (level_4) fillingEnabled = false;
  //drainingDisabled = getFilteredInput(TANK_LOWER_LIMIT_SWITCH);
  //fillingEnabled   = getFilteredInput(TANK_UPPER_LIMIT_SWITCH);
}

void handleButtons() {  
  uint16_t butState = (iFiltered ^ 0xFFFF) & BUTTON_MASK;
  if (lastButState != butState) {
    Serial.print("Button change: 0x");Serial.println(butState, HEX);
    switch (butState) {
      case 0x8000:    // Filling button
        fillingRequested = !fillingRequested;
        if (!level_4) fillingEnabled = true;
        //i1FilterState.last_state |= 1 << (TANK_UPPER_LIMIT_SWITCH - 1);
        leakageDetectorCounter = 0;
        break;
      case 0x4000:    // Grass button
        grassIrrigationRequested = !grassIrrigationRequested;
        if (grassIrrigationRequested) {
          if (level_1) drainingDisabled = false;
          lastTimeGrassIrrigationRequested = millis();
          leakageDetectorCounter = 0;
        } else {
          closeGrassValves();
        }
        break;
      case 0x2000:    // Drip button
        dripIrrigationRequested = !dripIrrigationRequested;
        if (dripIrrigationRequested) {
          if (level_1) drainingDisabled = false;
          lastTimeDripIrrigationRequested = millis();
          leakageDetectorCounter = 0;
        } else {
          closeDripValves();
        }
        break;
      case 0xC000:    // Filling and Grass buttons together
        /* code */
        break;
      case 0x60:    // Grass and Drip buttons together
        /* code */
        break;
      
      default:
        break;
    }
    lastButState = butState;
  }
}
