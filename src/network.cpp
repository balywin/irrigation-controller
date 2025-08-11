#include "hw_config.h"
#include "network.h"

#ifdef WIFI_NO_ETHERNET
  #include <WiFi.h>
  #include <WiFiClient.h>
  #include <WebServer.h>
#else
  #include <WebServer_WT32_ETH01.h>
#endif
#include <ElegantOTA.h>

// listen for incoming clients
WebServer server(80);
int flagReadDi = 0;

// Variable to store the HTTP request
String header;
uint32_t previousTime = 0;

unsigned long ota_progress_millis = 0;

// Define timeout time in milliseconds (example: 2000ms = 2s)
long connectionTimeoutMs = 2000;

short relayStates[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // state of relays

  void networkInit() {
#ifdef WIFI_NO_ETHERNET
    Serial.print("Starting WiFi on " + String(ARDUINO_BOARD));
    Serial.print(", looking for SSID '" + String(AP_SSID) + "'");
    WiFi.begin(AP_SSID, AP_PASSWORD);
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
#endif
    Serial.println(" ... started.");
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

void serverInit() {

  server.on("/", []() {
    server.send(200, "text/plain", "Hi! This is ElegantOTA Demo.");
  });

  ElegantOTA.begin(&server);    // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);
  server.begin();
}

void serverLoop() {
  server.handleClient();
  ElegantOTA.loop();
}

char* ip2CharArray(IPAddress ip) {
  static char a[24];
  sprintf(a, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return a;
}
