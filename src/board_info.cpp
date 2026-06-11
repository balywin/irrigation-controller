#include <Arduino.h>
#include "esp_chip_info.h"

String getChipModelDescription(esp_chip_model_t chip_model) {
    switch (chip_model) {
        case 1: return "ESP32";
        case 2: return "ESP32-S2";
        case 5: return "ESP32-C3";
        case 9: return "ESP32-S3";
        case 12: return "ESP32-C2";
        case 13: return "ESP32-C6";
        case 16: return "ESP32-H2";
        default: return "unknown";
    }
}

void printBoardInfo() {
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  Serial.println("------------ Hardware Info: ------------");
  Serial.printf("Model: %d-core %s, rev. %d\n", chip_info.cores, getChipModelDescription(chip_info.model).c_str(), chip_info.revision);
  Serial.printf("Features: WiFi:%s BT:%s BLE:%s\n",
      (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "yes" : "no",
      (chip_info.features & CHIP_FEATURE_BT) ? "yes" : "no",
      (chip_info.features & CHIP_FEATURE_BLE) ? "yes" : "no");
  // Flash memory
  Serial.printf("Flash Chip Size: %u MB\n", ESP.getFlashChipSize() / 1024 / 1024);
  // RAM
  Serial.printf("Free RAM: %u KB\n", ESP.getFreeHeap() / 1024);
  Serial.printf("Total PSRAM: %u KB,  Free PSRAM: %u KB\n", ESP.getPsramSize() / 1024, ESP.getFreePsram() / 1024);
  Serial.println("--------------------------");
}