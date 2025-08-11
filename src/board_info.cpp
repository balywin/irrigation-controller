#include <Arduino.h>
void printBoardInfo() {
  Serial.println("------------------------\nESP32 Memory Info:");

  // Flash memory
  Serial.print("Flash Chip Size: ");
  Serial.print(ESP.getFlashChipSize() / 1024 / 1024);
  Serial.print(" MB,   ");

  // RAM
  Serial.print("Free RAM: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  Serial.print("Total PSRAM: ");
  Serial.print(ESP.getPsramSize() / 1024);
  Serial.print(" KB,   Free PSRAM: ");
  Serial.print(ESP.getFreePsram() / 1024);
  Serial.println(" KB\n--------------------------");
}