# Filling Schedule Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Fill Schedule tab, a manual fill duration picker (persisted), and schedule-disarm-on-manual-start for the filling subsystem.

**Architecture:** Three independent sub-features wired together: (1) firmware `websocket_protocol.cpp` gains Filling schedule execution and disarm/restore; (2) two new Svelte components (`FillScheduleCard`, `FillScheduleTab`) mirror the area schedule pattern without zones; (3) `ManualTab` gains a persisted duration picker for filling.

**Tech Stack:** C++ (ArduinoJson, PlatformIO), Svelte 5 (runes), LittleFS JSON config, WebSocket protocol.

---

### Task 1: Add Filling key to manual_control.sample.json

**Files:**
- Modify: `data/config/samples/manual_control.sample.json`

- [ ] **Step 1: Add Filling key**

Open `data/config/samples/manual_control.sample.json` and add `"Filling"` as the first key:

```json
{
    "Filling": {
        "durationMinutes": 15
    },
    "Grass": {
        "enabled": true,
        "zones": [[2, 3], [1, 4], [2, 5]],
        "shuffle": true,
        "durationMinutes": 20,
        "delayedStart": "0m",
        "presets": [
            { "name": "Morning quick", "zones": [[1], [2]], "shuffle": false, "durationMinutes": 10, "delayedStart": "0m" },
            { "name": "Evening full",  "zones": [[1, 2], [3, 4], [5]], "shuffle": true, "durationMinutes": 30, "delayedStart": "1h" }
        ]
    },
    "Drip": {
        "enabled": true,
        "zones": [[1, 3], [2, 4], [5, 2]],
        "shuffle": true,
        "durationMinutes": 60,
        "delayedStart": "0m",
        "presets": []
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add data/config/samples/manual_control.sample.json
git commit -m "feat: add Filling duration to manual_control sample"
```

---

### Task 2: Backend — new state variables, status fix, gLastFiredMin expansion

**Files:**
- Modify: `src/websocket_protocol.cpp`

- [ ] **Step 1: Expand gLastFiredMin and add Filling state vars**

In `src/websocket_protocol.cpp`, find line 27:
```cpp
uint32_t gLastFiredMin[2][8] = {{0}};
```
Change to:
```cpp
uint32_t gLastFiredMin[3][8] = {{0}};  // index 2 = Filling
```

Then find lines 42-43 (after `gPrevRunningDrip = false;`):
```cpp
bool gPrevRunningGrass = false;
bool gPrevRunningDrip = false;
```
Add four new lines immediately after:
```cpp
bool gPrevRunningGrass = false;
bool gPrevRunningDrip = false;
bool gFillingManuallyStarted = false;
bool gFillingScheduleActive  = false;
SchedDisarmState gDisarmFilling;
bool gPrevRunningFilling = false;
```

- [ ] **Step 2: Fix status payload for filling**

Find lines 253-254:
```cpp
  filling["manuallyStarted"] = isFillingActive();
  filling["scheduleActive"] = false;
```
Replace with:
```cpp
  filling["manuallyStarted"] = gFillingManuallyStarted;
  filling["scheduleActive"]  = gFillingScheduleActive;
```

- [ ] **Step 3: Commit**

```bash
git add src/websocket_protocol.cpp
git commit -m "feat: filling schedule state vars and status payload fix"
```

---

### Task 3: Backend — handleCommand disarm/restore for Filling

**Files:**
- Modify: `src/websocket_protocol.cpp`

- [ ] **Step 1: Update start action for Filling**

Find lines 443-448 (inside `handleCommand`, the `else` branch of the area chain):
```cpp
    } else {
      if (durMin > 0) fillingMaxMs = durMin * 60000UL;
      else fillingMaxMs = controllerConfig.fillingMaxMinutes * 60000UL;
      startFilling();
      if (!isFillingActive()) { outCode = "conflict"; outMsg = "Tank full"; return false; }
    }
```
Replace with:
```cpp
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
```

- [ ] **Step 2: Update stop action for Filling**

Find line 456 (inside `action == "stop"`):
```cpp
    else stopFilling();
```
Replace with:
```cpp
    else {
      stopFilling();
      gFillingManuallyStarted = false;
      gFillingScheduleActive  = false;
      restoreAreaSchedule("Filling", gDisarmFilling);
    }
```

