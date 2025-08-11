#include <Arduino.h>
#include "file_config.h"
#include "hw_info.h"
#include "i2c/hw_config.h"
#include "main.h"

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

#define HX_SCK_PIN 32
#define HX_DAT_PIN 33

RTC_DS3231 rtc;
WiFiUDP ntpUDP;
NTP ntp(ntpUDP);

HX710B pressureSensor(HX_SCK_PIN, HX_DAT_PIN);


#define TIME_UPDATE_PERIOD_MS  1000UL
#define STATUS_SHOW_PERIOD_MS   200UL
#define INPUT1_SCAN_PERIOD_MS     5UL

#define FILLING_MAX_MS (FILLING_MAX_MINUTES * 60*1000UL) // 40 minutes
#define GRASS_MAX_MS   (GRASS_MAX_MINUTES * 60*1000UL)   // 20 minutes
#define DRIP_MAX_MS    (DRIP_MAX_MINUTES * 60*1000UL)    // 120 minutes

#define LEVEL_FILTERING_COUNTER_THRESHOLD (LEVEL_FILTERING_SECONDS * 1000 / INPUT1_SCAN_PERIOD_MS)
#define BUTTON_FILTERING_COUNTER_THRESHOLD (BUTTON_FILTERING_MS / INPUT1_SCAN_PERIOD_MS)

// If no DHCP used, select a static IP address, subnet mask and a gateway IP address according to your local network
// IPAddress myIP(192, 168, 255, 201);
// IPAddress mySN(255, 255, 255, 0);
// IPAddress myGW(192, 168, 255, 65);

// ... and DNS Server IP
// IPAddress myDNS(8, 8, 8, 8);

// ------------- Time --------------------------------
unsigned long currentTime = millis();
// Previous times
unsigned long lastTimeShowTime    = 0;
unsigned long lastTimeShowLevel   = 0;
unsigned long lastTimeShowInputs  = 0;
unsigned long lastTimeScanButtons = 0;
unsigned long lastTimeGrassIrrigationRequested = 0;
unsigned long lastTimeDripIrrigationRequested = 0;
uint32_t oldTusCnt;
uint32_t oldTlsCnt;
bool prevGrassIrrigationState = false;
uint32_t grassPumpStartTime;

uint8_t pcf_init_code;
// ----------- Connection status ---------------------
bool previousConnected = false;
int8_t previousNetworkStatus = -1;
uint8_t previousFilteredState;
uint8_t i1State;
uint8_t i2State;
// -------------- Pump related -----------------------
uint32_t pressureRaw = 0;
bool fillingEnabled = false;
bool prevFillingEnabled = false;
uint32_t leakageDetectorCounter = 0;
bool drainingDisabled = true;
bool fillingRequested = false;
bool grassIrrigationRequested = false;
bool dripIrrigationRequested = false;

FilterState filterState = {0};
uint8_t lastButState = BUTTON_MASK;

bool timeSet = false;
bool rtcReady = false;
bool timeBlink = false;

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

void checkConnection() {
  bool now_connected = getNetworkIsConnected();
  int8_t networkStatus = getNetworkStatus();
  String s;
  if (networkStatus != previousNetworkStatus) {
#ifdef WIFI_NO_ETHERNET  
  switch (networkStatus) {
    case 0: s = "WiFi Idle      "; break;
    case 1: s = "SSID Not Found "; break;
    case 2: s = "WiFi Scanned   "; break;
    case 3: s = "WiFi Connected "; break;
    case 4: s = "WiFi ConnFailed"; break;
    case 5: s = "Connection Lost"; break;
    case 6: s = "Disconnected   "; break;
    default: s = "Unknown WiFi Status"; break;
  }
#else
    if (networkStatus == 1) {
      s = "Cable connected.";
    } else {
      s = "Cable disconnected.";
    }
#endif  
    Serial.println(s);oled_show(1, s);
    previousNetworkStatus = networkStatus;
  }
  if (now_connected != previousConnected) {
    if (!now_connected) { 
#ifndef WIFI_NO_ETHERNET  
      if (networkStatus) {
        s = "Waiting for DHCP...";
        Serial.println(s);oled_show(1, s);
      }
#endif      
      s = "";
    } else {
      s = String(ip2CharArray(getNetworkLocalIp()));
      Serial.println(s);oled_show(1, s);

      setup_NTP();  // This also updates the time
      Serial.println("NTP setup complete.");
      if (ntp.epoch() > (24 * 60 * 60)) {
        adjustRtc(&ntp);
      }

      // start the web server on port 80
      Serial.println("Starting Web server ...");
      serverInit();
      s = "Server started.";
    }
    if (s != "") Serial.println(s);
    oled_show(2, s);
    previousConnected = now_connected;
  }
}

