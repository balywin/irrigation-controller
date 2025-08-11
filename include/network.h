#include <Arduino.h>
#include "hw_config.h"

#ifndef WIFI_NO_ETHERNET
  #define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT
  #define ETH_PHY_POWER   -1
  #define ETH_PHY_ADDR    0
#endif

uint8_t getNetworkStatus();
IPAddress getNetworkLocalIp();
bool getNetworkIsConnected();

void networkInit();
void serverInit();
void httpHandler();
void serverLoop();
char* ip2CharArray(IPAddress ip);