- [ ] **Step 3: Update pause_1h action for Filling**

Find line 464 (inside `action == "pause_1h"`):
```cpp
    else stopFilling();
```
Replace with:
```cpp
    else {
      stopFilling();
      gFillingManuallyStarted = false;
      gFillingScheduleActive  = false;
      restoreAreaSchedule("Filling", gDisarmFilling);
    }
```

- [ ] **Step 4: Commit**

```bash
git add src/websocket_protocol.cpp
git commit -m "feat: filling manual start disarms/restores schedule"
```

---

### Task 4: Backend — checkSchedules Filling block

**Files:**
- Modify: `src/websocket_protocol.cpp`

- [ ] **Step 1: Add Filling schedule execution after the areas loop**

Find the closing brace of `checkSchedules()` at line 612 (after `}` of the `for (int8_t ai...)` loop and before the final `}`):
```cpp
  }
}
```

Insert the Filling block between them:
```cpp
  }

  // Filling schedule — no zone-groups; just fires startFilling() with duration.
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
```

- [ ] **Step 2: Commit**

```bash
git add src/websocket_protocol.cpp
git commit -m "feat: filling schedule execution in checkSchedules"
```

---

### Task 5: Backend — websocketProtocolLoop natural stop detection for Filling

**Files:**
- Modify: `src/websocket_protocol.cpp`

- [ ] **Step 1: Add Filling transition detection**

Find lines 758-762 in `websocketProtocolLoop()`:
```cpp
  bool curD = isDripIrrigating();
  if (gPrevRunningDrip && !curD) {
    restoreAreaSchedule("Drip", gDisarmDrip);
    gDripScheduleActive = -1;
  }
  gPrevRunningDrip = curD;
```

Add the Filling block immediately after `gPrevRunningDrip = curD;`:
```cpp
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
```

- [ ] **Step 2: Commit**

```bash
git add src/websocket_protocol.cpp
git commit -m "feat: filling natural stop restores schedule and clears flags"
```

---

### Task 6: Firmware build verification

**Files:** none changed

- [ ] **Step 1: Build firmware**

```bash
cd /path/to/irrigation-controller
pio run -e denky32
```

Expected: `SUCCESS` with no errors. The only warning acceptable is about unused variables from other areas.

If build fails with "use of undeclared identifier `fillingMaxMs`": it's already declared in `main.h` and used in `websocket_protocol.cpp` at line 444 — no change needed.

---

### Task 7: New FillScheduleCard.svelte

**Files:**
- Create: `ui/src/tabs/FillScheduleCard.svelte`

- [ ] **Step 1: Create the component**

