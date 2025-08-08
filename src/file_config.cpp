#include "FS.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "file_config.h"

#define FORMAT_LITTLEFS_IF_FAILED true

bool initFs() {
  bool res = LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED, "/spiffs");
  if (!res) {
    Serial.println(res ? "LittleFS mounted." : "LittleFS Mount Failed");
  }
  Serial.printf("Total: %llu bytes\n", LittleFS.totalBytes());
  Serial.printf("Used:  %llu bytes\n", LittleFS.usedBytes());
  return res;
}

void printTestValues(const JsonDocument& doc) {
  // Read values
  const char* deviceName = doc["device_name"];
  int interval = doc["interval"];
  bool enabled = doc["enabled"];

  // Print values
  Serial.println("Config loaded:");
  Serial.printf("  - Device Name: %s\n", deviceName);
  Serial.printf("  - Interval: %d ms\n", interval);
  Serial.printf("  - Enabled: %s\n", enabled ? "true" : "false");
}

bool loadConfig() {
  File file = LittleFS.open("/app_config.json", "r");
  if (!file || file.isDirectory()) {
    Serial.println("Failed to open config file!");
    return false;
  }
  // Allocate a JSON document
  JsonDocument doc;

  // Parse the file
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.f_str());
    return false;
  }

  printTestValues(doc);

  Serial.println("Config loaded from file system.");
  file.close();
  return true;
}