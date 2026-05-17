# Live Zone Group Switching

**Date:** 2026-05-22  
**Status:** Approved

## Goal

While grass/drip irrigation is running, allow the user to click a zone group chip in the ManualTab Sequence row to immediately switch the firmware to that group. Zone edits (add/remove zones from active group) also apply immediately. Per-group timers preserve remaining time across manual switches.

---

## Architecture

### Per-Group Timer Tracking

Add `groupElapsedMs[MAX_ZONE_GROUPS]` to `ZoneRunConfig`. Tracks accumulated ms for each group across manual switches so a group resumes with its remaining time when revisited.

**Switch logic:**
1. Save elapsed for current group: `groupElapsedMs[old] += millis() - lastTimeGrassZoneSwitched`
2. Set `activeIdx = newIdx`
3. Backdate start time: `lastTimeGrassZoneSwitched = millis() - groupElapsedMs[newIdx]`
4. Call `applyGrassGroup(newIdx)`

This makes `millis() - lastTimeGrassZoneSwitched` naturally yield the correct total elapsed for any group without changing the timer check logic.

The auto-advance in the main loop must also save elapsed before moving to the next group (currently it doesn't).

### New WS Command: `set_group`

```json
{ "type": "command", "action": "set_group", "target": "Grass", "groupIndex": 2 }
```

Optional `zones` array — when present, updates zone IDs for the target group before switching/applying:

```json
{ "type": "command", "action": "set_group", "target": "Grass", "groupIndex": 0, "zones": [1, 3] }
```

Constraints:
- `groupIndex` must be valid (< `gGrassRun.count`)
- Only valid while area is running; returns `conflict` error otherwise
- Works for both Grass and Drip

---

## Components

### `include/main.h`
- Add `uint32_t groupElapsedMs[MAX_ZONE_GROUPS]` to `ZoneRunConfig`
- Declare `void switchGrassGroup(uint8_t newIdx)` and `void switchDripGroup(uint8_t newIdx)`

### `src/main.cpp`
- `switchGrassGroup(newIdx)`: saves elapsed for old group, switches `activeIdx`, backdates `lastTimeGrassZoneSwitched`, calls `applyGrassGroup`
- `switchDripGroup(newIdx)`: same for drip
- Auto-advance in main loop: call `switchGrassGroup(activeIdx + 1)` instead of bare `activeIdx++` so elapsed is saved before moving on
- `startGrassIrrigation` / `startDripIrrigation`: zero-initialize `groupElapsedMs`

### `src/websocket_protocol.cpp`
- Add `set_group` branch in `handleCommand`:
  - Parse `groupIndex` and optional `zones`
  - If `zones` present: update `gGrassRun.zoneIds[groupIndex]` / `sizes[groupIndex]`
  - Call `switchGrassGroup` / `switchDripGroup`
  - Broadcast updated status

### `ui/src/tabs/ManualTab.svelte`
- **Chip click during irrigation**: send `set_group` command; do NOT update `currentGroupIdxs` locally — wait for WS status to confirm (existing `$effect` at line 319 already syncs UI to firmware `activeZones`)
- **Zone toggle for active group during irrigation**: after local state update, send `set_group` with `groupIndex = curIdx` and updated zones
- **Visual**: chip matching firmware `activeZones` gets `fw-active` class (pulsing outline) — derived from existing `activeGrpIdx`-style logic already in AreaTab, applied in ManualTab

### `preview.html`
- Sync visual change: active chip in Sequence row shows pulsing outline when irrigation running

---

## Data Flow

```
User clicks chip gi  →  sendCommand('set_group', areaId, { groupIndex: gi })
  →  WS → firmware switchGrassGroup(gi)
    →  valves updated  →  WS status broadcast
      →  UI $effect syncs currentGroupIdxs to firmware activeZones
        →  chip gi renders fw-active style
```

```
User toggles zone z (active group, running)
  →  toggleZone() updates local groups[curIdx]
  →  sendCommand('set_group', areaId, { groupIndex: curIdx, zones: updatedZones })
    →  firmware updates zoneIds + reapplies group  →  WS broadcast
```

---

## Out of Scope

- Clicking non-active group chips to edit zone membership (only active group gets live edit during run; others edit normally and apply on next switch)
- Group reordering (drag) during active irrigation