```svelte
<script>
  const DAY_LABELS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  const DURATION_PRESETS = [5, 10, 15, 20, 30, 45, 60, 90];

  let { schedule, index, ondelete } = $props();

  function toggleDay(day, checked) {
    if (checked) {
      schedule.daysOfWeek = [...schedule.daysOfWeek, day].sort((a, b) => a - b);
    } else {
      schedule.daysOfWeek = schedule.daysOfWeek.filter(d => d !== day);
    }
  }

  function addTime() {
    schedule.startTimes = [...schedule.startTimes, '08:00'];
  }

  function removeTime(ti) {
    schedule.startTimes = schedule.startTimes.filter((_, idx) => idx !== ti);
  }
</script>

<div class="card" style:opacity={schedule.enabled ? 1 : 0.65}>
  <div class="card-header">
    <div style="display:flex;align-items:center;gap:0.5rem;">
      <h3 style="margin:0;">Filling Schedule {index + 1}</h3>
      <label class="toggle">
        <input type="checkbox" bind:checked={schedule.enabled} />
        <span class="toggle-slider"></span>
      </label>
    </div>
    <button type="button" class="btn btn-danger" onclick={ondelete}>Delete</button>
  </div>

  <div class="field" style="flex-wrap:wrap;gap:0.3rem;">
    <span>Duration (min)</span>
    <div class="preset-row" style="margin-top:0;">
      {#each DURATION_PRESETS as d}
        <button type="button" class="btn btn-sm"
          class:btn-primary={schedule.durationMinutes === d}
          class:btn-secondary={schedule.durationMinutes !== d}
          onclick={() => schedule.durationMinutes = d}>{d}</button>
      {/each}
      <input type="number" min="1" style="width:60px;" bind:value={schedule.durationMinutes} />
    </div>
  </div>

  <div class="field" style="flex-wrap:wrap;gap:0.3rem;">
    <span>Days of Week</span>
    <div class="days">
      {#each [1,2,3,4,5,6,7] as day}
        <label>
          <input
            type="checkbox"
            checked={schedule.daysOfWeek.includes(day)}
            onchange={(e) => toggleDay(day, e.target.checked)}
          />
          {DAY_LABELS[day - 1]}
        </label>
      {/each}
    </div>
  </div>

  <div class="field" style="flex-wrap:wrap;gap:0.3rem;">
    <span>Start Times</span>
    <div class="time-list">
      {#each schedule.startTimes as _, ti}
        <div class="time-row">
          <input
            id="time-filling-{index}-{ti}"
            type="time"
            bind:value={schedule.startTimes[ti]}
          />
          <button type="button" class="btn btn-danger btn-sm" onclick={() => removeTime(ti)}>✕</button>
        </div>
      {/each}
      <button type="button" class="btn btn-secondary btn-sm" onclick={addTime}>+ Add time</button>
    </div>
  </div>

  <div class="field" style="flex-wrap:wrap;gap:1.5rem;">
    <span style="display:inline-flex;align-items:center;gap:0.25rem;">
      Start
      <input type="number" style="width:55px;" bind:value={schedule.sunriseSchedule.sunriseOffsetMinutes}
        disabled={!schedule.sunriseSchedule.enabled} />
      min. after sunrise
      <label class="toggle" style="margin-left:0.25rem;">
        <input type="checkbox" bind:checked={schedule.sunriseSchedule.enabled} />
        <span class="toggle-slider"></span>
      </label>
    </span>

    <span style="display:inline-flex;align-items:center;gap:0.25rem;">
      Start
      <input type="number" style="width:55px;" bind:value={schedule.sunsetSchedule.sunsetOffsetMinutes}
        disabled={!schedule.sunsetSchedule.enabled} />
      min. after sunset
      <label class="toggle" style="margin-left:0.25rem;">
        <input type="checkbox" bind:checked={schedule.sunsetSchedule.enabled} />
        <span class="toggle-slider"></span>
      </label>
    </span>
  </div>
</div>
```

- [ ] **Step 2: Commit**

```bash
git add ui/src/tabs/FillScheduleCard.svelte
git commit -m "feat: FillScheduleCard component"
```

---

### Task 8: New FillScheduleTab.svelte

**Files:**
- Create: `ui/src/tabs/FillScheduleTab.svelte`

- [ ] **Step 1: Create the tab component**

