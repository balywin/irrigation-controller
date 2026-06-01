#include "websocket_protocol.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "main.h"
#include "file_utils.h"
#include "zones.h"

namespace {

AsyncWebSocket gWs("/ws");
unsigned long gLastHeartbeatMs = 0;
String gLastStatusPayload;
unsigned long gPauseUntilGrassMs  = 0;
unsigned long gPauseUntilDripMs   = 0;
unsigned long gPauseUntilFillingMs = 0;

// Index of the schedule card that fired the current run, or -1 if manually started.
int8_t gGrassScheduleActive = -1;
int8_t gDripScheduleActive  = -1;

// Per-area, per-schedule last-fired tracking (max 8 schedules per area).
// Stores epoch-minute-within-week: dow*24*60 + hour*60 + minute.
// Prevents re-firing the same schedule within the same minute.
uint32_t gLastFiredMin[3][8] = {{0}};  // index 2 = Filling

// Per-area schedule auto-disarm: when a manual run starts via WS, the global
// `[area].enabled` flag in schedule.json is set to false so a scheduled run
// cannot race the manual one. Only that single flag is written; per-schedule
// card data (zones, durations, per-card `enabled`, etc.) is never touched.
// The original value is stashed in /config/disarm.json so it survives a
// reboot mid-run and can be correctly restored when the run ends.
// The future schedule engine must also consult `armed` before firing.
struct SchedDisarmState {
  bool armed = false;
  bool original = true;
};
SchedDisarmState gDisarmGrass;
SchedDisarmState gDisarmDrip;
bool gPrevRunningGrass = false;
bool gPrevRunningDrip = false;
bool gFillingManuallyStarted = false;
bool gFillingScheduleActive  = false;
SchedDisarmState gDisarmFilling;
bool gPrevRunningFilling = false;

const char* DISARM_FILE = "/config/disarm.json";

// Resolve schedule.json key for an area: try exact (e.g. "Grass"), then lower
// ("grass"). Returns the key that actually exists, or the original on miss.
String resolveSchedKey(const char* areaId) {
  if (scheduleJson[areaId].is<JsonObject>()) return {areaId};
  String low = String(areaId);
  low.toLowerCase();
  if (scheduleJson[low.c_str()].is<JsonObject>()) return low;
  return {areaId};
}

// Parse a zones JSON array-of-arrays into a ZoneRunConfig.
ZoneRunConfig parseZoneGroups(JsonVariantConst zonesVar, uint32_t groupMs) {
  ZoneRunConfig cfg = {};
  cfg.groupMs = groupMs;
  JsonArrayConst arr = zonesVar.as<JsonArrayConst>();
  if (arr.isNull()) return cfg;
  for (JsonVariantConst group : arr) {
    if (cfg.count >= MAX_ZONE_GROUPS) break;
    uint8_t sz = 0;
    JsonArrayConst ga = group.as<JsonArrayConst>();
    if (!ga.isNull()) {
      for (JsonVariantConst z : ga) {
        if (sz >= MAX_ZONES_PER_GROUP) break;
        cfg.zoneIds[cfg.count][sz++] = z.as<uint8_t>();
      }
    }
    cfg.sizes[cfg.count++] = sz;
  }
  return cfg;
}

// Write the stash. Reads existing stash first to preserve other areas.
void writeDisarmStash(const char* areaId, bool originalValue) {
  JsonDocument doc;
  if (LittleFS.exists(DISARM_FILE)) loadJsonFile(doc, DISARM_FILE);
  doc[areaId] = originalValue;
  File f = LittleFS.open(DISARM_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

// Remove one area from stash.
void clearDisarmStash(const char* areaId) {
  JsonDocument doc;
  if (!loadJsonFile(doc, DISARM_FILE)) return;
  doc.remove(areaId);
  File f = LittleFS.open(DISARM_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
  if (doc.size() == 0) LittleFS.remove(DISARM_FILE);
}

// Set only [areaId].enabled in schedule.json; all other fields preserved.
// Uses case-insensitive key lookup so both "Grass" and "grass" work.
void writeScheduleGlobalEnabled(const char* areaId, bool value) {
  JsonDocument doc;
  if (!loadJsonFile(doc, "/config/schedule.json")) return;
  String key = resolveSchedKey(areaId);
  if (!doc[key.c_str()].is<JsonObject>()) return;
  doc[key.c_str()]["enabled"] = value;
  // Also keep scheduleJson in RAM in sync.
  scheduleJson[key.c_str()]["enabled"] = value;
  File f = LittleFS.open("/config/schedule.json", "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

void disarmAreaSchedule(const char* areaId, SchedDisarmState& st) {
  if (st.armed) return;
  String key = resolveSchedKey(areaId);
  if (scheduleJson[key.c_str()].is<JsonObject>()) {
    st.original = scheduleJson[key.c_str()]["enabled"] | true;
  } else {
    st.original = true;
  }
  st.armed = true;
  writeDisarmStash(areaId, st.original);
  if (st.original) writeScheduleGlobalEnabled(areaId, false);
}

void restoreAreaSchedule(const char* areaId, SchedDisarmState& st) {
  if (!st.armed) return;
  st.armed = false;
  writeScheduleGlobalEnabled(areaId, st.original);  // also updates scheduleJson RAM
  clearDisarmStash(areaId);
}

// On boot, check if a previous run left disarm.json (reboot mid-run).
// If so, restore the stashed values so schedule.json ends up consistent.
void recoverDisarmOnBoot() {
  JsonDocument doc;
  if (!loadJsonFile(doc, DISARM_FILE)) return;
  for (JsonPair kv : doc.as<JsonObject>()) {
    writeScheduleGlobalEnabled(kv.key().c_str(), kv.value().as<bool>());
  }
  LittleFS.remove(DISARM_FILE);
}

const unsigned long STATUS_HEARTBEAT_MS = 1000UL;
const unsigned long PAUSE_1H_MS = 3600000UL;

String makeIso8601FromNow(unsigned long targetMs) {
  if (targetMs <= millis()) {
    return "";
  }
  unsigned long sec = (targetMs - millis()) / 1000UL;
  // Firmware does not keep full wall clock in a single global place.
  // Expose ETA as relative seconds in an ISO-like duration wrapper.
  return String("PT") + sec + "S";
}

bool isPaused(unsigned long pauseUntilMs) {
  return pauseUntilMs > millis();
}

unsigned long* pauseSlotByTarget(const String& target) {
  String t = target;
  t.toLowerCase();
  if (t == "grass") return &gPauseUntilGrassMs;
  if (t == "drip") return &gPauseUntilDripMs;
  if (t == "filling") return &gPauseUntilFillingMs;
  return nullptr;
}

String normalizeAreaTarget(const String& target) {
  String t = target;
  t.toLowerCase();
  if (t == "grass") return "Grass";
  if (t == "drip") return "Drip";
  if (t == "filling") return "Filling";
  return "";
}

bool isAreaRunning(const String& area) {
  if (area == "Grass") return isGrassIrrigating();
  if (area == "Drip") return isDripIrrigating();
  return false;
}

bool isAreaManuallyStarted(const String& area) {
  if (area == "Grass") return isGrassIrrigating() && gDisarmGrass.armed;
  if (area == "Drip")  return isDripIrrigating()  && gDisarmDrip.armed;
  return false;
}

int8_t areaScheduleIndex(const String& area) {
  if (area == "Grass") return isGrassIrrigating() ? gGrassScheduleActive : -1;
  if (area == "Drip")  return isDripIrrigating()  ? gDripScheduleActive  : -1;
  return -1;
}

String buildStatusJson() {
  JsonDocument doc;
  doc["type"] = "status";
  doc["protocolVersion"] = 1;

  JsonObject data = doc["data"].to<JsonObject>();
  JsonObject device = data["device"].to<JsonObject>();
  device["uptime"] = millis() / 1000UL;
  device["firmware"] = FIRMWARE_VERSION;
  device["heap"] = ESP.getFreeHeap();

  JsonObject sensors = data["sensors"].to<JsonObject>();
  sensors["waterLevel"] = getWaterLevelPercent();
  sensors["tankPressure"] = static_cast<long>(getPressureRawValue());

  JsonObject areas = data["areas"].to<JsonObject>();
  const ZoneRunConfig& gr = Zones::getGrassRunConfig();
  JsonObject grass = areas["Grass"].to<JsonObject>();
  grass["running"]         = isGrassIrrigating();
  grass["manuallyStarted"] = isAreaManuallyStarted("Grass");
  grass["scheduleActive"]  = areaScheduleIndex("Grass");
  grass["pausedUntil"]     = isPaused(gPauseUntilGrassMs) ? makeIso8601FromNow(gPauseUntilGrassMs) : String();
  if (!isPaused(gPauseUntilGrassMs)) grass["pausedUntil"] = nullptr;
  grass["activeGroupIndex"]     = isGrassIrrigating() ? static_cast<int>(gr.activeIdx) : -1;
  grass["remainingSeconds"]     = static_cast<uint32_t>(getGrassRemainingMs() / 1000UL);
  grass["groupRemainingSeconds"] = static_cast<uint32_t>(getGrassGroupRemainingMs() / 1000UL);
  {
    JsonArray az = grass["activeZones"].to<JsonArray>();
    if (isGrassIrrigating()) {
      if (gr.count > 0 && gr.activeIdx < gr.count) {
        for (uint8_t z = 0; z < gr.sizes[gr.activeIdx]; z++)
          az.add(gr.zoneIds[gr.activeIdx][z]);
      } else if (getGrassZoneIndex() > 0) {
        az.add(getGrassZoneIndex());
      }
    }
  }

  const ZoneRunConfig& dr = Zones::getDripRunConfig();
  JsonObject drip = areas["Drip"].to<JsonObject>();
  drip["running"]         = isDripIrrigating();
  drip["manuallyStarted"] = isAreaManuallyStarted("Drip");
  drip["scheduleActive"]  = areaScheduleIndex("Drip");
  drip["pausedUntil"]     = isPaused(gPauseUntilDripMs) ? makeIso8601FromNow(gPauseUntilDripMs) : String();
  if (!isPaused(gPauseUntilDripMs)) drip["pausedUntil"] = nullptr;
  drip["activeGroupIndex"]      = isDripIrrigating() ? static_cast<int>(dr.activeIdx) : -1;
  drip["remainingSeconds"]      = static_cast<uint32_t>(getDripRemainingMs() / 1000UL);
  drip["groupRemainingSeconds"] = static_cast<uint32_t>(getDripGroupRemainingMs() / 1000UL);
  {
    JsonArray az = drip["activeZones"].to<JsonArray>();
    if (isDripIrrigating() && dr.count > 0 && dr.activeIdx < dr.count) {
      for (uint8_t z = 0; z < dr.sizes[dr.activeIdx]; z++)
        az.add(dr.zoneIds[dr.activeIdx][z]);
    }
  }

  JsonObject filling = data["filling"].to<JsonObject>();
  filling["running"] = isFillingActive();
  filling["manuallyStarted"] = gFillingManuallyStarted;
  filling["scheduleActive"]  = gFillingScheduleActive;
  filling["enabled"] = isFillingEnabled();
  if (isPaused(gPauseUntilFillingMs)) {
    filling["pausedUntil"] = makeIso8601FromNow(gPauseUntilFillingMs);
  } else {
    filling["pausedUntil"] = nullptr;
  }
  filling["remainingSeconds"] = static_cast<uint32_t>(getFillingRemainingMs() / 1000UL);

  JsonObject pumps = data["pumps"].to<JsonObject>();
  pumps["1"] = getPumpGrassActive();
  pumps["2"] = getPumpDripActive();
  pumps["3"] = getPumpWellActive();

  JsonObject valves = data["valves"].to<JsonObject>();
  valves["1"] = getGrassMainValveActive();
  valves["2"] = getDripMainValveActive();

  String out;
  serializeJson(doc, out);
  return out;
}

void sendError(AsyncWebSocketClient* client, const String& code, const String& message, const String& reqId = "") {
  JsonDocument doc;
  doc["type"] = "error";
  doc["code"] = code;
  doc["message"] = message;
  if (reqId.length()) doc["reqId"] = reqId;
  String out;
  serializeJson(doc, out);
  client->text(out);
}

void sendStatusToClient(AsyncWebSocketClient* client) {
  client->text(buildStatusJson());
}

void broadcastStatusIfChanged(bool force = false) {
  String payload = buildStatusJson();
  if (force || payload != gLastStatusPayload) {
    gLastStatusPayload = payload;
    gWs.textAll(payload);
  }
}

bool readFileToString(const String& path, String& out) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  out = f.readString();
  f.close();
  return true;
}

bool writeFileFromJson(const String& path, JsonVariantConst data) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  if (serializeJson(data, f) == 0) {
    f.close();
    return false;
  }
  f.close();
  return true;
}

bool resetConfigFromSample(const String& fileName) {
  String samplePath = "/config/samples/" + fileName;
  if (!LittleFS.exists(samplePath)) return false;
  File in = LittleFS.open(samplePath, "r");
  if (!in) return false;
  File out = LittleFS.open("/config/" + fileName, "w");
  if (!out) {
    in.close();
    return false;
  }
  uint8_t buf[256];
  while (in.available()) {
    size_t n = in.read(buf, sizeof(buf));
    if (n == 0) break;
    out.write(buf, n);
  }
  in.close();
  out.close();
  return true;
}

void sendConfig(AsyncWebSocketClient* client, const String& fileName, const String& rawJson, const String& reqId) {
  JsonDocument doc;
  doc["type"] = "config";
  doc["file"] = fileName;
  if (reqId.length()) doc["reqId"] = reqId;

  JsonDocument payload;
  DeserializationError err = deserializeJson(payload, rawJson);
  if (err) {
    sendError(client, "io_error", String("invalid json in ") + fileName, reqId);
    return;
  }
  doc["data"] = payload.as<JsonVariantConst>();

  String out;
  serializeJson(doc, out);
  client->text(out);
}

void sendConfigSaved(AsyncWebSocketClient* client, const String& fileName, bool ok, const String& reqId, const String& error = "") {
  JsonDocument doc;
  doc["type"] = "config_saved";
  doc["file"] = fileName;
  doc["ok"] = ok;
  if (reqId.length()) doc["reqId"] = reqId;
  if (!ok) doc["error"] = error;
  String out;
  serializeJson(doc, out);
  client->text(out);
}

// Fire a specific schedule entry immediately, bypassing time/day checks.
// Does not update gLastFiredMin so the time-triggered run still fires at the configured time.
bool fireSchedule(int8_t areaIdx, int8_t schedIdx, String& outCode, String& outMsg) {
  const char* areaId = (areaIdx == 0) ? "Grass" : "Drip";
  String key = resolveSchedKey(areaId);
  JsonVariantConst areaNode = scheduleJson[key.c_str()];
  if (!areaNode.is<JsonObject>()) {
    outCode = "bad_request"; outMsg = "area not in schedule.json"; return false;
  }
  JsonArrayConst schedules = areaNode["schedules"].as<JsonArrayConst>();
  if (schedules.isNull() || schedIdx >= schedules.size()) {
    outCode = "bad_request"; outMsg = "schedule index out of range"; return false;
  }
  JsonVariantConst sched = schedules[schedIdx];
  bool schedEnabled = sched["enabled"] | true;
  if (!schedEnabled) {
    outCode = "conflict"; outMsg = "schedule is disabled"; return false;
  }
  uint32_t durMin = sched["durationMinutes"] | 20;
  ZoneRunConfig cfg = parseZoneGroups(sched["zones"], durMin * 60000UL);
  if (cfg.count == 0) {
    outCode = "bad_request"; outMsg = "no zones configured in schedule"; return false;
  }
  if (areaIdx == 0) {
    Zones::setGrassZoneGroups(cfg);
    gGrassScheduleActive = static_cast<int8_t>(schedIdx);
    startGrassIrrigation();
  } else {
    Zones::setDripZoneGroups(cfg);
    gDripScheduleActive = static_cast<int8_t>(schedIdx);
    startDripIrrigation();
  }
  Serial.printf("[schedule] %s schedule %d manually fired\n", areaId, schedIdx);
  return true;
}

bool handleCommand(const String& action, const String& target, JsonVariantConst extras, String& outCode, String& outMsg) {
  String norm = normalizeAreaTarget(target);
  if (!norm.length()) {
    outCode = "unknown_target";
    outMsg = "unknown target";
    return false;
  }

  unsigned long* pauseRef = pauseSlotByTarget(norm);
  if (!pauseRef) {
    outCode = "unknown_target";
    outMsg = "unknown target";
    return false;
  }

  if (action == "start") {
    if (isPaused(*pauseRef)) {
      outCode = "conflict";
      outMsg = "target is paused";
      return false;
    }
    uint32_t durMin = extras["durationMinutes"] | 0;
    if (norm == "Grass") {
      uint32_t gMs = durMin > 0 ? durMin * 60000UL : controllerConfig.grassMaxMinutes * 60000UL;
      ZoneRunConfig cfg = parseZoneGroups(extras["zones"], gMs);
      Zones::setGrassZoneGroups(cfg);
      gGrassScheduleActive = -1;
      disarmAreaSchedule("Grass", gDisarmGrass);
      startGrassIrrigation();
    } else if (norm == "Drip") {
      uint32_t gMs = durMin > 0 ? durMin * 60000UL : controllerConfig.dripMaxMinutes * 60000UL;
      ZoneRunConfig cfg = parseZoneGroups(extras["zones"], gMs);
      Zones::setDripZoneGroups(cfg);
      gDripScheduleActive = -1;
      disarmAreaSchedule("Drip", gDisarmDrip);
      startDripIrrigation();
    } else {
      if (durMin > 0) fillingMaxMs = durMin * 60000UL;
      else fillingMaxMs = controllerConfig.fillingMaxMinutes * 60000UL;
      gFillingManuallyStarted = true;
      gFillingScheduleActive  = false;
      disarmAreaSchedule("Filling", gDisarmFilling);
      startFilling();
      if (!isFillingActive()) {
        gFillingManuallyStarted = false;
        restoreAreaSchedule("Filling", gDisarmFilling);
        outCode = "conflict"; outMsg = "Tank full"; return false;
      }
    }
    return true;
  }

  if (action == "stop") {
    *pauseRef = 0;
    if (norm == "Grass") { stopGrassIrrigation(); restoreAreaSchedule("Grass", gDisarmGrass); gGrassScheduleActive = -1; }
    else if (norm == "Drip") { stopDripIrrigation(); restoreAreaSchedule("Drip", gDisarmDrip); gDripScheduleActive = -1; }
    else {
      stopFilling();
      gFillingManuallyStarted = false;
      gFillingScheduleActive  = false;
      restoreAreaSchedule("Filling", gDisarmFilling);
    }
    return true;
  }

  if (action == "pause_1h") {
    *pauseRef = millis() + PAUSE_1H_MS;
    if (norm == "Grass") { stopGrassIrrigation(); restoreAreaSchedule("Grass", gDisarmGrass); gGrassScheduleActive = -1; }
    else if (norm == "Drip") { stopDripIrrigation(); restoreAreaSchedule("Drip", gDisarmDrip); gDripScheduleActive = -1; }
    else {
      stopFilling();
      gFillingManuallyStarted = false;
      gFillingScheduleActive  = false;
      restoreAreaSchedule("Filling", gDisarmFilling);
    }
    return true;
  }

  if (action == "run_schedule") {
    if (norm == "Filling") {
      outCode = "bad_request"; outMsg = "run_schedule not supported for Filling"; return false;
    }
    if (isPaused(*pauseRef)) {
      outCode = "conflict"; outMsg = "target is paused"; return false;
    }
    if (isAreaRunning(norm)) {
      outCode = "conflict"; outMsg = "target already running"; return false;
    }
    int si = extras["scheduleIndex"] | -1;
    if (si < 0 || si >= 8) {
      outCode = "bad_request"; outMsg = "scheduleIndex missing or out of range"; return false;
    }
    return fireSchedule((norm == "Grass") ? 0 : 1, si, outCode, outMsg);
  }

  if (action == "set_group") {
    int gi = extras["groupIndex"] | -1;
    if (norm == "Grass") {
      if (!isGrassIrrigating()) { outCode = "conflict"; outMsg = "Grass not running"; return false; }
      if (gi < 0 || gi >= (int)Zones::getGrassRunConfig().count) { outCode = "bad_request"; outMsg = "groupIndex out of range"; return false; }
      if (!extras["zones"].isNull()) {
        ZoneRunConfig cfg = Zones::getGrassRunConfig();
        JsonArrayConst zones = extras["zones"].as<JsonArrayConst>();
        uint8_t sz = 0;
        for (JsonVariantConst z : zones) {
          auto id = z.as<uint8_t>();
          if (sz < MAX_ZONES_PER_GROUP && id >= 1) cfg.zoneIds[gi][sz++] = id;
        }
        cfg.sizes[gi] = sz;
        Zones::setGrassZoneGroups(cfg);
      }
      switchGrassGroup((uint8_t)gi);
    } else if (norm == "Drip") {
      if (!isDripIrrigating()) { outCode = "conflict"; outMsg = "Drip not running"; return false; }
      if (gi < 0 || gi >= (int)Zones::getDripRunConfig().count) { outCode = "bad_request"; outMsg = "groupIndex out of range"; return false; }
      if (!extras["zones"].isNull()) {
        ZoneRunConfig cfg = Zones::getDripRunConfig();
        JsonArrayConst zones = extras["zones"].as<JsonArrayConst>();
        uint8_t sz = 0;
        for (JsonVariantConst z : zones) {
          auto id = z.as<uint8_t>();
          if (sz < MAX_ZONES_PER_GROUP && id >= 1) cfg.zoneIds[gi][sz++] = id;
        }
        cfg.sizes[gi] = sz;
        Zones::setDripZoneGroups(cfg);
      }
      switchDripGroup((uint8_t)gi);
    } else {
      outCode = "bad_request"; outMsg = "set_group only for Grass/Drip"; return false;
    }
    return true;
  }

  outCode = "bad_request";
  outMsg = "unknown action";
  return false;
}

// Check and fire scheduled irrigation runs. Called once per second from
// websocketProtocolLoop. Relies on wall-clock time from getRtcTime().
// Schedule JSON keys may be capitalized ("Grass") or lowercase ("grass").
void checkSchedules() {
  uint8_t h, m, dow;
  if (!getRtcTime(h, m, dow)) return;
  // dow: 0=Sun, 1=Mon…6=Sat. Schedule uses 1=Mon…7=Sun.
  uint8_t schedDow = (dow == 0) ? 7 : dow;
  // Unique minute within the week; prevents re-firing the same schedule.
  uint32_t epochMin = (uint32_t)dow * 24 * 60 + h * 60 + m;

  const char* areaIds[]    = {"Grass", "Drip"};
  unsigned long* pauseRefs[] = {&gPauseUntilGrassMs, &gPauseUntilDripMs};

  for (int8_t ai = 0; ai < 2; ai++) {
    const char* areaId = areaIds[ai];
    if (isPaused(*pauseRefs[ai])) continue;

    // Area must not already be running
    bool running = (ai == 0) ? isGrassIrrigating() : isDripIrrigating();
    if (running) continue;

    String key = resolveSchedKey(areaId);
    JsonVariantConst area = scheduleJson[key.c_str()];
    if (!area.is<JsonObject>()) continue;

    bool globalEnabled = area["enabled"] | true;
    if (!globalEnabled) continue;

    JsonArrayConst schedules = area["schedules"].as<JsonArrayConst>();
    if (schedules.isNull()) continue;

    int si = 0;
    for (JsonVariantConst sched : schedules) {
      if (si >= 8) break;
      bool schedEnabled = sched["enabled"] | true;
      if (!schedEnabled) { si++; continue; }

      // Day-of-week check
      bool dayMatch = false;
      JsonArrayConst days = sched["daysOfWeek"].as<JsonArrayConst>();
      for (JsonVariantConst d : days) {
        if (d.as<uint8_t>() == schedDow) { dayMatch = true; break; }
      }
      if (!dayMatch) { si++; continue; }

      // Fixed start-time check (HH:MM)
      bool timeMatch = false;
      JsonArrayConst times = sched["startTimes"].as<JsonArrayConst>();
      for (JsonVariantConst t : times) {
        const char* ts = t.as<const char*>();
        if (!ts) continue;
        uint8_t th = 0, tm = 0;
        if (sscanf(ts, "%hhu:%hhu", &th, &tm) == 2 && th == h && tm == m) {
          timeMatch = true; break;
        }
      }
      // Note: sunriseSchedule / sunsetSchedule not yet implemented.

      if (!timeMatch) { si++; continue; }

      // Avoid re-firing the same (area, schedule, minute)
      if (gLastFiredMin[ai][si] == epochMin) { si++; continue; }

      // Build zone-group config from schedule entry
      uint32_t durMin = sched["durationMinutes"] | 20;
      ZoneRunConfig cfg = parseZoneGroups(sched["zones"], durMin * 60000UL);
      if (cfg.count == 0) { si++; continue; }  // no zones — skip

      gLastFiredMin[ai][si] = epochMin;
      if (ai == 0) {
        Zones::setGrassZoneGroups(cfg);
        gGrassScheduleActive = static_cast<int8_t>(si);
        startGrassIrrigation();
        Serial.printf("[schedule] Grass schedule %d fired at %02u:%02u\n", si, h, m);
      } else {
        Zones::setDripZoneGroups(cfg);
        gDripScheduleActive = static_cast<int8_t>(si);
        startDripIrrigation();
        Serial.printf("[schedule] Drip schedule %d fired at %02u:%02u\n", si, h, m);
      }
      break;  // one schedule fires per area per check
    }
  }

  // Filling schedule block (no zones — just duration + startFilling)
  if (!isPaused(gPauseUntilFillingMs) && !isFillingActive()) {
    String fillingKey = resolveSchedKey("Filling");
    JsonVariantConst fillingArea = scheduleJson[fillingKey.c_str()];
    if (fillingArea.is<JsonObject>()) {
      bool globalEnabled = fillingArea["enabled"] | true;
      if (globalEnabled) {
        JsonArrayConst schedules = fillingArea["schedules"].as<JsonArrayConst>();
        if (!schedules.isNull()) {
          int si = 0;
          for (JsonVariantConst sched : schedules) {
            if (si >= 8) break;
            bool schedEnabled = sched["enabled"] | true;
            if (!schedEnabled) { si++; continue; }

            bool dayMatch = false;
            JsonArrayConst days = sched["daysOfWeek"].as<JsonArrayConst>();
            for (JsonVariantConst d : days) {
              if (d.as<uint8_t>() == schedDow) { dayMatch = true; break; }
            }
            if (!dayMatch) { si++; continue; }

            bool timeMatch = false;
            JsonArrayConst times = sched["startTimes"].as<JsonArrayConst>();
            for (JsonVariantConst t : times) {
              const char* ts = t.as<const char*>();
              if (!ts) continue;
              uint8_t th = 0, tm = 0;
              if (sscanf(ts, "%hhu:%hhu", &th, &tm) == 2 && th == h && tm == m) {
                timeMatch = true; break;
              }
            }
            // Note: sunriseSchedule / sunsetSchedule not yet implemented.
            if (!timeMatch) { si++; continue; }

            if (gLastFiredMin[2][si] == epochMin) { si++; continue; }

            uint32_t durMin = sched["durationMinutes"] | 15;
            gLastFiredMin[2][si] = epochMin;
            fillingMaxMs = durMin * 60000UL;
            gFillingScheduleActive  = true;
            gFillingManuallyStarted = false;
            startFilling();
            Serial.printf("[schedule] Filling schedule %d fired at %02u:%02u\n", si, h, m);
            break;
          }
        }
      }
    }
  }
}

void handleMessage(AsyncWebSocketClient* client, const uint8_t* payload, size_t len) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    sendError(client, "bad_request", "invalid json");
    return;
  }

  const String type = doc["type"] | "";
  const String reqId = doc["reqId"] | "";

  if (type == "ping") {
    JsonDocument pong;
    pong["type"] = "pong";
    if (!doc["ts"].isNull()) pong["ts"] = doc["ts"];
    String out;
    serializeJson(pong, out);
    client->text(out);
    return;
  }

  if (type == "command") {
    String action = doc["action"] | "";
    String target = doc["target"] | "";
    String errorCode;
    String errorMsg;
    if (!handleCommand(action, target, doc.as<JsonVariantConst>(), errorCode, errorMsg)) {
      sendError(client, errorCode, errorMsg, reqId);
      return;
    }
    broadcastStatusIfChanged(true);
    return;
  }

  if (type == "get_config") {
    String fileName = doc["file"] | "";
    if (!fileName.endsWith(".json")) {
      sendError(client, "bad_request", "file must be json", reqId);
      return;
    }
    String raw;
    if (!readFileToString("/config/" + fileName, raw)) {
      sendError(client, "io_error", "config not found", reqId);
      return;
    }
    sendConfig(client, fileName, raw, reqId);
    return;
  }

  if (type == "save_config") {
    String fileName = doc["file"] | "";
    if (!fileName.endsWith(".json") || doc["data"].isNull()) {
      sendError(client, "bad_request", "file or data missing", reqId);
      return;
    }
    if (!writeFileFromJson("/config/" + fileName, doc["data"])) {
      sendConfigSaved(client, fileName, false, reqId, "write failed");
      return;
    }
    if (fileName == "schedule.json") loadJsonFile(scheduleJson, "/config/schedule.json");
    sendConfigSaved(client, fileName, true, reqId);
    return;
  }

  if (type == "reset_config") {
    String fileName = doc["file"] | "";
    if (!fileName.endsWith(".json")) {
      sendError(client, "bad_request", "file must be json", reqId);
      return;
    }
    if (!resetConfigFromSample(fileName)) {
      sendConfigSaved(client, fileName, false, reqId, "sample not found");
      return;
    }
    sendConfigSaved(client, fileName, true, reqId);
    return;
  }

  // Forward compatibility: ignore unknown message types.
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  (void)server;
  if (type == WS_EVT_CONNECT) {
    sendStatusToClient(client);
    return;
  }
  if (type == WS_EVT_DATA) {
    auto* info = reinterpret_cast<AwsFrameInfo*>(arg);
    if (!info || info->opcode != WS_TEXT) return;
    if (!info->final || info->index != 0 || info->len != len) return;
    handleMessage(client, data, len);
    return;
  }
}

}  // namespace

void armAreaManualStart(const char* area) {
  if (strcmp(area, "Grass") == 0) { gGrassScheduleActive = -1; disarmAreaSchedule("Grass", gDisarmGrass); }
  else if (strcmp(area, "Drip") == 0) { gDripScheduleActive = -1; disarmAreaSchedule("Drip", gDisarmDrip); }
}

void restoreAreaManualStop(const char* area) {
  if (strcmp(area, "Grass") == 0) { restoreAreaSchedule("Grass", gDisarmGrass); gGrassScheduleActive = -1; }
  else if (strcmp(area, "Drip") == 0) { restoreAreaSchedule("Drip", gDisarmDrip); gDripScheduleActive = -1; }
}

void websocketNotifyHardwareCommand(const char* action, const char* target, int zone) {
  JsonDocument doc;
  doc["type"] = "event";
  doc["event"] = "hardware_command";
  doc["source"] = "button";
  doc["action"] = action ? action : "";
  doc["target"] = target ? target : "";
  if (zone >= 0) doc["zone"] = zone;

  String out;
  serializeJson(doc, out);
  gWs.textAll(out);
  broadcastStatusIfChanged(true);
}

void setupWebSocketProtocol(AsyncWebServer& server) {
  recoverDisarmOnBoot();
  gWs.onEvent(onWsEvent);
  server.addHandler(&gWs);
}

void websocketProtocolLoop() {
  // Expire pause timers.
  if (!isPaused(gPauseUntilGrassMs))  gPauseUntilGrassMs  = 0;
  if (!isPaused(gPauseUntilDripMs))   gPauseUntilDripMs   = 0;
  if (!isPaused(gPauseUntilFillingMs)) gPauseUntilFillingMs = 0;

  // Detect natural end of irrigation (timer expiry, hardware button, low water, etc.)
  // and restore schedule.enabled + clear schedule-active flag.
  bool curG = isGrassIrrigating();
  if (gPrevRunningGrass && !curG) {
    restoreAreaSchedule("Grass", gDisarmGrass);
    gGrassScheduleActive = -1;
  }
  gPrevRunningGrass = curG;

  bool curD = isDripIrrigating();
  if (gPrevRunningDrip && !curD) {
    restoreAreaSchedule("Drip", gDisarmDrip);
    gDripScheduleActive = -1;
  }
  gPrevRunningDrip = curD;

  bool curF = isFillingActive();
  if (gPrevRunningFilling && !curF) {
    restoreAreaSchedule("Filling", gDisarmFilling);
    gFillingScheduleActive  = false;
    gFillingManuallyStarted = false;
  }
  gPrevRunningFilling = curF;

  gWs.cleanupClients();

  unsigned long now = millis();
  if (now - gLastHeartbeatMs >= STATUS_HEARTBEAT_MS) {
    gLastHeartbeatMs = now;
    checkSchedules();
    broadcastStatusIfChanged();
  }
}
