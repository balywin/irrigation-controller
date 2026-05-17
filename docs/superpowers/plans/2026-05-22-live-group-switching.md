# Live Zone Group Switching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** While grass/drip irrigation is running, clicking a zone group chip in ManualTab immediately switches the firmware to that group; editing zones in the active group also syncs immediately; per-group timers resume from saved elapsed time when revisited.

**Architecture:** Add `groupElapsedMs[]` to `ZoneRunConfig` for per-group timer tracking. New WS command `set_group` handles both group switching and optional zone update. UI sends the command on chip click (when running) and zone toggle (when editing the firmware-active group); chip shows a pulsing outline when the firmware is running that group.

**Tech Stack:** C++ (PlatformIO/Arduino), Svelte 5, ArduinoJson, AsyncWebSocket

---

## File Map

| File | Change |
|------|--------|
| `include/main.h` | Add `groupElapsedMs[]` to struct; declare switch functions |
| `src/main.cpp` | Implement `switchGrassGroup` / `switchDripGroup` |
| `src/websocket_protocol.cpp` | Add `set_group` command handler |
| `ui/src/assets/app.css` | Add `.active-group` chip pulse style |
| `ui/src/tabs/ManualTab.svelte` | Chip click → WS command; zone toggle → WS command; visual fw-active state |
| `preview.html` | Chip click sends command in running state (mock) |

---

### Task 1: Add per-group timer tracking to `ZoneRunConfig`

**Files:**
- Modify: `include/main.h` (lines 43–49, struct body)

**Context:** `ZoneRunConfig` currently has no per-group elapsed time. We add `groupElapsedMs[MAX_ZONE_GROUPS]` — filled by `switchGrassGroup`/`switchDripGroup` before switching away from a group, read back when returning to it. The field zero-initialises automatically whenever a `ZoneRunConfig` is assigned from `parseZoneGroups` (which returns a local struct).

- [ ] **Step 1: Add field to struct**

In `include/main.h`, replace the struct definition:

```cpp
struct ZoneRunConfig {
    uint8_t  count;                                    // number of groups
    uint8_t  activeIdx;                                // currently-running group (0-based)
    uint32_t groupMs;                                  // ms allotted per group
    uint8_t  sizes[MAX_ZONE_GROUPS];                   // zones in each group
    uint8_t  zoneIds[MAX_ZONE_GROUPS][MAX_ZONES_PER_GROUP]; // 1-based zone IDs
    uint32_t groupElapsedMs[MAX_ZONE_GROUPS];          // accumulated ms per group (manual switches)
};
```

- [ ] **Step 2: Declare switch functions in `include/main.h`**

After the existing declarations (`getGrassRemainingMs`, etc.) add:

```cpp
void switchGrassGroup(uint8_t newIdx);
void switchDripGroup(uint8_t newIdx);
```

- [ ] **Step 3: Verify build compiles**

```bash
pio run 2>&1 | tail -5
```

Expected: no errors (struct is larger but zero-init works the same).

- [ ] **Step 4: Commit**

```bash
git add include/main.h
git commit -m "feat: add groupElapsedMs to ZoneRunConfig, declare switch fns"
```

---

### Task 2: Implement `switchGrassGroup` and `switchDripGroup`

**Files:**
- Modify: `src/main.cpp` — add two functions after `applyDripGroup` (around line 358)

**Context:** `lastTimeGrassZoneSwitched` tracks when the current group started. To save elapsed: `groupElapsedMs[old] += millis() - lastTimeGrassZoneSwitched`. To restore: set `lastTimeGrassZoneSwitched = millis() - groupElapsedMs[newIdx]` so that `millis() - lastTimeGrassZoneSwitched` equals the already-accumulated time for the new group. Elapsed is capped at `groupMs` so a group that previously ran to full time doesn't go negative.

- [ ] **Step 1: Implement `switchGrassGroup` in `src/main.cpp`**

Insert after the closing `}` of `applyDripGroup` (≈ line 358):

```cpp
void switchGrassGroup(uint8_t newIdx) {
  if (newIdx >= gGrassRun.count) return;
  uint32_t elapsed = millis() - lastTimeGrassZoneSwitched;
  if (elapsed > gGrassRun.groupMs) elapsed = gGrassRun.groupMs;
  gGrassRun.groupElapsedMs[gGrassRun.activeIdx] += elapsed;
  gGrassRun.activeIdx = newIdx;
  lastTimeGrassZoneSwitched = millis() - gGrassRun.groupElapsedMs[newIdx];
  applyGrassGroup(newIdx);
}

void switchDripGroup(uint8_t newIdx) {
  if (newIdx >= gDripRun.count) return;
  uint32_t elapsed = millis() - lastTimeDripZoneSwitched;
  if (elapsed > gDripRun.groupMs) elapsed = gDripRun.groupMs;
  gDripRun.groupElapsedMs[gDripRun.activeIdx] += elapsed;
  gDripRun.activeIdx = newIdx;
  lastTimeDripZoneSwitched = millis() - gDripRun.groupElapsedMs[newIdx];
  applyDripGroup(newIdx);
}
```

