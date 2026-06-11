#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include "file_utils.h"

#define FORMAT_LITTLEFS_IF_FAILED true

bool initFs() {
  bool res = LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED, "/spiffs");
  Serial.printf("LittleFS ");
  if (res) Serial.printf("Mounted:  Total: %u KB, Used: %u KB\n", LittleFS.totalBytes() / 1024, LittleFS.usedBytes() / 1024);
  else Serial.println("Mount Failed!");
  Serial.println("--------------------------");
  return res;
}

bool loadJsonFile(JsonDocument& doc, const String& fileName) {
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