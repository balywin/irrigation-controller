#include <LittleFS.h>

#include "hw_config.h"
#include "network.h"
#include "i2c/oled.h"

#ifdef WIFI_NO_ETHERNET
  #include <WiFi.h>
  #include <WiFiClient.h>

  uint8_t ssid_index = 0;
  WiFiCredentials wifiCredentials[MAX_SSID_NUMBER] = {
    {"balywin", "@Titi14#Papazov22%"},
    {"VivacarM", "@Titi14#Papazov22%"},
    {"Vivacar", "@Titi14#Papazov22%"}
  };
  const unsigned char wificon[] PROGMEM = { // wifi icon
    0x00, 0x3C, 0x42, 0x99, 0x24, 0x42, 0x18, 0x18
  };
  const unsigned char wifidiscon[] PROGMEM = { // wifi icon
    0x00, 0x44, 0x28, 0x10, 0x28, 0x44, 0x00, 0x00
  };
#else
  #include <WebServer_WT32_ETH01.h>
#endif
#ifdef ELEGANTOTA_USE_ASYNC_WEBSERVER  
  #include <ESPAsyncWebServer.h>
  #define ProperWebServer AsyncWebServer
#else
  #include <WebServer.h>
  #define ProperWebServer WebServer
#endif  
#include <ElefantOTA.h>

// If no DHCP used, select a static IP address, subnet mask and a gateway IP address according to your local network
// IPAddress myIP(192, 168, 255, 201);
// IPAddress mySN(255, 255, 255, 0);
// IPAddress myGW(192, 168, 255, 65);

// ... and DNS Server IP
// IPAddress myDNS(8, 8, 8, 8);

// listen for incoming clients
ProperWebServer server(80);
int flagReadDi = 0;

// Variable to store the HTTP request
String header;
uint32_t previousTime = 0;
int8_t previousNetworkStatus = -1;
// ----------- Connection status ---------------------
bool previousConnected = false;

uint32_t ota_progress_millis = 0;

#define WIFI_RECONNECT_COUNTER_THRESHOLD 1000UL
uint32_t wifiReconnectCounter = WIFI_RECONNECT_COUNTER_THRESHOLD;
// Define timeout time in milliseconds (example: 2000ms = 2s)
uint32_t connectionTimeoutMs = 2000;

short relayStates[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // state of relays

void networkInit() {
  #ifdef WIFI_NO_ETHERNET
    Serial.print("Starting WiFi on " + String(ARDUINO_BOARD));
    Serial.print(", looking for SSID '" + wifiCredentials[ssid_index].ssid + "' ... ");
    WiFi.begin(wifiCredentials[ssid_index].ssid, wifiCredentials[ssid_index].password);
    WiFi.mode(WIFI_STA);
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
    return WiFi.isConnected();
  #else
    return WT32_ETH01_isConnected();
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
      case 4: s = "WiFi ConnFailed"; break;
      case 5: s = "Connection Lost"; break;
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
        oled_clear_from(1, 1, SCREEN_WIDTH / 6 - 1);
        oled.drawBitmap(120, 8, wifidiscon, 8, 8, OLED_WHITE);
        oled_clear_line(2);
      } else if (s == "WiFi Connected" || s == "Cable connected") {
        if (s == "WiFi Connected") {
          oled_show(1, String(wifiCredentials[ssid_index].ssid));
        }
        oled.drawBitmap(120, 8, wificon, 8, 8, OLED_WHITE);
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
      oled_clear_keep_last(1, 1, 1);
      oled_show(1, s, 1, false);
      // start the web server on port 80
      Serial.println("Starting Web server ...");
      serverInit();
      s = "Server started.";
      Serial.println(s);
      oled_show(2, s);
    }
    previousConnected = now_connected;
    return now_connected;
  }
  return false;
}

void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
  // <Add your own code here>
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
  // <Add your own code here>
}

String processor(const String& var) {
  Serial.println("index: " + var);
  return var;
}

void serverInit() {

  // Route for root / web page
#ifdef ELEGANTOTA_USE_ASYNC_WEBSERVER  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", String(), false, processor);
  });
#else
  server.on("/", []() {
    server.send(200, "text/plain", "Hi! This is ElegantOTA Demo.");
  });
#endif
  ElegantOTA.begin(&server, "", "", FIRMWARE_VERSION);    // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);
  server.begin();
}

void networkLoop() {
#ifndef ELEGANTOTA_USE_ASYNC_WEBSERVER  
  server.handleClient();
#endif  
  ElegantOTA.loop();
}

char* ip2CharArray(IPAddress ip) {
  static char a[24];
  sprintf(a, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return a;
}
