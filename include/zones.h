#pragma once

#include "main.h"

namespace Zones {
  // Initialize zone management (call from setup)
  void init(uint8_t numGrassZones, uint8_t numDripZones);

  // Zone switching
  void changeGrassZone(int8_t step);
  void changeDripZone(int8_t step);

  // Apply zone group to hardware (open correct valve)
  void applyGrassGroup(uint8_t groupIdx);
  void applyDripGroup(uint8_t groupIdx);

  // Get current zone configuration
  const ZoneRunConfig& getGrassRunConfig();
  const ZoneRunConfig& getDripRunConfig();

  // Set zone run configuration (from areas config or schedule)
  void setGrassZoneGroups(const ZoneRunConfig& cfg);
  void setDripZoneGroups(const ZoneRunConfig& cfg);

  // Current zone indices (legacy flat mode)
  int8_t getGrassZoneIndex();
  void setGrassZoneIndex(int8_t idx);
}