- [ ] **Step 2: Verify build compiles**

```bash
pio run 2>&1 | tail -5
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: implement switchGrassGroup / switchDripGroup with per-group elapsed tracking"
```

---

### Task 3: Add `set_group` WebSocket command

**Files:**
- Modify: `src/websocket_protocol.cpp` — add case in `handleCommand` before the final `outCode = "bad_request"` (≈ line 483)

**Context:** The command accepts `groupIndex` (required) and optional `zones` array. If `zones` is present, it updates `zoneIds`/`sizes` for that group before switching — this handles live zone editing of the active group. Returns `conflict` if area is not running, `bad_request` if index out of range.

- [ ] **Step 1: Add `set_group` handler in `handleCommand`**

In `src/websocket_protocol.cpp`, insert before `outCode = "bad_request"; outMsg = "unknown action"; return false;`:

```cpp
  if (action == "set_group") {
    int gi = extras["groupIndex"] | -1;
    if (norm == "Grass") {
      if (!isGrassIrrigating()) { outCode = "conflict"; outMsg = "Grass not running"; return false; }
      if (gi < 0 || gi >= (int)gGrassRun.count) { outCode = "bad_request"; outMsg = "groupIndex out of range"; return false; }
      if (!extras["zones"].isNull()) {
        JsonArrayConst zones = extras["zones"].as<JsonArrayConst>();
        uint8_t sz = 0;
        for (JsonVariantConst z : zones) {
          uint8_t id = z.as<uint8_t>();
          if (sz < MAX_ZONES_PER_GROUP && id >= 1) gGrassRun.zoneIds[gi][sz++] = id;
        }
        gGrassRun.sizes[gi] = sz;
      }
      switchGrassGroup((uint8_t)gi);
    } else if (norm == "Drip") {
      if (!isDripIrrigating()) { outCode = "conflict"; outMsg = "Drip not running"; return false; }
      if (gi < 0 || gi >= (int)gDripRun.count) { outCode = "bad_request"; outMsg = "groupIndex out of range"; return false; }
      if (!extras["zones"].isNull()) {
        JsonArrayConst zones = extras["zones"].as<JsonArrayConst>();
        uint8_t sz = 0;
        for (JsonVariantConst z : zones) {
          uint8_t id = z.as<uint8_t>();
          if (sz < MAX_ZONES_PER_GROUP && id >= 1) gDripRun.zoneIds[gi][sz++] = id;
        }
        gDripRun.sizes[gi] = sz;
      }
      switchDripGroup((uint8_t)gi);
    } else {
      outCode = "bad_request"; outMsg = "set_group only for Grass/Drip"; return false;
    }
    return true;
  }
```

- [ ] **Step 2: Verify build compiles**

```bash
pio run 2>&1 | tail -5
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/websocket_protocol.cpp
git commit -m "feat: add set_group WS command for live zone group switching"
```

---

### Task 4: Add active-group chip CSS

**Files:**
- Modify: `ui/src/assets/app.css` — append at end

**Context:** preview.html already has `.btn.active-group` with orange pulse. We mirror it in app.css so ManualTab can use the same class. Color is `currentColor` so it inherits the button's area color.

- [ ] **Step 1: Append CSS to `ui/src/assets/app.css`**

Add at the very end of the file:

```css
/* Sequence chip — matches firmware active group while irrigation running */
.btn.active-group {
  outline: 2px solid currentColor;
  outline-offset: 2px;
  animation: active-group-pulse 1.4s ease-in-out infinite;
}
@keyframes active-group-pulse {
  0%, 100% { box-shadow: 0 0 0 3px color-mix(in srgb, currentColor 40%, transparent); }
  50%       { box-shadow: 0 0 0 5px color-mix(in srgb, currentColor 55%, transparent); }
}
```

- [ ] **Step 2: Commit**

```bash
git add ui/src/assets/app.css
git commit -m "style: add active-group chip pulse for running irrigation"
```

---

### Task 5: ManualTab — chip click and zone toggle send WS commands

**Files:**
- Modify: `ui/src/tabs/ManualTab.svelte`

**Context:**

**Chip click (Sequence row, ≈ line 542):**  
Currently: `onclick={() => currentGroupIdxs[area.id] = gi}`  
When irrigation is running, clicking a chip should send `set_group` instead. Do NOT update `currentGroupIdxs` locally — let the existing `$effect` at line 319 sync it from the firmware's `activeZones` WS status once the command is confirmed.

**Zone toggle (`toggleZone` function, ≈ line 56):**  
After updating local state, if the area is running AND the user is editing the firmware-active group (detected via `ws.status.areas[areaId].activeGroupIndex`), send `set_group` with the updated zone list.

**Visual (Sequence row chip):**  
Derive `fwGroupIdx` from `ws.status?.areas?.[area.id]?.activeGroupIndex ?? -1`. Add `active-group` class to the chip when `gi === fwGroupIdx && running`.

