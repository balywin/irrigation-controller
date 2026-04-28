#include "websocket_protocol.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "main.h"
#include "file_utils.h"

namespace {

AsyncWebSocket gWs("/ws");
unsigned long gLastHeartbeatMs = 0;
String gLastStatusPayload;
unsigned long gPauseUntilGrassMs = 0;
unsigned long gPauseUntilDripMs = 0;
unsigned long gPauseUntilFillingMs = 0;

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

static const char* DISARM_FILE = "/config/disarm.json";

// Write the stash. Reads existing stash first to preserve other areas.
static void writeDisarmStash(const char* areaId, bool originalValue) {
  JsonDocument doc;
  if (LittleFS.exists(DISARM_FILE)) loadJsonFile(doc, DISARM_FILE);
  doc[areaId] = originalValue;
  File f = LittleFS.open(DISARM_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

// Remove one area from stash.
static void clearDisarmStash(const char* areaId) {
  JsonDocument doc;
  if (!loadJsonFile(doc, DISARM_FILE)) return;
  doc.remove(areaId);
  File f = LittleFS.open(DISARM_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
  if (doc.size() == 0) LittleFS.remove(DISARM_FILE);
}

// Set only [areaId].enabled in schedule.json; all other fields preserved.
static void writeScheduleGlobalEnabled(const char* areaId, bool value) {
  JsonDocument doc;
  if (!loadJsonFile(doc, "/config/schedule.json")) return;
  if (!doc[areaId].is<JsonObject>()) return;
  doc[areaId]["enabled"] = value;
  File f = LittleFS.open("/config/schedule.json", "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

void disarmAreaSchedule(const char* areaId, SchedDisarmState& st) {
  if (st.armed) return;
  // Read original before overwriting.
  JsonDocument doc;
  if (loadJsonFile(doc, "/config/schedule.json") && doc[areaId].is<JsonObject>()) {
    st.original = doc[areaId]["enabled"] | true;
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
  writeScheduleGlobalEnabled(areaId, st.original);
  clearDisarmStash(areaId);
}

// On boot, check if a previous run left disarm.json (reboot mid-run).
// If so, restore the stashed values so schedule.json ends up consistent.
static void recoverDisarmOnBoot() {
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
  // Until schedule engine is integrated, running means manually started.
  return isAreaRunning(area);
}

String buildStatusJson() {
  DynamicJsonDocument doc(2048);
  doc["type"] = "status";
  doc["protocolVersion"] = 1;

  JsonObject data = doc.createNestedObject("data");
  JsonObject device = data.createNestedObject("device");
  device["uptime"] = millis() / 1000UL;
  device["firmware"] = FIRMWARE_VERSION;
  device["heap"] = ESP.getFreeHeap();

  JsonObject sensors = data.createNestedObject("sensors");
  sensors["waterLevel"] = getWaterLevelPercent();
  sensors["tankPressure"] = static_cast<long>(getPressureRawValue());

  JsonObject areas = data.createNestedObject("areas");
  JsonObject grass = areas.createNestedObject("Grass");
  grass["running"] = isGrassIrrigating();
  grass["manuallyStarted"] = isAreaManuallyStarted("Grass");
  grass["scheduleActive"] = false;
  if (isPaused(gPauseUntilGrassMs)) {
    grass["pausedUntil"] = makeIso8601FromNow(gPauseUntilGrassMs);
  } else {
    grass["pausedUntil"] = nullptr;
  }
  grass["zone"] = isGrassIrrigating() ? static_cast<int>(getGrassZoneIndex()) : -1;
  grass["activeGroupIndex"] = isGrassIrrigating() ? static_cast<int>(getGrassZoneIndex()) : -1;
  grass["remainingSeconds"] = static_cast<uint32_t>(getGrassRemainingMs() / 1000UL);
  grass["groupRemainingSeconds"] = static_cast<uint32_t>(getGrassGroupRemainingMs() / 1000UL);

  JsonObject drip = areas.createNestedObject("Drip");
  drip["running"] = isDripIrrigating();
  drip["manuallyStarted"] = isAreaManuallyStarted("Drip");
  drip["scheduleActive"] = false;
  if (isPaused(gPauseUntilDripMs)) {
    drip["pausedUntil"] = makeIso8601FromNow(gPauseUntilDripMs);
  } else {
    drip["pausedUntil"] = nullptr;
  }
  drip["zone"] = nullptr;
  drip["activeGroupIndex"] = -1;
  drip["remainingSeconds"] = static_cast<uint32_t>(getDripRemainingMs() / 1000UL);
  drip["groupRemainingSeconds"] = 0;

  JsonObject filling = data.createNestedObject("filling");
  filling["running"] = isFillingActive();
  filling["manuallyStarted"] = isFillingActive();
  filling["scheduleActive"] = false;
  filling["enabled"] = isFillingEnabled();
  if (isPaused(gPauseUntilFillingMs)) {
    filling["pausedUntil"] = makeIso8601FromNow(gPauseUntilFillingMs);
  } else {
    filling["pausedUntil"] = nullptr;
  }
  filling["remainingSeconds"] = static_cast<uint32_t>(getFillingRemainingMs() / 1000UL);

  JsonObject pumps = data.createNestedObject("pumps");
  pumps["1"] = getPumpGrassActive();
  pumps["2"] = getPumpDripActive();
  pumps["3"] = getPumpWellActive();

  JsonObject valves = data.createNestedObject("valves");
  valves["1"] = getGrassMainValveActive();
  valves["2"] = getDripMainValveActive();

  String out;
  serializeJson(doc, out);
  return out;
}

void sendError(AsyncWebSocketClient* client, const String& code, const String& message, const String& reqId = "") {
  DynamicJsonDocument doc(384);
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
  DynamicJsonDocument doc(8192);
  doc["type"] = "config";
  doc["file"] = fileName;
  if (reqId.length()) doc["reqId"] = reqId;

  DynamicJsonDocument payload(6144);
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
  DynamicJsonDocument doc(512);
  doc["type"] = "config_saved";
  doc["file"] = fileName;
  doc["ok"] = ok;
  if (reqId.length()) doc["reqId"] = reqId;
  if (!ok) doc["error"] = error;
  String out;
  serializeJson(doc, out);
  client->text(out);
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
    // Apply per-run duration overrides from the start command. UI sends
    // durationMinutes (per zone group) and zoneGroupCount; total run is
    // duration * count. Without these, default config values apply.
    uint32_t durMin = extras["durationMinutes"] | 0;
    uint32_t groupCount = extras["zoneGroupCount"] | 0;
    if (norm == "Grass") {
      if (durMin > 0 && groupCount > 0) {
        grassMaxMs = durMin * groupCount * 60000UL;
        grassGroupSwitchMs = durMin * 60000UL;
      } else {
        grassMaxMs = controllerConfig.grassMaxMinutes * 60000UL;
        grassGroupSwitchMs = 0;
      }
      disarmAreaSchedule("Grass", gDisarmGrass);
      startGrassIrrigation();
    } else if (norm == "Drip") {
      if (durMin > 0 && groupCount > 0) {
        dripMaxMs = durMin * groupCount * 60000UL;
      } else {
        dripMaxMs = controllerConfig.dripMaxMinutes * 60000UL;
      }
      disarmAreaSchedule("Drip", gDisarmDrip);
      startDripIrrigation();
    } else {
      if (durMin > 0) fillingMaxMs = durMin * 60000UL;
      else fillingMaxMs = controllerConfig.fillingMaxMinutes * 60000UL;
      startFilling();
    }
    return true;
  }

  if (action == "stop") {
    *pauseRef = 0;
    if (norm == "Grass") { stopGrassIrrigation(); restoreAreaSchedule("Grass", gDisarmGrass); }
    else if (norm == "Drip") { stopDripIrrigation(); restoreAreaSchedule("Drip", gDisarmDrip); }
    else stopFilling();
    return true;
  }

  if (action == "pause_1h") {
    *pauseRef = millis() + PAUSE_1H_MS;
    if (norm == "Grass") { stopGrassIrrigation(); restoreAreaSchedule("Grass", gDisarmGrass); }
    else if (norm == "Drip") { stopDripIrrigation(); restoreAreaSchedule("Drip", gDisarmDrip); }
    else stopFilling();
    return true;
  }

  outCode = "bad_request";
  outMsg = "unknown action";
  return false;
}

void handleMessage(AsyncWebSocketClient* client, const uint8_t* payload, size_t len) {
  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    sendError(client, "bad_request", "invalid json");
    return;
  }

  const String type = doc["type"] | "";
  const String reqId = doc["reqId"] | "";

  if (type == "ping") {
    DynamicJsonDocument pong(128);
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
    AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);
    if (!info || info->opcode != WS_TEXT) return;
    if (!info->final || info->index != 0 || info->len != len) return;
    handleMessage(client, data, len);
    return;
  }
}

}  // namespace

void websocketNotifyHardwareCommand(const char* action, const char* target, int zone) {
  DynamicJsonDocument doc(512);
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
  if (!isPaused(gPauseUntilGrassMs)) gPauseUntilGrassMs = 0;
  if (!isPaused(gPauseUntilDripMs)) gPauseUntilDripMs = 0;
  if (!isPaused(gPauseUntilFillingMs)) gPauseUntilFillingMs = 0;

  // Detect natural end of a manual run (timer-driven, hardware button, etc.)
  // and restore the area's schedule.enabled if we had disarmed it.
  bool curG = isGrassIrrigating();
  if (gPrevRunningGrass && !curG) restoreAreaSchedule("Grass", gDisarmGrass);
  gPrevRunningGrass = curG;

  bool curD = isDripIrrigating();
  if (gPrevRunningDrip && !curD) restoreAreaSchedule("Drip", gDisarmDrip);
  gPrevRunningDrip = curD;

  gWs.cleanupClients();

  unsigned long now = millis();
  if (now - gLastHeartbeatMs >= STATUS_HEARTBEAT_MS) {
    gLastHeartbeatMs = now;
    broadcastStatusIfChanged();
  }
}
