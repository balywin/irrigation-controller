# WebSocket Protocol

Implemented real-time JSON protocol between firmware and web UI.

- **Endpoint:** `ws://<device>/ws` (or `wss://` behind TLS proxy)
- **Encoding:** UTF-8 JSON, one message per frame
- **Version:** `protocolVersion: 1` in `status`
- **Compatibility rule:** unknown message `type` values are ignored by server

## Implemented Client → Server messages

### `command`

```json
{ "type": "command", "action": "start", "target": "Grass", "durationMinutes": 2, "zoneGroupCount": 4, "reqId": "c1" }
```

- `action`: `start` | `stop` | `pause_1h`
- `target`: `Grass` | `Drip` | `Filling` (case-insensitive)
- `durationMinutes` (optional, `start` only): per-zone-group duration. For Grass/Drip the firmware computes the total run length as `durationMinutes * zoneGroupCount`; for Filling it is the total filling cap. Omit (or 0) to use the configured default from `app_config.json`.
- `zoneGroupCount` (optional, `start` only, areas only): number of zone groups defined for the manual run. Combined with `durationMinutes` this drives the per-group switch interval and the total run length.
- `reqId` (optional): echoed in `error` responses

Behavior:
- `start`: starts requested subsystem (blocked while paused by `pause_1h`)
  - **Schedule auto-disarm**: for areas (`Grass`/`Drip`), firmware reads `schedule.json[<area>].enabled`, stashes it in `/config/disarm.json`, then writes only `schedule.json[<area>].enabled = false`. Per-card schedule data is never touched. On stop/natural-end, the original value is restored and the stash entry removed. On next boot, any remaining stash entries are recovered and restored.
  - **Per-run duration override**: when the command carries `durationMinutes` (and for Grass/Drip also `zoneGroupCount`), the firmware overrides the in-memory `<area>MaxMs` (and for Grass `grassGroupSwitchMs`) for this run. The override is RAM-only and is reset to the `app_config.json` defaults the next time `applyConfig()` runs. Without these fields, the configured defaults apply.
- `stop`: stops requested subsystem, clears an active pause, clears the RAM `armed` flag for the area
- `pause_1h`: stops subsystem and blocks `start` for one hour; clears the RAM `armed` flag for the area

### `ping`

```json
{ "type": "ping", "ts": 1730000000000 }
```

Server replies with `pong` (echoes `ts` when provided).

### `get_config`

```json
{ "type": "get_config", "file": "schedule.json", "reqId": "cfg1" }
```

- Reads `/config/<file>` from LittleFS.
- `file` must end with `.json`.

### `save_config`

```json
{ "type": "save_config", "file": "manual_control.json", "data": { "...": "..." }, "reqId": "cfg2" }
```

- Writes `data` JSON to `/config/<file>` in LittleFS.
- No schema validation yet (write-only validation at JSON serialization level).

### `reset_config`

```json
{ "type": "reset_config", "file": "app_config.json", "reqId": "cfg3" }
```

- Resets `/config/<file>` by copying `/config/samples/<file>` when sample exists in LittleFS.

## Implemented Server → Client messages

### `status`

Sent:
- once immediately on connect (full snapshot)
- whenever state changes
- at heartbeat interval (1s), if changed since last broadcast

```json
{
  "type": "status",
  "protocolVersion": 1,
  "data": {
    "device": { "uptime": 1234, "firmware": "0.1.0", "heap": 214512 },
    "sensors": { "waterLevel": 75, "tankPressure": 100200 },
    "areas": {
      "Grass": { "running": true, "manuallyStarted": true, "scheduleActive": false, "pausedUntil": null, "zone": 2, "activeGroupIndex": 2, "remainingSeconds": 935, "groupRemainingSeconds": 47 },
      "Drip": { "running": false, "manuallyStarted": false, "scheduleActive": false, "pausedUntil": null, "zone": null, "activeGroupIndex": -1, "remainingSeconds": 0, "groupRemainingSeconds": 0 }
    },
    "filling": { "running": false, "manuallyStarted": false, "scheduleActive": false, "enabled": true, "pausedUntil": null, "remainingSeconds": 0 },
    "pumps": { "1": true, "2": false, "3": false },
    "valves": { "1": true, "2": false }
  }
}
```

Notes:
- `pausedUntil` is currently an ISO-8601 duration-like string (`PT<n>S`) or `null`.
- `scheduleActive` is currently always `false` (schedule WS integration pending).
- `activeZones` is the array of zone IDs currently energized for the area's running group. While idle the array is empty. Multi-zone groups (e.g. `[1, 3]`) report all members simultaneously. Used by the UI to:
  1. show energized zone numbers inside the area's emergency-stop button,
  2. identify the active group for `Shuffle now` to reposition it without disturbing the running cycle (matches against `manual_control.json` `zones` group entries by sorted zone IDs).
- `remainingSeconds` (per area) is the firmware-side countdown until the irrigation auto-stops at `<area>MaxMinutes`. `0` when the area is not running. Used by the UI to show the time-left next to the state inside the emergency button.
- `groupRemainingSeconds` (per area) is the time until the next zone-group switch (`maxMinutes / (numberOfZones - 1)` for Grass; always `0` for Drip — it has no per-group switching backend-side). Used by the UI to show the per-group countdown left of the state in the emergency button.
- `activeGroupIndex` (per area) is the 0-based zone-cycling index (`grass_zone_index`) while running, or `-1` when not running. Used by the Manual Control zone-group Sequence editor to highlight (pulsing orange outline) the chip whose group is currently being fired by the backend.
- `filling.remainingSeconds` is the firmware-side countdown until filling auto-stops at `fillingMaxMinutes`. `0` when not running.

### `pong`

```json
{ "type": "pong", "ts": 1730000000000 }
```

### `error`

```json
{ "type": "error", "code": "conflict", "message": "target is paused", "reqId": "c1" }
```

Current error codes:
- `bad_request`
- `unknown_target`
- `conflict`
- `io_error`

### `config`

Response to `get_config`:

```json
{ "type": "config", "file": "schedule.json", "data": { "...": "..." }, "reqId": "cfg1" }
```

### `config_saved`

Response to `save_config` and `reset_config`:

```json
{ "type": "config_saved", "file": "manual_control.json", "ok": true, "reqId": "cfg2" }
```

Failure example:

```json
{ "type": "config_saved", "file": "manual_control.json", "ok": false, "error": "sample not found", "reqId": "cfg3" }
```

### `event` (`hardware_command`)

Firmware sends this when a physical hardware button changes irrigation state.

```json
{ "type": "event", "event": "hardware_command", "source": "button", "action": "start", "target": "Grass" }
```

Examples:

```json
{ "type": "event", "event": "hardware_command", "source": "button", "action": "stop", "target": "Filling" }
```

```json
{ "type": "event", "event": "hardware_command", "source": "button", "action": "zone_next", "target": "Grass", "zone": 2 }
```

## Reserved / Not Yet Implemented

The following protocol items are reserved for future expansion and currently not emitted/processed by firmware:

- `subscribe` topic filtering
- schedule-specific status fields (`currentScheduleIndex`)
- strict schema validation for `save_config`
- backend reload of `manual_control.json` after `save_config`. The UI's edits to `zones` (groups), `shuffle`, `durationMinutes`, `delayedStart` only take effect at the next firmware boot unless a preset save explicitly persists them. `Shuffle now` and the auto-shuffle on stop are UI-only reorderings that do not feed back into the running firmware cycle.
