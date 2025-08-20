#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include "file_utils.h"

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

bool loadFile(JsonDocument& doc, String fileName) {
  File file = LittleFS.open(fileName, "r");
  if (!file || file.isDirectory()) {
    Serial.println("Failed to open file " + fileName);
    return false;
  }
  // Parse the file
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.print("Failed to parse JSON from " + fileName);
    Serial.println(error.f_str());
    return false;
  }
  file.close();

  Serial.println(fileName + " loaded from the file system.");

  return true;
}