```svelte
<script>
  import { onMount } from 'svelte';
  import { getConfig, saveConfig } from '../lib/api.js';
  import { ws } from '../lib/ws.svelte.js';
  import FillScheduleCard from './FillScheduleCard.svelte';

  let fullScheduleRaw = null;
  let sectionEnabled = $state(true);
  let schedules = $state([]);
  let loadError = $state('');
  let saving = $state(false);
  let saveMsg = $state('');
  let saveError = $state('');

  onMount(async () => {
    try {
      const raw = await getConfig('schedule.json');
      fullScheduleRaw = raw;
      const section = raw['Filling'] ?? { enabled: true, schedules: [] };
      sectionEnabled = section.enabled ?? true;
      schedules = (section.schedules ?? []).map(s => ({
        enabled: s.enabled ?? true,
        durationMinutes: s.durationMinutes ?? 15,
        startTimes: s.startTimes ?? ['08:00'],
        daysOfWeek: (s.daysOfWeek ?? [1,2,3,4,5,6,7]).filter(d => d >= 1 && d <= 7),
        sunriseSchedule: {
          enabled: s.sunriseSchedule?.enabled ?? false,
          sunriseOffsetMinutes: s.sunriseSchedule?.sunriseOffsetMinutes ?? 0,
        },
        sunsetSchedule: {
          enabled: s.sunsetSchedule?.enabled ?? false,
          sunsetOffsetMinutes: s.sunsetSchedule?.sunsetOffsetMinutes ?? 0,
        },
      }));
    } catch (e) {
      loadError = e.message;
    }
  });

  // Reload sectionEnabled when manual run ends — firmware restores schedule.json at that point.
  let prevManual = false;
  $effect(() => {
    const m = ws.status?.filling?.manuallyStarted ?? false;
    if (prevManual && !m) {
      getConfig('schedule.json').then(raw => {
        if (!raw) return;
        fullScheduleRaw = { ...(fullScheduleRaw ?? {}), ...raw };
        const section = raw['Filling'];
        if (section != null) sectionEnabled = section.enabled ?? true;
      }).catch(() => {});
    }
    prevManual = m;
  });

  function addSchedule() {
    schedules = [...schedules, {
      enabled: true, durationMinutes: 15,
      startTimes: ['08:00'], daysOfWeek: [1,2,3,4,5,6,7],
      sunriseSchedule: { enabled: false, sunriseOffsetMinutes: 0 },
      sunsetSchedule:  { enabled: false, sunsetOffsetMinutes: 0 },
    }];
  }

  function deleteSchedule(i) {
    schedules = schedules.filter((_, idx) => idx !== i);
  }

  async function save() {
    saving = true; saveMsg = ''; saveError = '';
    try {
      const payload = {
        ...(fullScheduleRaw ?? {}),
        Filling: { enabled: sectionEnabled, schedules },
      };
      await saveConfig('schedule.json', payload);
      fullScheduleRaw = payload;
      saveMsg = 'Saved';
    } catch (e) { saveError = e.message; }
    finally { saving = false; }
  }
</script>

{#if loadError}
  <div class="load-error">Failed to load schedules: {loadError}</div>
{:else}
<div style="--area-color:#fb923c;">
  <div class="field" style="flex-wrap:wrap;gap:0.4rem;">
    <span>Filling Schedules enabled</span>
    <label class="toggle">
      <input type="checkbox" bind:checked={sectionEnabled} />
      <span class="toggle-slider"></span>
    </label>
  </div>

  <div style:opacity={sectionEnabled ? 1 : 0.55}>
    {#each schedules as schedule, i}
      <FillScheduleCard {schedule} index={i} ondelete={() => deleteSchedule(i)} />
    {/each}
    <div class="save-bar" style="justify-content:space-between;">
      <button type="button" class="btn btn-secondary" onclick={addSchedule}>+ Add Schedule</button>
      <div style="display:flex;align-items:center;gap:0.75rem;">
        <button type="button" class="btn btn-primary" onclick={save} disabled={saving}>
          {saving ? 'Saving…' : 'Save'}
        </button>
        {#if saveMsg}<span class="status-ok">{saveMsg}</span>{/if}
        {#if saveError}<span class="status-err">{saveError}</span>{/if}
      </div>
    </div>
  </div>
</div>
{/if}
```

- [ ] **Step 2: Commit**

```bash
git add ui/src/tabs/FillScheduleTab.svelte
git commit -m "feat: FillScheduleTab component"
```

---

### Task 9: Modify ManualTab.svelte — persisted filling duration picker

**Files:**
- Modify: `ui/src/tabs/ManualTab.svelte`

- [ ] **Step 1: Add fillingDuration state and dirty tracking**

Find the line near the top of `<script>` that reads:
```javascript
  let { appConfig, manualZones = $bindable({}) } = $props();
```

After the existing `let areaConfigs = $state({});` and `let loadedConfigs = {};` lines, add:
```javascript
  let fillingDuration = $state(15);
  let loadedFillingDuration = 15;
  let fillingDurationDirty = $derived(fillingDuration !== loadedFillingDuration);
```

- [ ] **Step 2: Load fillingDuration in onMount**

Inside `onMount`, after the `for (const area of areas) { ... }` loop (before the closing `} catch (e) {`), add:
```javascript
      const fc = raw['Filling'] ?? {};
      fillingDuration = fc.durationMinutes ?? 15;
      loadedFillingDuration = fillingDuration;
```

- [ ] **Step 3: Update toggleFilling to be async and use fillingDuration**

