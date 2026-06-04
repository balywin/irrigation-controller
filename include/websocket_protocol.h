#pragma once

#include <ESPAsyncWebServer.h>

void setupWebSocketProtocol(AsyncWebServer& server);
void websocketProtocolLoop();
void websocketNotifyHardwareCommand(const char* action, const char* target, int zone = -1);
void armAreaManualStart(const char* area);
void restoreAreaManualStop(const char* area);

// Pause state accessors for OLED display (area = "grass"/"drip"/"filling")
bool isAreaPausedByKey(const char* area);
unsigned long areaRemainingPauseMs(const char* area);
