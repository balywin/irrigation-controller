#include "areas.h"
#include <cstring>

namespace Areas {

bool loadAreasConfig(const JsonDocument& doc, AreaConfig* areas, uint8_t maxAreas, uint8_t& outCount) {
  if (!doc["areas"].is<JsonArrayConst>()) {
    outCount = 0;
    return true;  // areas optional
  }

  JsonArrayConst areasArr = doc["areas"].as<JsonArrayConst>();
  outCount = 0;

  for (JsonVariantConst areaVar : areasArr) {
    if (outCount >= maxAreas) break;

    AreaConfig& area = areas[outCount];

    // Parse area properties
    const char* id = areaVar["id"].as<const char*>();
    if (id) {
      strncpy(area.id, id, sizeof(area.id) - 1);
      area.id[sizeof(area.id) - 1] = '\0';
    } else {
      area.id[0] = '\0';
    }

    area.enabled = areaVar["enabled"].as<bool>();
    area.pump_id = areaVar["pump_id"].as<uint8_t>();
    area.main_valve_id = areaVar["main_valve_id"].as<uint8_t>();
    area.pump_start_delay_seconds = areaVar["pump_start_delay_seconds"].as<uint16_t>();
    area.max_minutes = areaVar["max_minutes"].as<uint16_t>();

    // Parse zone_ids array
    JsonArrayConst zoneIds = areaVar["zone_ids"].as<JsonArrayConst>();
    area.num_zones = 0;
    for (JsonVariantConst zoneVar : zoneIds) {
      if (area.num_zones >= 10) break;
      area.zone_ids[area.num_zones] = zoneVar.as<uint16_t>();
      area.num_zones++;
    }

    // Parse zone_names array
    JsonArrayConst zoneNames = areaVar["zone_names"].as<JsonArrayConst>();
    for (uint8_t i = 0; i < area.num_zones && i < 10; i++) {
      const char* name = zoneNames[i].as<const char*>();
      if (name) {
        strncpy(area.zone_names[i], name, sizeof(area.zone_names[i]) - 1);
        area.zone_names[i][sizeof(area.zone_names[i]) - 1] = '\0';
      } else {
        area.zone_names[i][0] = '\0';
      }
    }

    outCount++;
  }

  return true;
}

AreaConfig* findAreaById(const char* areaId, AreaConfig* areas, uint8_t areaCount) {
  if (!areaId) return nullptr;
  for (uint8_t i = 0; i < areaCount; i++) {
    if (strcmp(areas[i].id, areaId) == 0) {
      return &areas[i];
    }
  }
  return nullptr;
}

bool getAreaZoneIds(AreaConfig* area, uint16_t** outZoneIds, uint8_t& outCount) {
  if (!area) return false;
  *outZoneIds = area->zone_ids;
  outCount = area->num_zones;
  return true;
}

const char* getZoneName(AreaConfig* area, uint8_t zoneIdx) {
  if (!area || zoneIdx >= area->num_zones) return "";
  return area->zone_names[zoneIdx];
}

void configureAreaZones(const char* areaId, AreaConfig* areas, uint8_t areaCount) {
  bool isGrass = strcmp(areaId, "grass") == 0;
  AreaConfig* area = findAreaById(areaId, areas, areaCount);
  if (area && area->enabled && area->num_zones > 0) {
    ZoneRunConfig zonesCfg = {};
    zonesCfg.count = area->num_zones;
    zonesCfg.activeIdx = 0;
    zonesCfg.groupMs = (area->max_minutes * 60000UL) / area->num_zones;
    for (uint8_t i = 0; i < area->num_zones; i++) {
      zonesCfg.sizes[i] = 1;
      zonesCfg.zoneIds[i][0] = area->zone_ids[i];
      zonesCfg.groupElapsedMs[i] = 0;
    }
    if (isGrass) {
      Zones::setGrassZoneGroups(zonesCfg);
      Serial.print("Grass zones configured: ");
    } else {
      Zones::setDripZoneGroups(zonesCfg);
      Serial.print("Drip zones configured: ");
    }
    Serial.println(area->num_zones);
  } else {
    if (isGrass) { Zones::setGrassZoneGroups({}); Serial.print("Grass"); }
            else { Zones::setDripZoneGroups( {}); Serial.print("Drip");  }

    if (!area) Serial.println(" area not found");
    else if (!area->enabled) Serial.println(" area not enabled");
    else Serial.println(" area has no defined");
  }
}

}  // namespace Areas
