#pragma once

#include <ESPAsyncWebServer.h>

void setupWebSocketProtocol(AsyncWebServer& server);
void websocketProtocolLoop();
void websocketNotifyHardwareCommand(const char* action, const char* target, int zone = -1);
void armAreaManualStart(const char* area);
void restoreAreaManualStop(const char* area);
