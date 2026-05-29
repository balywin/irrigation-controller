#pragma once

#include <ArduinoJson.h>
#include "zones.h"

// Area configuration (grass, drip, etc.)
struct AreaConfig {
  char id[32];                    // "Grass", "Drip"
  bool enabled;
  uint8_t pump_id;
  uint8_t main_valve_id;
  uint16_t pump_start_delay_seconds;
  uint16_t max_minutes;
  uint8_t num_zones;
  uint16_t zone_ids[10];          // Physical zone IDs (hardware pin mapping)
  char zone_names[10][32];        // Zone display names
};

namespace Areas {
  // Load areas configuration from JSON doc
  // Returns true if parsing succeeded
  bool loadAreasConfig(const JsonDocument& doc, AreaConfig* areas, uint8_t maxAreas, uint8_t& outCount);

  // Find area by ID ("Grass", "Drip")
  AreaConfig* findAreaById(const char* areaId, AreaConfig* areas, uint8_t areaCount);

  // Get zone IDs for an area (for zone switching within that area)
  // Returns zone_ids array and count
  bool getAreaZoneIds(AreaConfig* area, uint16_t** outZoneIds, uint8_t& outCount);

  // Get zone name for display
  const char* getZoneName(AreaConfig* area, uint8_t zoneIdx);

  // Configure zone groups from area config, with fallback to legacy mode
  void configureAreaZones(const char* areaId, AreaConfig* areas, uint8_t areaCount);
}
