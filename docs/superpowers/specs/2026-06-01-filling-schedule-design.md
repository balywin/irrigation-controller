# Filling Schedule — Design Spec

**Date:** 2026-06-01  
**Status:** Approved

## Overview

Three related sub-features:
1. **Fill Schedule tab** — time-based schedule for automatic tank filling, UI + firmware execution.
2. **Manual-start disarms schedule** — manual filling temporarily disables the schedule, restores on stop.
3. **Configurable manual fill duration** — preset chips + free input in ManualTab, persisted in `manual_control.json`.

## Data Model

### schedule.json — new "Filling" key

```json
{
  "Filling": {
    "enabled": true,
    "schedules": [
      {
        "enabled": true,
        "durationMinutes": 15,
        "startTimes": ["07:00"],
        "daysOfWeek": [1, 2, 3, 4, 5, 6, 7],
        "sunriseSchedule": { "enabled": false, "sunriseOffsetMinutes": 0 },
        "sunsetSchedule":  { "enabled": false, "sunsetOffsetMinutes": 0 }
      }
    ]
  },
  "Grass": { "..." : "..." },
  "Drip":  { "..." : "..." }
}
```

No `zones` field — filling has no zone groups.

### manual_control.json — new "Filling" key

```json
{
  "Filling": { "durationMinutes": 15 },
  "Grass": { "..." : "..." },
  "Drip":  { "..." : "..." }
}
```

Default duration: 15 minutes.

## Backend (websocket_protocol.cpp)

### New state variables

```cpp
bool gFillingManuallyStarted = false;
bool gFillingScheduleActive  = false;
SchedDisarmState gDisarmFilling;
bool gPrevRunningFilling = false;
```

### handleCommand — "start" Filling

1. Set `gFillingManuallyStarted = true`, `gFillingScheduleActive = false`.
2. Call `disarmAreaSchedule("Filling", gDisarmFilling)` — stashes `schedule.json["Filling"]["enabled"]`, writes `false`.
3. Call `startFilling()`.
4. If `!isFillingActive()` (tank full conflict): clear state, call `restoreAreaSchedule("Filling", gDisarmFilling)`, return error.

### handleCommand — "stop" / "pause_1h" Filling

Call `stopFilling()`, then `restoreAreaSchedule("Filling", gDisarmFilling)`, clear both flags.

### checkSchedules() — Filling block

Added after Grass/Drip loop. Logic:
- Skip if `isPaused(gPauseUntilFillingMs)` or `isFillingActive()`.
- Read `scheduleJson["Filling"]`; skip if missing or `enabled == false`.
- Iterate schedules (max 8); match `daysOfWeek`, `startTimes` same as areas. Dedup via `gLastFiredMin[2][si]` — use index 2 for Filling.
- On match: `gFillingScheduleActive = true`, `gFillingManuallyStarted = false`, set `fillingMaxMs = durMin * 60000UL`, call `startFilling()`.
- sunriseSchedule / sunsetSchedule: parsed and stored for future implementation (same note as areas: "not yet implemented").

### websocketProtocolLoop() — natural stop detection

```cpp
bool curF = isFillingActive();
if (gPrevRunningFilling && !curF) {
  restoreAreaSchedule("Filling", gDisarmFilling);
  gFillingScheduleActive  = false;
  gFillingManuallyStarted = false;
}
gPrevRunningFilling = curF;
```

### Status payload fix

```cpp
filling["manuallyStarted"] = gFillingManuallyStarted;  // was: isFillingActive()
filling["scheduleActive"]  = gFillingScheduleActive;   // was: false (hardcoded)
```

`recoverDisarmOnBoot()` requires no change — already generic over all keys in `disarm.json`.

### gLastFiredMin expansion

Array currently `[2][8]`. Expand to `[3][8]` where index 2 = Filling.

## UI

### New: FillScheduleCard.svelte

Derived from ScheduleCard with zone-specific sections removed:
- **Removed:** "Zones in group", "Sequence" sections; Start button; `zoneIds`, `zoneNames`, `areaRunning`, `areaPaused`, `onstart` props; all zone/group state and logic.
- **Kept:** enabled toggle, Delete button, duration presets+input, days of week, start times (add/remove), sunrise/sunset offset toggles.
- Title: `"Filling Schedule {index + 1}"`.
- Duration presets: `[5, 10, 15, 20, 30, 45, 60, 90]` (same as ScheduleCard).

Props: `{ schedule, index, ondelete }`.

### New: FillScheduleTab.svelte

Mirrors AreaTab with suspend options removed. Structure:
- On mount: `getConfig('schedule.json')`, extract `["Filling"]` section. Load `sectionEnabled`, `schedules[]`.
- State: `sectionEnabled`, `schedules`, `saving`, `saveMsg`, `saveError`, `loadError`.
- Render: section enabled toggle, list of `FillScheduleCard`, save bar with `+ Add Schedule` + `Save`.
- `addSchedule()`: push default entry (same defaults as AreaTab).
- `save()`: write back full `schedule.json` with `Filling` key merged alongside existing keys.
- `$effect`: watch `ws.status?.filling?.manuallyStarted` transition to `false` (manual run ended) → reload `sectionEnabled` from disk, same pattern as AreaTab.

No `run_schedule` support — no Start button on cards.

### Modified: App.svelte

- Import `FillScheduleTab`.
- New nav tab "Fill Sched" (or "Filling") placed after area tabs, before Settings.
- Schedule-active dot on tab: `ws.status?.filling?.scheduleActive` (now boolean).
- Render `FillScheduleTab` when `activeTab === 'fill_schedule'`.

### Modified: ManualTab.svelte (filling card only)

- On mount: read `raw["Filling"]?.durationMinutes ?? 15` into `fillingDuration` state. Store loaded value in `loadedFillingDuration`.
- `fillingDurationDirty` derived: `fillingDuration !== loadedFillingDuration`.
- Filling card gets a duration row below existing controls: preset chips `[5, 10, 15, 20]` + `<input type="number">`. Same styling as area duration rows.
- `toggleFilling()` sends `durationMinutes: fillingDuration` (replaces `fillingCfg.max_minutes`).
- On start: if `fillingDurationDirty`, save `manual_control.json` with Filling key included before sending command.

### Modified: preview.html

Sync all above changes: new "Fill Sched" tab, FillScheduleTab layout, duration picker in filling card.

## File Changes Summary

| File | Change |
|---|---|
| `src/websocket_protocol.cpp` | New state vars, disarm/restore for Filling, checkSchedules Filling block, natural stop detection, status fix, gLastFiredMin[3][8] |
| `ui/src/tabs/FillScheduleCard.svelte` | New file |
| `ui/src/tabs/FillScheduleTab.svelte` | New file |
| `ui/src/tabs/ManualTab.svelte` | Filling duration persist + picker |
| `ui/src/App.svelte` | New tab |
| `preview.html` | Sync with UI changes |

`data/config/samples/` — not modified.
