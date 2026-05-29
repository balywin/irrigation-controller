#include "zones.h"
#include "hw_config.h"
#include "main.h"

static ZoneRunConfig gGrassRun = {};
static ZoneRunConfig gDripRun = {};
static int8_t grassZoneIndex = 0;
static int8_t dripZoneIndex = 0;
static uint8_t controllerNumGrassZones = 6;
static uint8_t controllerNumDripZones = 3;

namespace Zones {

void init(uint8_t numGrassZones, uint8_t numDripZones) {
  controllerNumGrassZones = (numGrassZones > 6) ? 6 : numGrassZones;
  controllerNumDripZones = (numDripZones > 3) ? 3 : numDripZones;
  grassZoneIndex = 0;
  dripZoneIndex = 0;
  gGrassRun = {};
  gDripRun = {};
}

void changeGrassZone(int8_t step) {
  if (gGrassRun.count > 0) {
    int8_t ni = gGrassRun.activeIdx + step;
    if (ni < 0) ni = gGrassRun.count - 1;
    if (ni >= gGrassRun.count) ni = 0;
    gGrassRun.activeIdx = ni;
    applyGrassGroup(gGrassRun.activeIdx);
  } else {
    grassZoneIndex += step;
    if (grassZoneIndex >= controllerNumGrassZones) grassZoneIndex = 1;
    else if (grassZoneIndex < 1) grassZoneIndex = controllerNumGrassZones - 1;
  }
}

void changeDripZone(int8_t step) {
  if (gDripRun.count > 0) {
    int ni = static_cast<int>(gDripRun.activeIdx) + step;
    if (ni < 0) ni = static_cast<int>(gDripRun.count) - 1;
    if (ni >= static_cast<int>(gDripRun.count)) ni = 0;
    gDripRun.activeIdx = static_cast<uint8_t>(ni);
    applyDripGroup(gDripRun.activeIdx);
  } else {
    dripZoneIndex += step;
    if (dripZoneIndex >= controllerNumDripZones) dripZoneIndex = 1;
    else if (dripZoneIndex < 1) dripZoneIndex = controllerNumDripZones - 1;
  }
}

void applyGrassGroup(uint8_t groupIdx) {
  closeGrassValves();
  if (groupIdx >= gGrassRun.count) return;
  uint8_t sz = gGrassRun.sizes[groupIdx];
  for (uint8_t z = 0; z < sz; z++) {
    uint8_t id = gGrassRun.zoneIds[groupIdx][z];  // 1-based
    if (id >= 1 && id <= MAX_NUMBER_OF_GRASS_ZONES)
      setOutput(grassZones[id - 1], false);  // false = open (active-low relay)
  }
}

void applyDripGroup(uint8_t groupIdx) {
  closeDripValves();
  if (groupIdx >= gDripRun.count) return;
  uint8_t sz = gDripRun.sizes[groupIdx];
  for (uint8_t z = 0; z < sz; z++) {
    uint8_t id = gDripRun.zoneIds[groupIdx][z];  // 1-based
    if (id >= 1 && id <= MAX_NUMBER_OF_DRIP_ZONES && dripZones[id - 1] != 0)
      setOutput(dripZones[id - 1], false);
  }
}

const ZoneRunConfig& getGrassRunConfig() {
  return gGrassRun;
}

const ZoneRunConfig& getDripRunConfig() {
  return gDripRun;
}

void setGrassZoneGroups(const ZoneRunConfig& cfg) {
  gGrassRun = cfg;
}

void setDripZoneGroups(const ZoneRunConfig& cfg) {
  gDripRun = cfg;
}

int8_t getGrassZoneIndex() {
  return grassZoneIndex;
}

void setGrassZoneIndex(int8_t idx) {
  grassZoneIndex = idx;
}

}  // namespace Zones
