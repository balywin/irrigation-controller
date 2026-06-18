#include <LittleFS.h>
#include <Update.h>

#include "hw_config.h"
#include "network.h"
#include "i2c/oled.h"
#include "webserver_embedded.h"
#include "websocket_protocol.h"
#include "fallback_page.h"

#include <ESPAsyncWebServer.h>

#ifdef WIFI_NO_ETHERNET
  #include <WiFi.h>
  #include <WiFiClient.h>

  uint8_t ssid_index = 0;
  WiFiCredentials wifiCredentials[MAX_SSID_NUMBER] = {
    {"balywin", "@Titi14#Papazov22%"},
    {"VivacarM", "@Titi14#Papazov22%"},
    {"Vivacar", "@Titi14#Papazov22%"}
  };
  const unsigned char conIcon[] PROGMEM = { // connected icon
    0x00, 0x3C, 0x42, 0x99, 0x24, 0x42, 0x18, 0x18
  };
  const unsigned char disconIcon[] PROGMEM = { // disconnected icon
    0x00, 0x44, 0x28, 0x10, 0x28, 0x44, 0x00, 0x00
  };
#else
  #include <ETH.h>

  bool WT32_ETH01_eth_connected = false;

  void WT32_ETH01_event(WiFiEvent_t event) {
    switch (event) {
      case ARDUINO_EVENT_ETH_START:
        ETH.setHostname("WT32-ETH01");
        break;
      case ARDUINO_EVENT_ETH_CONNECTED:
        break;
      case ARDUINO_EVENT_ETH_GOT_IP:
        WT32_ETH01_eth_connected = true;
        break;
      case ARDUINO_EVENT_ETH_DISCONNECTED:
        WT32_ETH01_eth_connected = false;
        break;
      default:
        break;
    }
  }

  void WT32_ETH01_onEvent() {
    WiFi.onEvent(WT32_ETH01_event);
  }

  bool WT32_ETH01_isConnected() {
    return WT32_ETH01_eth_connected;
  }

  #define WEBSERVER_WT32_ETH01_VERSION "WebServer_WT32_ETH01 v1.5.1"
  #define SHIELD_TYPE "LAN8720"

  const unsigned char conIcon[] PROGMEM = { // connected icon
    0x24, 0x24, 0xFF, 0x81, 0x81, 0x81, 0x42, 0x3C
  };
  const unsigned char disconIcon[] PROGMEM = { // disconnected icon
    0xFF, 0x44, 0x28, 0x10, 0x28, 0x44, 0xFF, 0x00
  };
#endif

#include <ElefantOTA.h>

// If no DHCP used, select a static IP address, subnet mask and a gateway IP address according to your local network
// IPAddress myIP(192, 168, 255, 201);
// IPAddress mySN(255, 255, 255, 0);
// IPAddress myGW(192, 168, 255, 65);

// ... and DNS Server IP
// IPAddress myDNS(8, 8, 8, 8);

// listen for incoming clients
AsyncWebServer server(80);
int flagReadDi = 0;

// Variable to store the HTTP request
String header;
uint32_t previousTime = 0;
int8_t previousNetworkStatus = -1;
// ----------- Connection status ---------------------
bool previousConnected = false;
uint32_t serverStartedEventTime = 0;

uint32_t ota_progress_millis = 0;

#define WIFI_RECONNECT_COUNTER_THRESHOLD 1000UL
uint32_t wifiReconnectCounter = WIFI_RECONNECT_COUNTER_THRESHOLD;

short relayStates[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // state of relays

void networkInit() {
  #ifdef WIFI_NO_ETHERNET
    Serial.print("Starting WiFi on " + String(ARDUINO_BOARD));
    Serial.print(", looking for SSID '" + wifiCredentials[ssid_index].ssid + "' ... ");
    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.begin(wifiCredentials[ssid_index].ssid, wifiCredentials[ssid_index].password);
  #else
    Serial.print("\nStarting " + String(WEBSERVER_WT32_ETH01_VERSION) + " on " + String(ARDUINO_BOARD));
    Serial.print(" with " + String(SHIELD_TYPE));

    // To be called before ETH.begin()
    WT32_ETH01_onEvent();

    // bool begin(uint8_t phy_addr=ETH_PHY_ADDR, int power=ETH_PHY_POWER, int mdc=ETH_PHY_MDC, int mdio=ETH_PHY_MDIO,
    //            eth_phy_type_t type=ETH_PHY_TYPE, eth_clock_mode_t clk_mode=ETH_CLK_MODE);
    // ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE);
    ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER);
    // Static IP, leave without this line to get IP via DHCP
    // bool config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1 = 0, IPAddress dns2 = 0);
    //  ETH.config(myIP, myGW, mySN, myDNS);oled.setCursor(0, 0);oled.println(F("Manual IP config."));oled.display();
    Serial.println(" ... started.");
  #endif
}

