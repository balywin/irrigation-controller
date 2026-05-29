#pragma once

#include "hw_config.h"

#ifndef WIFI_NO_ETHERNET
  #define ETH_CLK_MODE    (ETH_CLOCK_GPIO17_OUT)
  #define ETH_PHY_POWER   (-1)
  #define ETH_PHY_ADDR    (0)
#else
  #define MAX_SSID_NUMBER (3)
  typedef struct WiFiCredentials {
    String ssid;
    String password;
  } WiFiCredentials;
#endif

class AsyncWebServer;

// HTTP server for web UI and API
extern AsyncWebServer server;

uint8_t getNetworkStatus();
IPAddress getNetworkLocalIp();
bool getNetworkIsConnected();
String getNetworkMacAddress();

void networkInit();
void serverInit();
void httpHandler();
void networkLoop();
char* ip2CharArray(IPAddress ip);
char* ip2CharArrayShort(IPAddress ip);
bool checkConnection();

uint32_t getServerStartedEventTime();
void clearServerStartedEventTime();