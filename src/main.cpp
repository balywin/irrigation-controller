#include "main.h"
#include "file_utils.h"
#include "hw_config.h"
#include "board_info.h"
#include "schedule_cache.h"

#define DEBUG_ETHERNET_WEBSERVER_PORT       Serial
#define NTP_DBG_PORT                        Serial

// Debug Level from 0 to 4
#define _ETHERNET_WEBSERVER_LOGLEVEL_ 3
#define _NTP_LOGLEVEL_                3

#include <NTP.h>
#ifdef WIFI_NO_ETHERNET
  #include <WiFi.h>
#endif
#include <WiFiUdp.h>
#include <RTClib.h>
#include <HX710AB.h>

#include "i2c/inout.h"
#include "i2c/oled.h"
#include "network.h"
#include "webserver_embedded.h"
#include "websocket_protocol.h"
#include "zones.h"
#include "areas.h"

RTC_DS3231 rtc;
WiFiUDP ntpUDP;
NTP ntp(ntpUDP);

HX710B pressureSensor(HX_DAT_PIN, HX_SCK_PIN);

ControllerConfig controllerConfig;

uint32_t fillingMaxMs;
uint32_t grassMaxMs;
uint32_t dripMaxMs;
uint32_t levelFilteringMsThreshold;
uint32_t macDisplayUntil = 0;

uint32_t buttonFilteringCounterThreshold;

// ------------- Time --------------------------------
unsigned long currentTime = millis();
// Previous times
unsigned long lastTimeShowTime    = 0;
unsigned long lastTimeShowLevel   = 0;
unsigned long lastTimeShowInputs  = 0;
unsigned long lastTimeInputsScanned = 0;
unsigned long lastTimeFillingRequested = 0;
unsigned long lastTimeGrassIrrigationRequested = 0;
unsigned long lastTimeDripIrrigationRequested = 0;
unsigned long lastTimeGrassZoneSwitched = 0;
unsigned long lastTimeDripZoneSwitched  = 0;
uint32_t oldTusCnt = 0;
uint32_t oldTlsCnt = 0;
bool prevGrassIrrigationState = false;
uint32_t grassPumpStartTime = 0;
bool prevDripIrrigationState = false;
uint32_t dripPumpStartTime = 0;
uint32_t diag = NO_DEFECT;
uint32_t prevDiag = diag;

uint8_t pcf_init_code;
uint16_t previousFilteredState;
uint16_t iState = 0xFFFF;
uint16_t iFiltered;
// -------------- Pump related -----------------------
int32_t pressureRaw = 0;
uint16_t ultrasonic = 0;
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
char prevLevel = '?';

#ifdef LEVEL_SIMULATOR
  uint8_t levelCounter = 0;
#endif

const unsigned char bidon[] PROGMEM = {
  0x7F, 0xFE, 0x3F, 0xFC, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
  0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
  0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0xFF, 0xFF
};

FilterState filterState = {0};
uint16_t lastButState = BUTTONS_MASK;

bool timeSet = false;
bool rtcReady = false;
bool timeBlink = false;

JsonDocument appConfigJson;
JsonDocument scheduleJson;
JsonDocument manualControlJson;

AreaConfig areas[2];  // Grass and Drip
uint8_t numAreas = 0;

uint8_t grassZones[MAX_NUMBER_OF_GRASS_ZONES];
uint8_t dripZones[MAX_NUMBER_OF_DRIP_ZONES];

