#include <Arduino.h>
#include "hw_config.h"

#ifndef WIFI_NO_ETHERNET
  #define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT
  #define ETH_PHY_POWER   -1
  #define ETH_PHY_ADDR    0
#else
  #define MAX_SSID_NUMBER 3
  typedef struct WiFiCredentials {
    String ssid;
    String password;
  } WiFiCredentials;
#endif

uint8_t getNetworkStatus();
IPAddress getNetworkLocalIp();
bool getNetworkIsConnected();

void networkInit();
void serverInit();
void httpHandler();
void networkLoop();
char* ip2CharArray(IPAddress ip);
bool checkConnection();