void showPressure(uint8_t code, uint8_t line, uint8_t size) {
  char pressure[24];
  if ( code != HX710B_OK ) {
    sprintf(pressure, "err: %d", code);
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
  char line[24];
  if (rtcReady) {
    sprintf(tm, "%02u:%02u:%02u", rtc.now().hour(), rtc.now().minute(), rtc.now().second());
    sprintf(line, "%.2f C   %s %c", rtc.getTemperature(), (timeSet || timeBlink) ? tm : "        ", pcf_init_code ? 'E' : ' ');
  } else {
    sprintf(tm, "%02u:%02u:%02u", ntp.hours(), ntp.minutes(), ntp.seconds());
    sprintf(line, "---     %s %c", (timeSet || timeBlink) ? tm : "         ", pcf_init_code ? 'E' : ' ');
  }
  oled_show(0, line, 1);
  timeBlink = !timeBlink;
}

void showStates() {
  char tm[14];
  char states[24];
  char pumpStates[8];
  int32_t irrigationRemainingMinutes = (millis() - lastTimeGrassIrrigationRequested) / 60000L;
  sprintf(states, "%02X %02X %d%d %d%d G%02d L%d ", i1State, i2State, fillingRequested, fillingEnabled, 
          grassIrrigationRequested, drainingDisabled, irrigationRemainingMinutes, leakageDetectorCounter);
  //Serial.println(states);
  oled_show(7, states, 1);

  uint32_t tusCnt = filterState.counter[TANK_UPPER_SWITCH_INPUT_NUMBER-1];
//  sprintf(pumpStates, "%c%c%c", getPumpWell() ? 'F' : ' ', getPumpGrass ? 'G' : ' ', getPumpDrip ? 'D' : ' ');
  sprintf(pumpStates, "%c%c%c", fillingRequested ? 'F' : ' ', grassIrrigationRequested ? 'G' : ' ', dripIrrigationRequested ? 'D' : ' ');
  if (tusCnt) {
    if (tusCnt != oldTusCnt) {
      sprintf(states, "%s U%.1f ", pumpStates, tusCnt*INPUT1_SCAN_PERIOD_MS/1000.0);
      oled_show(4, states, 2);
    }
  } else {
    oled_show(4, pumpStates, 2);
  }
  oldTusCnt = tusCnt;

  uint32_t tlsCnt = filterState.counter[TANK_LOWER_SWITCH_INPUT_NUMBER-1];
  if (tlsCnt != oldTlsCnt) {
    if (tlsCnt) {
      sprintf(states, "%s L%.1f ", pumpStates, tlsCnt*INPUT1_SCAN_PERIOD_MS/1000.0);
      oled_show(4, states, 2);
    }
    oldTlsCnt = tlsCnt;
  }

  if (filterState.last_state != previousFilteredState) {
    previousFilteredState = filterState.last_state;
    sprintf(tm, "%02u:%02u:%02u: ", rtc.now().hour(), rtc.now().minute(), rtc.now().second());
    Serial.print(tm);
    Serial.println(states);
  }
}

void setup() {
  Serial.begin(115200);
  //-------------------------------------
  while (!Serial)
    delay(100);

  printHwInfo();
  initFs();
  loadConfig();
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
#ifndef DEV_BOARD_OLED 
  // Init PCFs
  pcf_init_code = init_pcfs();
  String s = "Init PCFs... " + (pcf_init_code == 0 ? "OK" : "Error " + String(pcf_init_code, HEX));
  Serial.println(s);oled_show(0, s);
  uint8_t code = pressureSensor.init();
  s = "Init H710B... " + (code == HX710B_OK ? "OK" : "Error " + String(code, HEX));
  Serial.println(s);oled_show(0, s);

  test_pcf();
#endif

  networkInit();
  oled_show(1, "Network started.");

  Serial.print("Level threshold: ");Serial.println(LEVEL_FILTERING_COUNTER_THRESHOLD);
  Serial.print("Button threshold: ");Serial.println(BUTTON_FILTERING_COUNTER_THRESHOLD);
  filterState.threshold[TANK_UPPER_SWITCH_INPUT_NUMBER - 1] = LEVEL_FILTERING_COUNTER_THRESHOLD;
  filterState.threshold[TANK_LOWER_SWITCH_INPUT_NUMBER - 1] = LEVEL_FILTERING_COUNTER_THRESHOLD;
  filterState.threshold[BUTTON_FILLING - 1] = BUTTON_FILTERING_COUNTER_THRESHOLD;
  filterState.threshold[BUTTON_GRASS - 1] = BUTTON_FILTERING_COUNTER_THRESHOLD;
  filterState.threshold[BUTTON_DRIP - 1] = BUTTON_FILTERING_COUNTER_THRESHOLD;

} 

void loop() {
  checkConnection();
  if (getNetworkIsConnected() && ntp.update()) {
    Serial.print("Time synced: " + String(ntp.formattedTime("%T")) + " , ");
    adjustRtc(&ntp);
    ntp.updateInterval(60 * 60 * 1000);     // initially on 1m, after the time is set update the interval to 1h
  }
  currentTime = millis();
  if ((currentTime - lastTimeShowTime) >= (TIME_UPDATE_PERIOD_MS/(2-uint8_t(timeSet)))) {
    showTime();
    lastTimeShowTime = currentTime;

    if ((grassIrrigationRequested && ((currentTime - lastTimeGrassIrrigationRequested) >= GRASS_MAX_MS))) {
      grassIrrigationRequested = false;
      Serial.println("Grass irrigation complete");
    }

    if ((dripIrrigationRequested && ((currentTime - lastTimeDripIrrigationRequested) >= DRIP_MAX_MS))) {
      dripIrrigationRequested = false;
      Serial.println("Drip irrigation complete");
    }
  }
  currentTime = millis();
  if ((currentTime - lastTimeShowInputs) >= (STATUS_SHOW_PERIOD_MS)) {
    showStates();
    lastTimeShowInputs = currentTime;
  }
#ifndef DEV_BOARD_OLED
  currentTime = millis();
  if ((currentTime - lastTimeScanButtons) >= INPUT1_SCAN_PERIOD_MS) {
    ScanInputs();
    lastTimeScanButtons = currentTime;
  }
#endif  
  // currentTime = millis();
  // if ((currentTime - lastTimeShowLevel) >= INPUTS_UPDATE_PERIOD_MS) {
  //   uint8_t code = pressureSensor.read(&pressureRaw, 500UL);
  //   showPressure(code, 4, 2);
  //   lastTimeShowLevel = currentTime;
  // }

  // if ((long)pressureRaw > HIGH_LEVEL_PRESSURE) {
  //   fillingEnabled = false;
  // }

#ifndef DEV_BOARD_OLED  
  if (!pcf_init_code) {
    setPumpWell(fillingRequested && fillingEnabled);
    bool grassIrrigationState = grassIrrigationRequested && !drainingDisabled;
    if (grassIrrigationState != prevGrassIrrigationState) {
      if (grassIrrigationState) {
        grassPumpStartTime = millis();
      }
      prevGrassIrrigationState = grassIrrigationState;
    }
    bool delayTimePassed = (millis() - grassPumpStartTime > GRASS_PUMP_START_DELAY_SECONDS * 1000UL);
    setPumpGrass(grassIrrigationState && delayTimePassed);
    setGrassMainValve(grassIrrigationState);
  }
#endif
  serverLoop();
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

void setGrassMainValve(bool value) {
  setOutput(MAIN_VALVE_GRASS, !value);
}

void setDripMainValve(bool value) {
  setOutput(MAIN_VALVE_DRIP, !value);
}

void setPumpWell(bool value) {
  setOutput(PUMP_WELL_OUTPUT_NUMBER, !value);
}

void setPumpGrass(bool value) {
  setOutput(PUMP_GRASS_OUTPUT_NUMBER, !value);
}

void setPumpDrip(bool value) {
  setOutput(PUMP_DRIP_OUTPUT_NUMBER, !value);
}

bool getPumpWell() {
  return !getOutput(PUMP_WELL_OUTPUT_NUMBER);
}

bool getPumpGrass() {
  return !getOutput(PUMP_GRASS_OUTPUT_NUMBER);
}

bool getPumpDrip() {
  return !getOutput(PUMP_DRIP_OUTPUT_NUMBER);
}

void ScanInputs()
{
  char line[24];
  i1State = pcf8574_I1.digitalReadAll();
  i2State = pcf8574_I2.digitalReadAll();
  uint8_t filtered = filter_inputs(i1State, &filterState);

  handleButtons(filtered);

  drainingDisabled = filtered & (1 << (TANK_LOWER_SWITCH_INPUT_NUMBER - 1));
  fillingEnabled   = filtered & (1 << (TANK_UPPER_SWITCH_INPUT_NUMBER - 1));
  
  if (fillingEnabled && (fillingEnabled != prevFillingEnabled)) {
    leakageDetectorCounter++;
    if (leakageDetectorCounter > LEAKAGE_DETECTOR_THRESHOLD) {
      Serial.println("Stop filling due to leaks.");
      fillingRequested = false;
    }
  }
  prevFillingEnabled = fillingEnabled;
}

void handleButtons(uint8_t filtered) {  
  uint8_t butState = (filtered ^ 0xFF) & BUTTON_MASK;
  
  if (lastButState != butState) {
    Serial.print("Button change: 0x");Serial.println(butState, HEX);
    switch (butState) {
      case 0x04:    // Filling button
        fillingRequested = !fillingRequested;
        filterState.last_state |= 1 << (TANK_UPPER_SWITCH_INPUT_NUMBER - 1); 
        leakageDetectorCounter = 0;
        break;
      case 0x08:    // Grass button
        grassIrrigationRequested = !grassIrrigationRequested;
        if (grassIrrigationRequested) 
          lastTimeGrassIrrigationRequested = millis();
          leakageDetectorCounter = 0;
        break;
      case 0x10:    // Drip button
        dripIrrigationRequested = !dripIrrigationRequested;
        if (dripIrrigationRequested) 
          lastTimeDripIrrigationRequested = millis();
          leakageDetectorCounter = 0;
        break;
      case 0x14:    // Filling and Grass buttons together
        /* code */
        break;
      case 0x18:    // Grass and Drip buttons together
        /* code */
        break;
      
      default:
        break;
    }
    lastButState = butState;
  }
}