Find the existing `toggleFilling` function:
```javascript
  function toggleFilling() {
    if (pendingAction['filling']) return;
    const manual = ws.status?.filling?.manuallyStarted;
    setPending('filling', manual ? 'stop' : 'start');
    if (manual) {
      sendCommand('stop', 'Filling');
    } else {
      sendCommand('start', 'Filling', {
        durationMinutes: Number(fillingCfg?.max_minutes) || 0,
      });
    }
  }
```

Replace entirely with:
```javascript
  async function toggleFilling() {
    if (pendingAction['filling']) return;
    const manual = ws.status?.filling?.manuallyStarted;
    setPending('filling', manual ? 'stop' : 'start');
    if (manual) {
      sendCommand('stop', 'Filling');
    } else {
      if (fillingDurationDirty) {
        try {
          await saveConfig('manual_control.json', { ...areaConfigs, Filling: { durationMinutes: fillingDuration } });
          loadedFillingDuration = fillingDuration;
        } catch (e) {
          presetError = `Config save failed: ${e.message}`;
          clearPending('filling');
          return;
        }
      }
      sendCommand('start', 'Filling', { durationMinutes: fillingDuration });
    }
  }
```

- [ ] **Step 4: Add duration picker row to the filling card markup**

Find the filling card `<div class="card" ...>` block. Inside it, find the single inner `<div style="display:flex;align-items:center;gap:1rem;flex-wrap:wrap;">` and add a second sibling div immediately after it (before the closing `</div>` of the card):

```svelte
    <div class="preset-row" style="margin-top:0.4rem;">
      {#each [5, 10, 15, 20] as d}
        <button type="button" class="btn btn-sm"
          class:btn-primary={fillingDuration === d}
          class:btn-secondary={fillingDuration !== d}
          onclick={() => fillingDuration = d}>{d}</button>
      {/each}
      <input type="number" min="1" style="width:60px;" bind:value={fillingDuration} />
      <span style="font-size:0.8rem;color:#6b7280;">min</span>
    </div>
```

- [ ] **Step 5: Commit**

```bash
git add ui/src/tabs/ManualTab.svelte
git commit -m "feat: configurable filling duration picker in ManualTab"
```

---

### Task 10: Modify App.svelte — Fill Schedule tab

**Files:**
- Modify: `ui/src/App.svelte`

- [ ] **Step 1: Import FillScheduleTab**

Find the existing imports block:
```javascript
  import ManualTab from './tabs/ManualTab.svelte';
  import AreaTab from './tabs/AreaTab.svelte';
  import SettingsTab from './tabs/SettingsTab.svelte';
  import FirmwareTab from './tabs/FirmwareTab.svelte';
```

Add one line:
```javascript
  import ManualTab from './tabs/ManualTab.svelte';
  import AreaTab from './tabs/AreaTab.svelte';
  import FillScheduleTab from './tabs/FillScheduleTab.svelte';
  import SettingsTab from './tabs/SettingsTab.svelte';
  import FirmwareTab from './tabs/FirmwareTab.svelte';
```

- [ ] **Step 2: Add Fill Schedule nav button**

Find in the `<nav>` block:
```svelte
    <button class:active={activeTab === 'settings'} onclick={() => activeTab = 'settings'}>Settings</button>
```