uint8_t getNetworkStatus() {
  #ifdef WIFI_NO_ETHERNET
    return WiFi.status();
  #else
    return ETH.linkUp() ? 1 : 0;
  #endif
}

IPAddress getNetworkLocalIp() {
  #ifdef WIFI_NO_ETHERNET
    return WiFi.localIP();
  #else
    return ETH.localIP();
  #endif
}

bool getNetworkIsConnected() {
  #ifdef WIFI_NO_ETHERNET
    return getNetworkLocalIp() != IPAddress(0, 0, 0, 0);
  #else
    return WT32_ETH01_isConnected();
  #endif
}

String getNetworkMacAddress() {
  #ifdef WIFI_NO_ETHERNET
    return WiFi.macAddress();
  #else
    return ETH.macAddress();
  #endif
}

/* Returns true if just got connected, false if not */
bool checkConnection() {
  if (wifiReconnectCounter < WIFI_RECONNECT_COUNTER_THRESHOLD) {
    wifiReconnectCounter++;
    if (wifiReconnectCounter == WIFI_RECONNECT_COUNTER_THRESHOLD) {
      networkInit();
    }
    return false;
  }
  bool now_connected = getNetworkIsConnected();
  int8_t networkStatus = getNetworkStatus();
  String s;
  if (networkStatus != previousNetworkStatus) {
#ifdef WIFI_NO_ETHERNET
    switch (networkStatus) {
      case 0: s = "WiFi Idle"; break;
      case 1:
        s = String(wifiCredentials[ssid_index].ssid) + " Not Found";
        if (++ssid_index >= MAX_SSID_NUMBER) ssid_index = 0;
        WiFi.disconnect(true);
        wifiReconnectCounter = 0;
        break;
      case 2: s = "WiFi Scanned"; break;
      case 3: s = "WiFi Connected"; break;
      case 4:
        s = "WiFi ConnFailed";
        WiFi.disconnect(true);
        wifiReconnectCounter = 0;
        break;
      case 5:
        s = "Connection Lost";
        WiFi.disconnect(true);
        wifiReconnectCounter = 0;
        break;
      case 6: s = "Disconnected"; break;
      default: s = ""; break;
    }
#else
    if (networkStatus == 1) {
      s = "Cable connected";
    } else {
      s = "Cable disconnected";
    }
#endif
    if (s != "") {
      if (s != "Disconnected") Serial.println(s);
      if (s == "Disconnected" || s == "Cable disconnected") {
#ifndef WIFI_NO_ETHERNET
        oled_show(1, s);
#endif
        oled_clear_from(1, 1, 19, 2);
        oled.drawBitmap(120, 8, disconIcon, 8, 8, OLED_WHITE);
        oled_clear_line(2);
      } else if (s == "WiFi Connected" || s == "Cable connected") {
#ifdef WIFI_NO_ETHERNET
        if (s == "WiFi Connected") {
          oled_show(1, String(wifiCredentials[ssid_index].ssid));
        }
#endif
        oled_clear_from(1, 1, 19, 2);
        oled.drawBitmap(120, 8, conIcon, 8, 8, OLED_WHITE);
      } else {
        oled_clear_keep_last(1, 1, 1);
        oled_show(1, s, 1, false);
      }
    }
    previousNetworkStatus = networkStatus;
  }
  if (now_connected != previousConnected) {
    if (!now_connected) {
#ifndef WIFI_NO_ETHERNET
      if (networkStatus) {
        s = "Waiting for DHCP...";
        Serial.println(s);oled_show(1, s);
      }
#else
      oled_clear_keep_last(1, 1, 1);
      oled_show(1, s, 1, false);
#endif
    } else {
      s = String(ip2CharArray(getNetworkLocalIp()));
      Serial.println(s);
      //s = String(ip2CharArrayShort(getNetworkLocalIp()));
      oled_clear_keep_last(1, 1, 1);
      oled_show(1, s, 1, false);
      Serial.println("Starting Web server ...");
      serverInit();
      s = "Server started.";
      Serial.println(s);
      oled_show(2, s);
      serverStartedEventTime = millis();
    }
    previousConnected = now_connected;
    return now_connected;
  }
  return false;
}

