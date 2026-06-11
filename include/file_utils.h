#pragma once

#include <ArduinoJson.h>

bool initFs();
bool loadJsonFile(JsonDocument& doc, const String& fileName);