Add the Fill Schedule button immediately before it:
```svelte
    {#if appConfig?.filling?.enabled !== false}
      <button class:active={activeTab === 'fill_schedule'}
        style={`color:#fb923c;${activeTab === 'fill_schedule' ? 'border-bottom-color:#fb923c;' : ''}`}
        onclick={() => activeTab = 'fill_schedule'}>
        Fill Sched
        {#if ws.status?.filling?.scheduleActive}
          <span class="status-dot" style="background:#fb923c"></span>
        {/if}
      </button>
    {/if}
    <button class:active={activeTab === 'settings'} onclick={() => activeTab = 'settings'}>Settings</button>
```

- [ ] **Step 3: Render FillScheduleTab**

Find the content-rendering block (the `{#if ... {:else if ...}` chain). Find:
```svelte
  {:else}
    {#each areas as area, i}
      {#if activeTab === area.id}
        <AreaTab {area} color={areaColor(i)} />
      {/if}
    {/each}
  {/if}
```

Add a new branch for `fill_schedule` before that final `{:else}`:
```svelte
  {:else if activeTab === 'fill_schedule'}
    <FillScheduleTab />
  {:else}
    {#each areas as area, i}
      {#if activeTab === area.id}
        <AreaTab {area} color={areaColor(i)} />
      {/if}
    {/each}
  {/if}
```

- [ ] **Step 4: Commit**

```bash
git add ui/src/App.svelte
git commit -m "feat: Fill Schedule tab in App"
```

---

### Task 11: Update preview.html

**Files:**
- Modify: `preview.html`

- [ ] **Step 1: Add SCHEDULE_CONFIG Filling key**

Find in preview.html the `SCHEDULE_CONFIG` object (around line 148):
```javascript
  Grass: { enabled: true, suspendAboveTempEnabled: true, ...
  Drip: { enabled: true, ...
```

Add a `Filling` key before `Grass`:
```javascript
  Filling: { enabled: true, schedules: [
    { enabled: true, durationMinutes: 15, daysOfWeek: [1,2,3,4,5,6,7], startTimes: ['07:00'], sunriseSchedule: { enabled: false, sunriseOffsetMinutes: 0 }, sunsetSchedule: { enabled: false, sunsetOffsetMinutes: 0 } }
  ]},
  Grass: { enabled: true, suspendAboveTempEnabled: true, ...
```

- [ ] **Step 2: Add duration picker to the filling card in buildManualTab**

Find in `buildManualTab()` the `fillingCard` construction (around line 775):
```javascript
  const fillingCard = h('div', { class: `card...`, style: '...' },
    h('div', { style: 'display:flex;align-items:center;gap:1rem;flex-wrap:wrap;' },
      h('h3', { style: 'margin:0;' }, 'Automatic Filling'),
      fBtn,
      fReason,
      h('span', { style: 'margin-left:auto;...' }, ...))
  );
```

Add a `MOCK_FILLING_DURATION` variable and duration-picker row:
```javascript
  let mockFillingDuration = 15;
  const FILL_DUR_PRESETS = [5, 10, 15, 20];
  const fillDurPresetBtns = FILL_DUR_PRESETS.map(d => {
    const b = h('button', { class: 'btn btn-sm btn-secondary' }, String(d));
    b.addEventListener('click', () => {
      mockFillingDuration = d;
      fillDurInput.value = d;
      fillDurPresetBtns.forEach((pb, pi) => pb.className = `btn btn-sm ${FILL_DUR_PRESETS[pi] === d ? 'btn-primary' : 'btn-secondary'}`);
    });
    if (d === mockFillingDuration) b.className = 'btn btn-sm btn-primary';
    return b;
  });
  const fillDurInput = h('input', { type: 'number', min: '1', style: 'width:60px;', value: String(mockFillingDuration) });
  fillDurInput.addEventListener('input', () => { mockFillingDuration = parseInt(fillDurInput.value) || 15; });
  const fillDurRow = h('div', { class: 'preset-row', style: 'margin-top:0.4rem;' },
    ...fillDurPresetBtns,
    fillDurInput,
    h('span', { style: 'font-size:0.8rem;color:#6b7280;' }, 'min')
  );

  const fillingCard = h('div', { class: `card${APP_CONFIG.filling.enabled === false ? ' content-disabled' : ''}`, style: '--area-color:#fb923c;border-color:var(--area-color);border-width:2px;padding:0.5rem 1rem;' },
    h('div', { style: 'display:flex;align-items:center;gap:1rem;flex-wrap:wrap;' },
      h('h3', { style: 'margin:0;' }, 'Automatic Filling'),
      fBtn,
      fReason,
      h('span', { style: 'margin-left:auto;display:flex;gap:1rem;align-items:center;' },
        h('span', {}, h('span', { class: 'status-label', style: 'margin-right:0.3rem;' }, `Pump ${f.pump_id}`), h('span', { class: 'status-value status-off' }, 'OFF')),
        h('span', {}, h('span', { class: 'status-label', style: 'margin-right:0.3rem;' }, 'Water'), h('span', { class: 'status-value' }, `${MOCK_WATER_LEVEL}%`)))),
    fillDurRow
  );
```

- [ ] **Step 3: Add buildFillScheduleTab function**

Before the `// Navigation` comment (around line 1028), add:

```javascript
// Fill Schedule tab (filling only, no zones)
function buildFillScheduleTab() {
  const DAY_LABELS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  const DURATION_PRESETS_FILL = [5, 10, 15, 20, 30, 45, 60, 90];
  const sched = SCHEDULE_CONFIG['Filling'] ?? { enabled: true, schedules: [] };

  let sectionEnabled = sched.enabled ?? true;
  const enabledToggle = h('input', { type: 'checkbox' }); enabledToggle.checked = sectionEnabled;
  const enabledLabel = h('label', { class: 'toggle' }, enabledToggle, h('span', { class: 'toggle-slider' }));
  const cardsContainer = h('div', {});

  function buildFillCard(s, idx) {
    const durBtns = DURATION_PRESETS_FILL.map(d => {
      const b = h('button', { class: `btn btn-sm ${s.durationMinutes === d ? 'btn-primary' : 'btn-secondary'}` }, String(d));
      b.addEventListener('click', () => { s.durationMinutes = d; durBtns.forEach((pb, pi) => pb.className = `btn btn-sm ${DURATION_PRESETS_FILL[pi] === d ? 'btn-primary' : 'btn-secondary'}`); });
      return b;
    });
    const durInput = h('input', { type: 'number', min: '1', style: 'width:60px;', value: String(s.durationMinutes) });
    const enableToggle = h('input', { type: 'checkbox' }); enableToggle.checked = s.enabled ?? true;
    const delBtn = h('button', { class: 'btn btn-danger' }, 'Delete');
    delBtn.addEventListener('click', () => { cardsContainer.removeChild(card); });
    const card = h('div', { class: 'card', style: `opacity:${(s.enabled ?? true) ? 1 : 0.65};` },
      h('div', { class: 'card-header' },
        h('div', { style: 'display:flex;align-items:center;gap:0.5rem;' },
          h('h3', { style: 'margin:0;' }, `Filling Schedule ${idx + 1}`),
          h('label', { class: 'toggle' }, enableToggle, h('span', { class: 'toggle-slider' }))),
        delBtn),
      h('div', { class: 'field', style: 'flex-wrap:wrap;gap:0.3rem;' }, h('span', {}, 'Duration (min)'),
        h('div', { class: 'preset-row', style: 'margin-top:0;' }, ...durBtns, durInput)),
      h('div', { class: 'field', style: 'flex-wrap:wrap;gap:0.3rem;' }, h('span', {}, 'Days of Week'),
        h('div', { class: 'days' }, ...[1,2,3,4,5,6,7].map(day => {
          const cb = h('input', { type: 'checkbox' }); cb.checked = (s.daysOfWeek ?? [1,2,3,4,5,6,7]).includes(day);
          return h('label', {}, cb, DAY_LABELS[day - 1]);
        }))),
      h('div', { class: 'field', style: 'flex-wrap:wrap;gap:0.3rem;' }, h('span', {}, 'Start Times'),
        h('div', { class: 'time-list' },
          ...(s.startTimes ?? ['08:00']).map(t => h('div', { class: 'time-row' }, h('input', { type: 'time', value: t }), h('button', { class: 'btn btn-danger btn-sm' }, '✕'))),
          h('button', { class: 'btn btn-secondary btn-sm' }, '+ Add time')))
    );
    return card;
  }

  (sched.schedules ?? []).forEach((s, i) => cardsContainer.append(buildFillCard(s, i)));

  const addBtn = h('button', { class: 'btn btn-secondary' }, '+ Add Schedule');
  addBtn.addEventListener('click', () => {
    const s = { enabled: true, durationMinutes: 15, daysOfWeek: [1,2,3,4,5,6,7], startTimes: ['08:00'], sunriseSchedule: { enabled: false, sunriseOffsetMinutes: 0 }, sunsetSchedule: { enabled: false, sunsetOffsetMinutes: 0 } };
    cardsContainer.append(buildFillCard(s, cardsContainer.children.length));
  });
  const saveBtn = h('button', { class: 'btn btn-primary' }, 'Save');
  const saveMsg = h('span', { class: 'status-ok', style: 'display:none;' }, 'Saved');
  saveBtn.addEventListener('click', () => { saveMsg.style.display = ''; setTimeout(() => saveMsg.style.display = 'none', 2000); });

  return h('div', { style: '--area-color:#fb923c;' },
    h('div', { class: 'field', style: 'flex-wrap:wrap;gap:0.4rem;' }, h('span', {}, 'Filling Schedules enabled'), enabledLabel),
    cardsContainer,
    h('div', { class: 'save-bar', style: 'justify-content:space-between;' }, addBtn, h('div', { style: 'display:flex;align-items:center;gap:0.75rem;' }, saveBtn, saveMsg))
  );
}
```

- [ ] **Step 4: Add Fill Schedule tab to tabDefs**

Find `tabDefs` (around line 1029):
```javascript
const tabDefs = [
  { id: 'manual', label: 'Manual Control', build: buildManualTab },
  ...APP_CONFIG.areas.filter(a => a.enabled !== false).map((a, idx) => ({ id: a.id, label: a.id, color: AREA_COLORS[idx % AREA_COLORS.length], build: () => buildScheduleTab(a, idx) })),
  { id: 'settings', label: 'Settings', build: buildSettingsTab },
  { id: 'firmware', label: 'Firmware', build: buildFirmwareTab }
];
```

Add the Filling tab before `settings`:
```javascript
const tabDefs = [
  { id: 'manual', label: 'Manual Control', build: buildManualTab },
  ...APP_CONFIG.areas.filter(a => a.enabled !== false).map((a, idx) => ({ id: a.id, label: a.id, color: AREA_COLORS[idx % AREA_COLORS.length], build: () => buildScheduleTab(a, idx) })),
  ...(APP_CONFIG.filling.enabled !== false ? [{ id: 'fill_schedule', label: 'Fill Sched', color: '#fb923c', build: buildFillScheduleTab }] : []),
  { id: 'settings', label: 'Settings', build: buildSettingsTab },
  { id: 'firmware', label: 'Firmware', build: buildFirmwareTab }
];
```

- [ ] **Step 5: Add schedule-active dot for Fill Schedule nav tab in refreshNavDots**

Find `refreshNavDots()` at the end. After the existing area-tabs dot logic, before the closing `}`, add:

```javascript
  // Fill Schedule tab: show dot when filling scheduleActive
  const fillSchedDot = document.getElementById('nav-dot-fill_schedule');
  if (fillSchedDot) {
    const fillSchedActive = state.eState['filling'] === 'active' && state.eSource['filling'] === 'schedule';
    if (fillSchedActive) { fillSchedDot.style.background = '#fb923c'; fillSchedDot.style.display = ''; }
    else fillSchedDot.style.display = 'none';
  }
```

- [ ] **Step 6: Update eClickPreview to support fill_schedule mock schedule source**

Find in `buildEmergencyBar` or the mock state initialization where `state.eSource['filling']` is set. In the click handler for the filling button, when starting: `state.eSource['filling'] = 'manual'` — this is already correct, no change needed. The dot logic distinguishes manual vs schedule source.

- [ ] **Step 7: Commit**

```bash
git add preview.html
git commit -m "feat: preview.html sync — Fill Schedule tab and filling duration picker"
```

---

### Task 12: Final build and smoke check

- [ ] **Step 1: Build UI**

```bash
cd ui && npm run build
```

Expected: build completes with no errors.

- [ ] **Step 2: Open preview.html in browser**

Open `preview.html` in a browser and verify:
1. "Fill Sched" tab appears in nav (orange text), renders a schedule section with `+ Add Schedule` button.
2. Clicking `+ Add Schedule` adds a new schedule card with duration presets, days, start times, sunrise/sunset.
3. Manual Control tab filling card has 4 preset chips (5, 10, 15, 20) and a number input; selected preset highlights.
4. No JS console errors.

- [ ] **Step 3: Firmware build**

```bash
pio run -e denky32
```

Expected: `SUCCESS`.

- [ ] **Step 4: Final commit if any fixes applied**

```bash
git add -p
git commit -m "fix: address any build issues from final check"
```