void applyAppConfig(const JsonDocument& doc) {
  controllerConfig.fillingMaxMinutes        = doc["filling"]["max_minutes"]                | FILLING_MAX_MINUTES;
  controllerConfig.leakageDetectorThreshold = doc["filling"]["leakage_detector_threshold"] | LEAKAGE_DETECTOR_THRESHOLD;
  controllerConfig.levelFilteringSeconds    = doc["filling"]["level_filtering_seconds"]    | LEVEL_FILTERING_SECONDS;
  controllerConfig.highLevelPressure        = doc["filling"]["high_level_pressure"]        | HIGH_LEVEL_PRESSURE;
  controllerConfig.lowLevelPressure         = doc["filling"]["low_level_pressure"]         | LOW_LEVEL_PRESSURE;

  controllerConfig.grassMaxMinutes            = doc["grass_irrigation"]["max_minutes"]             | GRASS_MAX_MINUTES;
  controllerConfig.grassPumpStartDelaySeconds = doc["grass_irrigation"]["pump_start_delay_seconds"] | GRASS_PUMP_START_DELAY_SECONDS;
  controllerConfig.numberOfGrassZones         = doc["grass_irrigation"]["number_of_zones"]         | MAX_NUMBER_OF_GRASS_ZONES;

  controllerConfig.dripMaxMinutes    = doc["drip_irrigation"]["max_minutes"]     | DRIP_MAX_MINUTES;
  controllerConfig.numberOfDripZones = doc["drip_irrigation"]["number_of_zones"] | MAX_NUMBER_OF_DRIP_ZONES;

  controllerConfig.buttonFilteringMs = doc["button_filtering_ms"] | BUTTON_FILTERING_MS;

  fillingMaxMs = controllerConfig.fillingMaxMinutes * 60 * 1000UL; 
  grassMaxMs   = controllerConfig.grassMaxMinutes   * 60 * 1000UL; 
  dripMaxMs    = controllerConfig.dripMaxMinutes    * 60 * 1000UL; 

  levelFilteringMsThreshold = (controllerConfig.levelFilteringSeconds * 1000UL);
  filterState.threshold[TANK_UPPER_LIMIT2_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_UPPER_LIMIT1_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_UPPER_MID_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_LOWER_MID_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_LOWER_LIMIT_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[BUTTON_FILLING - 1] = controllerConfig.buttonFilteringMs;
  filterState.threshold[BUTTON_GRASS - 1] = controllerConfig.buttonFilteringMs;
  filterState.threshold[BUTTON_DRIP - 1] = controllerConfig.buttonFilteringMs;
  filterState.threshold[BUTTON_ZONE_SWITCH - 1] = controllerConfig.buttonFilteringMs;

  if (controllerConfig.numberOfGrassZones > 6) {
    Serial.println("Warning: number_of_grass_zones exceeds maximum supported 6. Limiting to 6.");
    controllerConfig.numberOfGrassZones = 6;
  }
    // Apply specific configuration for 6 or fewer grass zones
  grassZones[0] = GRASS_ZONE_1;
  grassZones[1] = GRASS_ZONE_2;
  grassZones[2] = GRASS_ZONE_3;
  grassZones[3] = GRASS_ZONE_4;
  grassZones[4] = GRASS_ZONE_5;
  grassZones[5] = GRASS_ZONE_6;

  dripZones[0] = DRIP_ZONE_1;
  dripZones[1] = DRIP_ZONE_2;
  dripZones[2] = DRIP_ZONE_3;
  // dripZones[3+] remain 0 until hw_config defines more drip zone outputs
}

void showPressure(uint8_t line, uint8_t size) {
  char pressure[24];
  sprintf(pressure, "%ld ", pressureRaw / 1024L);
  Serial.print("Pressure raw value: ");
  Serial.println(static_cast<long>(pressureRaw));
  oled_show(line, pressure, size);
}

void showUltrasonic(uint8_t line, uint8_t size) {
  char ultrasonicStr[24];
  sprintf(ultrasonicStr, "%u ", ultrasonic);
  Serial.print("uSonic mm: ");
  Serial.println(static_cast<long>(ultrasonic));
  oled_show(line, ultrasonicStr, size);
}

void showTime() {
  char tm[12];
  char temp[24];
  char pcf_status = pcf_init_code && timeBlink ? 'E' : ' ';
  if (rtcReady) {
    sprintf(tm, "%02u:%02u:%02u %c", rtc.now().hour(), rtc.now().minute(), rtc.now().second(), pcf_status);
    sprintf(temp, "%.1f%cC     ", rtc.getTemperature(), 0xF7);
  } else {
    sprintf(tm, "%02u:%02u:%02u %c", ntp.hours(), ntp.minutes(), ntp.seconds(), pcf_status);
    sprintf(temp, "--.-- %cC      ", 0xF7);
  }
  temp[11] = 0;
  oled_show_at(0, 0, temp);
  oled_show_at(11, 0, timeSet || timeBlink ? tm : "           ");

  if (getServerStartedEventTime() != 0) {
    if (millis() > getServerStartedEventTime() + 5000UL) {
      oled_clear_line(2);
      clearServerStartedEventTime();
    }
  } else {
    if (fillingRequested) {
      sprintf(temp, "%02lu:%02lu", getFillingRemainingMs() / 60000UL, (getFillingRemainingMs() / 1000UL) % 60);
      oled_show_at(0, 2, temp);
    } else oled_clear_from(2, 1, 0, 6);
    if (isGrassIrrigating()) {
      sprintf(temp, "%02lu:%02lu", getGrassRemainingMs()   / 60000UL, (getGrassRemainingMs()   / 1000UL) % 60);
      oled_show_at(6, 2, temp);
    } else oled_clear_from(2, 1, 6, 6);
    if (isDripIrrigating()) {
      sprintf(temp, "%02lu:%02lu", getDripRemainingMs()    / 60000UL, (getDripRemainingMs()    / 1000UL) % 60);
      oled_show_at(12, 2, temp);
    } else oled_clear_from(2, 1, 12, 6);
  }
  timeBlink = !timeBlink;
}

void showStates() {
  char tm[24];
  char states[24];
  char pumpStates[14];

#ifdef WIFI_NO_ETHERNET
  if (getNetworkIsConnected()) {
    sprintf(states, "%d", WiFi.RSSI());
    oled_show_at(16, 1, states);
  }
#endif
  if (millis() >= macDisplayUntil) {

    uint32_t irrigationRemainingMinutes = (millis() - lastTimeGrassIrrigationRequested) / 60000UL;
    sprintf(states, "%04X %d%d %d%d G%02d L%d ", iState, fillingRequested, fillingEnabled,
            grassIrrigationRequested, !drainingDisabled, irrigationRemainingMinutes, leakageDetectorCounter);
    //Serial.println(states);
    oled_show(7, states, 1);
  }

  String grassIndicator = "";
  if (grassIrrigationRequested) {
    grassIndicator = "G";
    const ZoneRunConfig& grassRun = Zones::getGrassRunConfig();
    if (grassRun.count > 0) {
      const uint8_t sz = grassRun.sizes[grassRun.activeIdx];
      for (uint8_t z = 0; z < sz && grassIndicator.length() < 4; z++) {
        grassIndicator += String(grassRun.zoneIds[grassRun.activeIdx][z]);
      }
    } else {
      grassIndicator += String(Zones::getGrassZoneIndex());
    }
  }
  // Fixed format: F + 1sp + 4-char grass (padded) + 1sp — 'D' always at col 6
  snprintf(pumpStates, sizeof(pumpStates), "%c %-4s",
           fillingRequested ? 'F' : ' ',
           grassIndicator.c_str());

  uint32_t tensMax = 1UL;
  uint8_t iMax = 0;
  for (uint8_t z = TANK_UPPER_LIMIT2_SWITCH; z <= TANK_LOWER_LIMIT_SWITCH; z++) {
    uint32_t tens = (millis() - filterState.counter[z])/100;
    if (tensMax < tens) {
      tensMax = tens;
      iMax = z;
    }
  }
  sprintf(states, "%02lu", tensMax/10UL);
  oled_show_at(19, 2, iMax != 0 ? states : "  ", 1);

  oled_show_at(0, 4, pumpStates, 2);
  oled_show_at(6, 4, dripIrrigationRequested ? "D" : " ", 2);

  if (filterState.last_state != previousFilteredState) {
    previousFilteredState = filterState.last_state;
    sprintf(tm, "%02u:%02u:%02u - filtState: ", rtc.now().hour(), rtc.now().minute(), rtc.now().second());
    Serial.print(tm);
    Serial.println(previousFilteredState, HEX);
  }

  char level = level_4 ? '4' : level_3 ? '3' : level_2 ? '2' : level_1 ? '1' : '0';
  oled.fillRect(6 * 2 * 9, 8 * 3, 20, 8 * 3, 0); oled.drawBitmap(6 * 2 * 9 + 1, 8 * 3, bidon, 16, 24, OLED_WHITE);
  oled.setCursor(6 * 2 * 9 + 4, 8 * 3 + 5); oled.setTextSize(2); oled.print(level); oled.display();
  if (prevLevel != level) {
    Serial.print("Tank Level: "); Serial.println(level);
    prevLevel = level;
  }
}

void printTestValues(const JsonDocument& doc) {
  // Read values
  const char* deviceName = doc["device_name"];
  const int interval = doc["interval"];
  const bool enabled = doc["enabled"];

  // Print values
  Serial.println("Config loaded:");
  Serial.printf("  - Device Name: %s\n", deviceName != nullptr ? deviceName : "Unknown");
  Serial.printf("  - Number of Grass Zones: %d\n", controllerConfig.numberOfGrassZones);
  Serial.printf("  - Enabled: %s\n", enabled ? "true" : "false");
}

void setup() {
  Serial.begin(115200);
  //-------------------------------------
  while (!Serial)
    delay(100);

  printBoardInfo();
  initFs();
  
  Serial2.begin(9600, SERIAL_8N1, 2, 15);

  // Load App (Controller) Configuration
  loadJsonFile(appConfigJson, "/config/app_config.json");
  applyAppConfig(appConfigJson);
  printTestValues(appConfigJson);
  Serial.printf("  - Filling max minutes: %d\n", controllerConfig.fillingMaxMinutes);

  // Load areas configuration
  if (Areas::loadAreasConfig(appConfigJson, areas, 2, numAreas)) {
    Serial.print("Loaded ");
    Serial.print(numAreas);
    Serial.println(" areas from app config");
  } else {
    Serial.println("Failed to load areas config");
  }

  // Load manual control configuration
  loadJsonFile(manualControlJson, "/config/manual_control.json");

  // Load schedule configuration
  loadSchedule();

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
  // Init PCFs
  pcf_init_code = init_pcfs();
#ifndef DEV_BOARD_OLED 
  if (pcf_init_code) diag |= PCF_INIT_FAILED;
#endif
  String s = "PCFs... " + (pcf_init_code == 0 ? "OK" : "Error " + String(pcf_init_code, HEX));
  Serial.println(s);oled_show(0, s);
  pressureSensor.begin();
  //s = "Init H710B... " + (code == HX710B_OK ? "OK" : "Error " + String(code, HEX));
  //Serial.println(s);oled_show(0, s);

  test_pcf();

  networkInit();
  {
    oled_show(1, "Network started.");
    String macStr = getNetworkMacAddress();
    oled_show(7, macStr.c_str());
    macDisplayUntil = millis() + 8000UL;
  }

  Zones::init(controllerConfig.numberOfGrassZones, controllerConfig.numberOfDripZones);
} 

void closeGrassValves() {
  for (uint8_t i = 0; i < controllerConfig.numberOfGrassZones; i++) {
    setOutput(grassZones[i], true);
  }
}

void closeDripValves() {
  for (uint8_t i = 0; i < controllerConfig.numberOfDripZones; i++) {
    setOutput(dripZones[i], true);
  }
}

void switchGrassGroup(uint8_t newIdx) {
  ZoneRunConfig grassRun = Zones::getGrassRunConfig();
  if (newIdx >= grassRun.count) return;
  uint32_t elapsed = millis() - lastTimeGrassZoneSwitched;
  if (elapsed > grassRun.groupMs) elapsed = grassRun.groupMs;
  grassRun.groupElapsedMs[grassRun.activeIdx] += elapsed;
  grassRun.activeIdx = newIdx;
  lastTimeGrassZoneSwitched = millis() - grassRun.groupElapsedMs[newIdx];
  Zones::setGrassZoneGroups(grassRun);
  Zones::applyGrassGroup(newIdx);
}

void switchDripGroup(uint8_t newIdx) {
  ZoneRunConfig dripRun = Zones::getDripRunConfig();
  if (newIdx >= dripRun.count) return;
  uint32_t elapsed = millis() - lastTimeDripZoneSwitched;
  if (elapsed > dripRun.groupMs) elapsed = dripRun.groupMs;
  dripRun.groupElapsedMs[dripRun.activeIdx] += elapsed;
  dripRun.activeIdx = newIdx;
  lastTimeDripZoneSwitched = millis() - dripRun.groupElapsedMs[newIdx];
  Zones::setDripZoneGroups(dripRun);
  Zones::applyDripGroup(newIdx);
}

void startGrassIrrigation() {
  grassIrrigationRequested = true;
  if (level_1) drainingDisabled = false;
  lastTimeGrassIrrigationRequested = millis();
  lastTimeGrassZoneSwitched = millis();
  Zones::setGrassZoneIndex(0);
  const ZoneRunConfig& grassRun = Zones::getGrassRunConfig();
  if (grassRun.count > 0) Zones::applyGrassGroup(0);
}

void stopGrassIrrigation() {
  grassIrrigationRequested = false;
  closeGrassValves();
  Zones::setGrassZoneGroups({});
}

void startDripIrrigation() {
  dripIrrigationRequested = true;
  if (level_1) drainingDisabled = false;
  lastTimeDripIrrigationRequested = millis();
  lastTimeDripZoneSwitched = millis();
  leakageDetectorCounter = 0;
  const ZoneRunConfig& dripRun = Zones::getDripRunConfig();
  if (dripRun.count > 0) Zones::applyDripGroup(0);
}

void stopDripIrrigation() {
  dripIrrigationRequested = false;
  closeDripValves();
  Zones::setDripZoneGroups({});
}


bool isGrassIrrigating() { return grassIrrigationRequested; }
bool isDripIrrigating()  { return dripIrrigationRequested; }

void startFilling() {
  if (level_4) {
    fillingEnabled = false;
    return;
  }
  fillingEnabled = true;
  fillingRequested = true;
  lastTimeFillingRequested = millis();
  leakageDetectorCounter = 0;
}

void stopFilling() {
  fillingRequested = false;
}

bool isFillingActive() { return fillingRequested; }
bool isFillingEnabled() { return fillingEnabled; }
bool isDrainingDisabled() { return drainingDisabled; }
int8_t getGrassZoneIndex() { return Zones::getGrassZoneIndex(); }
uint32_t getPressureRawValue() { return pressureRaw; }

// Remaining-time accessors (ms). Return 0 when not running.
uint32_t getGrassRemainingMs() {
  if (!grassIrrigationRequested) return 0;
  const ZoneRunConfig& grassRun = Zones::getGrassRunConfig();
  if (grassRun.count > 0 && grassRun.groupMs > 0) {
    uint32_t totalMs = (uint32_t)grassRun.count * grassRun.groupMs;
    uint32_t elapsed = millis() - lastTimeGrassIrrigationRequested;
    return elapsed >= totalMs ? 0 : totalMs - elapsed;
  }
  if (grassMaxMs == 0) return 0;
  uint32_t elapsed = millis() - lastTimeGrassIrrigationRequested;
  return elapsed >= grassMaxMs ? 0 : grassMaxMs - elapsed;
}
uint32_t getGrassGroupRemainingMs() {
  if (!grassIrrigationRequested) return 0;
  const ZoneRunConfig& grassRun = Zones::getGrassRunConfig();
  if (grassRun.count > 0 && grassRun.groupMs > 0) {
    uint32_t elapsed = millis() - lastTimeGrassZoneSwitched;
    return elapsed >= grassRun.groupMs ? 0 : grassRun.groupMs - elapsed;
  }
  if (grassMaxMs == 0 || controllerConfig.numberOfGrassZones <= 1) return 0;
  uint32_t perZoneMs = grassMaxMs / (controllerConfig.numberOfGrassZones - 1);
  uint32_t elapsed = millis() - lastTimeGrassZoneSwitched;
  return elapsed >= perZoneMs ? 0 : perZoneMs - elapsed;
}
uint32_t getDripRemainingMs() {
  if (!dripIrrigationRequested) return 0;
  const ZoneRunConfig& dripRun = Zones::getDripRunConfig();
  if (dripRun.count > 0 && dripRun.groupMs > 0) {
    uint32_t totalMs = (uint32_t)dripRun.count * dripRun.groupMs;
    uint32_t elapsed = millis() - lastTimeDripIrrigationRequested;
    return elapsed >= totalMs ? 0 : totalMs - elapsed;
  }
  if (dripMaxMs == 0) return 0;
  uint32_t elapsed = millis() - lastTimeDripIrrigationRequested;
  return elapsed >= dripMaxMs ? 0 : dripMaxMs - elapsed;
}
uint32_t getDripGroupRemainingMs() {
  if (!dripIrrigationRequested) return 0;
  const ZoneRunConfig& dripRun = Zones::getDripRunConfig();
  if (dripRun.count > 0 && dripRun.groupMs > 0) {
    uint32_t elapsed = millis() - lastTimeDripZoneSwitched;
    return elapsed >= dripRun.groupMs ? 0 : dripRun.groupMs - elapsed;
  }
  return 0;
}
uint32_t getFillingRemainingMs() {
  if (!fillingRequested || fillingMaxMs == 0) return 0;
  uint32_t elapsed = millis() - lastTimeFillingRequested;
  return elapsed >= fillingMaxMs ? 0 : fillingMaxMs - elapsed;
}

bool getRtcTime(uint8_t& hour, uint8_t& minute, uint8_t& dow) {
  if (rtcReady) {
    DateTime now = rtc.now();
    hour   = now.hour();
    minute = now.minute();
    dow    = now.dayOfTheWeek();  // 0=Sun, 1=Mon…6=Sat
    return true;
  }
  if (timeSet) {
    hour   = ntp.hours();
    minute = ntp.minutes();
    dow    = 0;  // NTP library doesn't expose weekday; caller should ignore dow
    return true;
  }
  return false;
}

uint8_t getWaterLevelPercent() {
  if (level_4) return 100;
  if (level_3) return 75;
  if (level_2) return 50;
  if (level_1) return 25;
  return 0;
}

bool getPumpWellActive() { return getPumpWell(); }
bool getPumpGrassActive() { return getPumpGrass(); }
bool getPumpDripActive() { return getPumpDrip(); }
bool getGrassMainValveActive() { return getGrassMainValve(); }
bool getDripMainValveActive() { return getDripMainValve(); }

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
      const ZoneRunConfig& grassRun = Zones::getGrassRunConfig();
      if (grassRun.count > 0 && grassRun.groupMs > 0) {
        // Group-based: cycle through zone groups, wrap around, stop on total time
        uint32_t groupElapsed = currentTime - lastTimeGrassZoneSwitched;
        if (groupElapsed >= grassRun.groupMs) {
          uint32_t totalMs = (uint32_t)grassRun.count * grassRun.groupMs;
          uint32_t totalElapsed = currentTime - lastTimeGrassIrrigationRequested;
          if (totalElapsed >= totalMs) {
            stopGrassIrrigation();
            Serial.println("Grass irrigation completed");
          } else {
            Zones::changeGrassZone(+1);
            lastTimeGrassZoneSwitched = millis();
            Serial.println("Grass → group " + String(grassRun.activeIdx));
          }
        }
      } else {
        // Legacy flat single-zone cycling (hardware-button start)
        uint32_t elapsed = currentTime - lastTimeGrassIrrigationRequested;
        if (elapsed >= grassMaxMs) {
          stopGrassIrrigation();
          Serial.println("Grass irrigation completed (legacy)");
        } else {
          uint32_t switchMs = controllerConfig.numberOfGrassZones > 1
            ? grassMaxMs / (controllerConfig.numberOfGrassZones - 1) : grassMaxMs;
          if ((currentTime - lastTimeGrassZoneSwitched) >= switchMs) {
            Zones::changeGrassZone(+1);
            lastTimeGrassZoneSwitched = currentTime;
          }
          for (uint8_t i = 1; i < controllerConfig.numberOfGrassZones; i++)
            setOutput(grassZones[i], i != Zones::getGrassZoneIndex());
        }
      }
    }

    if (dripIrrigationRequested) {
      const ZoneRunConfig& dripRun = Zones::getDripRunConfig();
      if (dripRun.count > 0 && dripRun.groupMs > 0) {
        uint32_t groupElapsed = currentTime - lastTimeDripZoneSwitched;
        if (groupElapsed >= dripRun.groupMs) {
          uint32_t totalMs = (uint32_t)dripRun.count * dripRun.groupMs;
          uint32_t totalElapsed = currentTime - lastTimeDripIrrigationRequested;
          if (totalElapsed >= totalMs) {
            stopDripIrrigation();
            Serial.println("Drip irrigation completed");
          } else {
            Zones::changeDripZone(+1);
            lastTimeDripZoneSwitched = millis();
            Serial.println("Drip → group " + String(dripRun.activeIdx));
          }
        }
      } else {
        uint32_t elapsed = currentTime - lastTimeDripIrrigationRequested;
        if (elapsed >= dripMaxMs) {
          stopDripIrrigation();
          Serial.println("Drip irrigation completed (legacy)");
        }
      }
    }

    if (fillingRequested && ((currentTime - lastTimeFillingRequested) >= fillingMaxMs)) {
      fillingRequested = false;
      Serial.println("Filling completed in " + String(fillingMaxMs / 60000UL) + " minutes");
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
  currentTime = millis();
#ifdef PRESSURE_SENSOR
  if ((currentTime - lastTimeShowLevel) >= PRESSURE_SCAN_PERIOD_MS) {
    pressureRaw = pressureSensor.read(true);
    //showPressure(6, 1);
    int id = 0;
    int d;
    while (Serial2.available()) {
      Serial.print(d = Serial2.read(), HEX);
      Serial.print(' ');
      if (id == 1) ultrasonic = d << 8;
      if (id == 2) ultrasonic += d;
      id++;
      if (id % 4 == 0) Serial.println();
    }
    if (id) {
      Serial.println();
      if ((ultrasonic > 0) && (ultrasonic < 6000)) showUltrasonic(6, 1);
    }
    lastTimeShowLevel = currentTime;
  }
  // if ((long)pressureRaw > HIGH_LEVEL_PRESSURE) {
  //   fillingEnabled = false;
  // }
#endif
  currentTime = millis();
  if ((currentTime - lastTimeShowInputs) >= STATUS_SHOW_PERIOD_MS) {
    showStates();
    showDiagInfo();
    lastTimeShowInputs = currentTime;
  }
  controlOutputs();

  networkLoop();
}