- [ ] **Step 1: Update `toggleZone` to sync live during irrigation**

In `ManualTab.svelte`, replace the `toggleZone` function body:

```js
function toggleZone(areaId, z) {
  const ac = areaConfigs[areaId];
  if (ac.zones.length === 0) return;
  const cur = currentGroupIdxs[areaId] ?? 0;
  const g = [...(ac.zones[cur] ?? [])];
  const idx = g.indexOf(z);
  if (idx >= 0) g.splice(idx, 1);
  else { g.push(z); g.sort((a, b) => a - b); }
  const arr = ac.zones.map((og, i) => i === cur ? g : og);
  setGroups(areaId, arr);

  const running = ws.status?.areas?.[areaId]?.running ?? false;
  const fwActive = ws.status?.areas?.[areaId]?.activeGroupIndex ?? -1;
  if (running && fwActive === cur) {
    sendCommand('set_group', areaId, { groupIndex: cur, zones: g });
  }
}
```

- [ ] **Step 2: Update chip onclick and add `active-group` class**

In `ManualTab.svelte`, find the `{#each ac.zones as g, gi}` block in the Sequence row (≈ line 532). Replace the chip button with:

```svelte
{@const fwGroupIdx = running ? (ws.status?.areas?.[area.id]?.activeGroupIndex ?? -1) : -1}
{#each ac.zones as g, gi}
  <button type="button" class="btn btn-sm"
    class:btn-primary={gi === curIdx}
    class:btn-secondary={gi !== curIdx}
    class:active-group={gi === fwGroupIdx}
    style="font-family:monospace;min-width:2rem;cursor:grab;"
    draggable="true"
    ondragstart={(e) => onChipDragStart(area.id, gi, e)}
    ondragover={(e) => onChipDragOver(area.id, e)}
    ondrop={(e) => onChipDrop(area.id, gi, e)}
    ondragend={onChipDragEnd}
    onclick={() => {
      if (running) {
        sendCommand('set_group', area.id, { groupIndex: gi });
      } else {
        currentGroupIdxs[area.id] = gi;
      }
    }}
    title={gi === fwGroupIdx ? `Group ${gi + 1} — running (click to switch)` : `Group ${gi + 1} — drag to reorder`}
  >{g.join('') || '·'}</button>
{/each}
```

Note: the `{@const fwGroupIdx ...}` line must be placed inside the `{#if ac}` block but outside the `{#each ac.zones}` — put it just before the `{#each ac.zones as g, gi}` in the Sequence field div.

- [ ] **Step 3: Verify build**

```bash
cd ui && npm run build 2>&1 | tail -10
```

Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add ui/src/tabs/ManualTab.svelte
git commit -m "feat: chip click and zone toggle send set_group WS command during irrigation"
```

---

### Task 6: Sync `preview.html`

**Files:**
- Modify: `preview.html` (≈ line 434)

**Context:** preview.html already renders `active-group` class via `isFiring`. The only change needed is chip click behaviour: when the area is "active" in the mock state, clicking a different chip should update `currentGroupIdx` AND simulate a group switch (update `MOCK_ACTIVE_ZONES`). Since preview is a static mock, we just update `currentGroupIdx` and re-render; no actual WS command is sent.

- [ ] **Step 1: Update chip click in `renderGroups` to show switch behavior**

In `preview.html`, find the chip `click` listener (≈ line 434):

```js
b.addEventListener('click', () => { currentGroupIdx = gi; renderGroups(); renderZones(); });
```

Replace with:

```js
b.addEventListener('click', () => {
  currentGroupIdx = gi;
  // When running, mock a firmware switch by updating active zones
  if (state.eState[area.id] === 'active') {
    if (!window.MOCK_ACTIVE_ZONES) window.MOCK_ACTIVE_ZONES = {};
    MOCK_ACTIVE_ZONES[area.id] = [...(groups[gi] ?? [])];
  }
  renderGroups(); renderZones();
});
```

- [ ] **Step 2: Verify preview.html opens in browser without JS errors**

Open `preview.html` directly in browser, check console for errors.

- [ ] **Step 3: Commit**

```bash
git add preview.html
git commit -m "chore: preview.html chip click simulates group switch when running"
```

---

### Task 7: Flash and verify on device

- [ ] **Step 1: Build and upload**

```bash
pio run --target upload 2>&1 | tail -10
```

- [ ] **Step 2: Start grass irrigation via UI, verify**

1. Open UI → Manual tab → Grass section
2. Start irrigation with ≥ 2 zone groups  
3. Confirm chip for active group shows pulsing outline
4. Click a different group chip — OLED and UI should switch immediately
5. Wait ~30 s, click back to first group — verify it resumes with less time remaining (not full duration)
6. Toggle a zone on/off in the active group — verify chip label updates and valves respond

- [ ] **Step 3: Verify drip works the same**

Repeat step 2 for Drip area.