void onOTAStart(OTA_Mode mode) {
  Serial.println("OTA update started!");
  const char* otaType = (mode == OTA_MODE_FIRMWARE) ? "Firmware" : "LittleFS";
  oled_show(2, otaType, 1);
  oled_show(1, "SW Update", 1);
  serverStartedEventTime = millis() + 15000UL;
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  char percentStr[12];
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
//    sprintf(percentStr, "%u%%", (current * 100) / final);
//    oled_show_at((current * 10) / final + 11, 1, ".", 1);
//    serverStartedEventTime = millis() + 15000UL;
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    oled_show(1, "OTA Success...", 1);
    Serial.println("OTA update finished successfully!");
  } else {
    oled_show(1, "OTA Failed...", 1);
    Serial.println("There was an error during OTA update!");
  }
  // <Add your own code here>
}

String processor(const String& var) {
  Serial.println("index: " + var);
  return var;
}

static bool littleFsHasWebUI() {
  return LittleFS.exists("/index.html");
}

void serverInit() {

  // Root → redirect to /index.html (served by embedded webserver from PROGMEM)
#ifdef ELEGANTOTA_USE_ASYNC_WEBSERVER
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->redirect("/index.html");
  });
#else
  server.on("/", []() {
    server.send(200, "text/plain", "Hi! This is ElegantOTA Demo.");
  });
#endif

  // Always-available recovery page
  server.on("/recovery", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", FALLBACK_HTML);
  });

  // Fallback API: device status
  server.on("/api/fallback/status", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"ip\":\"" + String(ip2CharArray(getNetworkLocalIp())) + "\",";
    json += "\"uptime\":\"" + String(millis() / 1000) + "s\",";
    json += "\"heap\":\"" + String(ESP.getFreeHeap()) + " bytes\",";
    json += "\"fs\":\"" + String(LittleFS.usedBytes()) + "/" + String(LittleFS.totalBytes()) + " bytes\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  // Fallback API: upload a full LittleFS image (OTA filesystem partition)
  server.on("/api/fallback/fs-upload", HTTP_POST, [](AsyncWebServerRequest *request){
    if (Update.hasError()) {
      request->send(400, "text/plain", "Filesystem update failed: " + String(Update.errorString()));
    } else {
      request->send(200, "text/plain", "OK");
      delay(1000);
      ESP.restart();
    }
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if (index == 0) {
      Serial.printf("Fallback: filesystem upload start: %s\n", filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
        Serial.printf("Update.begin failed: %s\n", Update.errorString());
      }
    }
    if (Update.isRunning()) {
      if (Update.write(data, len) != len) {
        Serial.printf("Update.write failed: %s\n", Update.errorString());
      }
    }
    if (final) {
      if (Update.end(true)) {
        Serial.printf("Fallback: filesystem upload complete (%u bytes)\n", index + len);
      } else {
        Serial.printf("Update.end failed: %s\n", Update.errorString());
      }
    }
  });

  // Fallback API: upload a single file to LittleFS
  server.on("/api/fallback/file-upload", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "OK");
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    static String fsPath;
    if (index == 0) {
      fsPath = "/index.html"; // default
      if (request->hasParam("path", false)) {
        fsPath = request->getParam("path", false)->value();
      } else if (request->hasParam("path")) {
        fsPath = request->getParam("path")->value();
      }
      // Ensure parent directories exist (LittleFS creates them automatically)
      Serial.printf("Fallback: single file upload -> %s\n", fsPath.c_str());
      File f = LittleFS.open(fsPath, "w");
      if (f) { f.write(data, len); f.close(); }
    } else {
      File f = LittleFS.open(fsPath, "a");
      if (f) { f.write(data, len); f.close(); }
    }
    if (final) {
      Serial.printf("Fallback: file upload complete %s (%u bytes)\n", fsPath.c_str(), index + len);
    }
  });

  ElegantOTA.begin(&server, "", "", FIRMWARE_VERSION);    // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  // Setup embedded web server (serves the firmware-embedded SPA and config APIs)
  setupEmbeddedWebServer(server);
  setupWebSocketProtocol(server);

  server.begin();
  Serial.println("Embedded web server started on port 80");
}

void networkLoop() {
#ifndef ELEGANTOTA_USE_ASYNC_WEBSERVER  
  server.handleClient();
#endif  
  ElegantOTA.loop();
  websocketProtocolLoop();
}

char* ip2CharArray(IPAddress ip) {
  static char a[24];
  sprintf(a, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return a;
}

char* ip2CharArrayShort(IPAddress ip) {
  static char a[18];
  sprintf(a, "%d.%d ", ip[2], ip[3]);
  return a;
}

uint32_t getServerStartedEventTime() {
  return serverStartedEventTime;
}

void clearServerStartedEventTime() {
  serverStartedEventTime = 0;
}

int getNetworkRssi() {
#ifdef WIFI_NO_ETHERNET
  if (!WiFi.isConnected()) return 0;
  return (int)WiFi.RSSI();
#else
  return 0;
#endif
}