void controlOutputs() {
  if (!pcf_init_code) {
    setPumpWell(fillingRequested && fillingEnabled);
  }

  bool grassIrrigationState = grassIrrigationRequested && !drainingDisabled;
  if (grassIrrigationState != prevGrassIrrigationState) {
    if (grassIrrigationState) grassPumpStartTime = millis();
    prevGrassIrrigationState = grassIrrigationState;
  }
  bool grassDelayPassed = (millis() - grassPumpStartTime > GRASS_PUMP_START_DELAY_SECONDS * 1000UL);
  if (!pcf_init_code) {
    setGrassMainValve(grassIrrigationState);
    setPumpGrass(grassIrrigationState && grassDelayPassed);
  }

  bool dripIrrigationState = dripIrrigationRequested && !drainingDisabled;
  if (dripIrrigationState != prevDripIrrigationState) {
    if (dripIrrigationState) dripPumpStartTime = millis();
    prevDripIrrigationState = dripIrrigationState;
  }
  if (!pcf_init_code) {
    setDripMainValve(dripIrrigationState);
    setPumpDrip(dripIrrigationState);
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

void adjustRtc(NTP *ntp_v) {
    if (rtcReady) {
      Serial.print("Adjusting RTC ... ");
      rtc.adjust(DateTime(ntp_v->year(), ntp_v->month(), ntp_v->day(), ntp_v->hours(), ntp_v->minutes(), ntp_v->seconds()));
      Serial.println("done.");
    } else {
      Serial.println("RTC not ready to be adjusted.");
    }
//    rtc.adjust(ntp->epoch());
    timeSet = true;
}

bool getFilteredInput(uint8_t inputNumber) {
  if ((inputNumber < 1) || (inputNumber > 16)) return false;
  return iFiltered & (1 << (inputNumber - 1));
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
    if (diag & L1_DEFECT) {
      Serial.println(" **** L1 defect detected ****. Irrigation disabled.");
    }
    if (diag & L12_DEFECT) {
      Serial.println(" **** L12 defect detected ****. Irrigation disabled.");
    }
    if (diag & L123_DEFECT) {
      Serial.println(" **** L123 defect detected ****. Filling disabled.");
    }
    if (diag & TANK_UPPER_LIMIT_DEFECT) {
      Serial.println(" **** Tank upper limit defect detected ****. Filling disabled.");
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
    stopGrassIrrigation();
    stopDripIrrigation();
    diag |= L1_DEFECT;
  } else if (level_3 && (!level_2 || !level_1)) {
    drainingDisabled = true;
    stopGrassIrrigation();
    stopDripIrrigation();
    diag |= L12_DEFECT;
  } else if (level_4 && (!level_3 || !level_2 || !level_1)) {
    stopFilling();
    diag |= L123_DEFECT;
  }

  if (getFilteredInput(TANK_UPPER_LIMIT1_SWITCH) && !getFilteredInput(TANK_UPPER_LIMIT2_SWITCH)) {
    stopFilling();
    diag |= TANK_UPPER_LIMIT_DEFECT;
  }
  if (fillingEnabled && (fillingEnabled != prevFillingEnabled)) {
    leakageDetectorCounter++;
    if (leakageDetectorCounter > LEAKAGE_DETECTOR_THRESHOLD) {
      stopFilling();
      diag |= LEAK_DEFECT;
    }
  }
  prevFillingEnabled = fillingEnabled;
}

void ScanPCFInputs()
{
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
  uint16_t butState = (iFiltered ^ 0xFFFF) & BUTTONS_MASK;
  if (lastButState != butState) {
    Serial.print("Button change: 0x");Serial.println(butState, HEX);
    switch (butState) {
      case BUTTON_FILLING_MASK:    // Filling button
        if (!fillingRequested) {
          lastTimeFillingRequested = millis();
          fillingMaxMs = controllerConfig.fillingMaxMinutes / (level_2 ? 2 : level_3 ? 3 : 1) * 60 * 1000UL;
          fillingRequested = true;
          startFilling();
          websocketNotifyHardwareCommand("start", "filling");
        } else if (millis() - lastTimeFillingRequested < 3000UL) {
          fillingMaxMs *= 2;
        } else {
          stopFilling();
          websocketNotifyHardwareCommand("stop", "filling");
        }
        if (!level_4) fillingEnabled = true;
        //i1FilterState.last_state |= 1 << (TANK_UPPER_LIMIT_SWITCH - 1);
        leakageDetectorCounter = 0;
        break;
      case BUTTON_GRASS_MASK:    // Grass button
        if (grassIrrigationRequested) {
          stopGrassIrrigation();
          restoreAreaManualStop("grass");
          websocketNotifyHardwareCommand("stop", "grass");
        } else {
          Areas::configureAreaZones("grass", areas, numAreas);
          armAreaManualStart("grass");
          startGrassIrrigation();
          websocketNotifyHardwareCommand("start", "grass");
        }
        break;
      case BUTTON_DRIP_MASK:    // Drip button
        if (dripIrrigationRequested) {
          stopDripIrrigation();
          restoreAreaManualStop("drip");
          websocketNotifyHardwareCommand("stop", "drip");
        } else {
          Areas::configureAreaZones("drip", areas, numAreas);
          armAreaManualStart("drip");
          startDripIrrigation();
          websocketNotifyHardwareCommand("start", "drip");
        }
        break;
      case BUTTON_ZONE_SWITCH_MASK:    // Zone Switch button
        if (grassIrrigationRequested) {
          Zones::changeGrassZone(+1);
          lastTimeGrassZoneSwitched = millis();
          websocketNotifyHardwareCommand("zone_next", "grass", Zones::getGrassZoneIndex());
        }
        break;
      case BUTTONS_FILL_GRASS_MASK:    // Filling and Grass buttons together
        /* code */
        break;
      case BUTTONS_GRASS_DRIP_MASK:    // Grass and Drip buttons together
        /* code */
        break;
      
      default:
        break;
    }
    lastButState = butState;
  }
}
