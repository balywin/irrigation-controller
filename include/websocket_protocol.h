#pragma once

#include <ESPAsyncWebServer.h>

void setupWebSocketProtocol(AsyncWebServer& server);
void websocketProtocolLoop();
void websocketNotifyHardwareCommand(const char* action, const char* target, int zone = -1);
