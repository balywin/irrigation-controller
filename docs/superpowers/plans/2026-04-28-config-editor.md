# Config Editor UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Svelte 5 SPA with four tabs (App Settings, Schedules, Manual Control, Firmware) that reads and writes all three device config files via the existing `/api/config/*` endpoints.

**Architecture:** Bespoke component per tab — each owns its load/save lifecycle. API layer is two pure fetch functions. Build outputs to `data/` which is then embedded into firmware via `tools/embed_static.js`.

**Tech Stack:** Svelte 5.38.1, Vite 6.3.5, @sveltejs/vite-plugin-svelte 5.1.1, vitest (unit tests for api.js). C++ firmware updated to parse nested `app_config.json`.

---

## File Map

| Action | Path | Responsibility |
|--------|------|---------------|
| Create | `ui/package.json` | Project manifest, scripts, deps |
| Create | `ui/vite.config.js` | Vite + Svelte config, output to `../data/`, vitest config |
| Create | `ui/index.html` | SPA entry point |
| Create | `ui/src/main.js` | Svelte mount |
| Create | `ui/src/App.svelte` | Tab shell / navigation |
| Create | `ui/src/assets/app.css` | Global styles |
| Create | `ui/src/lib/api.js` | `getConfig` / `saveConfig` |
| Create | `ui/src/lib/api.test.js` | Unit tests for api.js |
| Create | `ui/src/tabs/AppTab.svelte` | `app_config.json` form + backup/restore |
| Create | `ui/src/tabs/ScheduleDrip.svelte` | `schedule.json` array editor |
| Create | `ui/src/tabs/ManualTab.svelte` | `manual_control.json` named-areas editor |
| Create | `ui/src/tabs/FirmwareTab.svelte` | OTA upload via iframe |
| Modify | `src/main.cpp:99-138` | `applyAppConfig()` reads nested JSON |
| Modify | `data/config/schedule.json` | Remove `pumpNumber`, `masterValveNumber`; fix `daysOfWeek` |
| Modify | `data/config/manual_control.json` | `enabled` → bool |

---

## Task 1: Scaffold Svelte + Vite Project

**Files:**
- Create: `ui/package.json`
- Create: `ui/vite.config.js`
- Create: `ui/index.html`
- Create: `ui/src/main.js`

- [ ] **Step 1: Create `ui/package.json`**

```json
{
  "name": "irrigation-ui",
  "private": true,
  "version": "0.1.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "vite build",
    "preview": "vite preview",
    "test": "vitest run"
  },
  "devDependencies": {
    "@sveltejs/vite-plugin-svelte": "^5.1.1",
    "svelte": "^5.38.1",
    "vite": "^6.3.5",
    "vitest": "^3.0.0"
  }
}
```

- [ ] **Step 2: Create `ui/vite.config.js`**

```js
import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

export default defineConfig({
  plugins: [svelte()],
  build: {
    outDir: '../data',
    emptyOutDir: false,
  },
  test: {
    environment: 'node',
  },
});
```

- [ ] **Step 3: Create `ui/index.html`**

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Irrigation Controller</title>
</head>
<body>
  <div id="app"></div>
  <script type="module" src="/src/main.js"></script>
</body>
</html>
```

- [ ] **Step 4: Create `ui/src/main.js`**

```js
import { mount } from 'svelte';
import App from './App.svelte';

mount(App, { target: document.getElementById('app') });
```

- [ ] **Step 5: Install dependencies**

Run from `ui/` directory:
```bash
cd ui && npm install
```
Expected: node_modules updated, no errors.

- [ ] **Step 6: Verify dev server starts**

```bash
cd ui && npm run dev
```
Expected: Vite prints `Local: http://localhost:5173/` — stop with Ctrl+C.

- [ ] **Step 7: Commit**

```bash
git add ui/package.json ui/vite.config.js ui/index.html ui/src/main.js
git commit -m "feat(ui): scaffold Svelte 5 + Vite project"
```

---

## Task 2: API Layer

**Files:**
- Create: `ui/src/lib/api.js`
- Create: `ui/src/lib/api.test.js`

- [ ] **Step 1: Write failing tests**

Create `ui/src/lib/api.test.js`:

```js
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { getConfig, saveConfig } from './api.js';

const mockFetch = vi.fn();
global.fetch = mockFetch;

describe('getConfig', () => {
  beforeEach(() => mockFetch.mockClear());

  it('GETs /api/config/<name> and returns parsed JSON', async () => {
    mockFetch.mockResolvedValue({ json: () => Promise.resolve({ foo: 'bar' }) });
    const result = await getConfig('app_config.json');
    expect(mockFetch).toHaveBeenCalledWith('/api/config/app_config.json');
    expect(result).toEqual({ foo: 'bar' });
  });

  it('throws on network failure', async () => {
    mockFetch.mockRejectedValue(new Error('Network error'));
    await expect(getConfig('app_config.json')).rejects.toThrow('Network error');
  });
});

describe('saveConfig', () => {
  beforeEach(() => mockFetch.mockClear());

  it('POSTs JSON body to /api/config/<name>', async () => {
    mockFetch.mockResolvedValue({ ok: true });
    await saveConfig('app_config.json', { foo: 'bar' });
    expect(mockFetch).toHaveBeenCalledWith('/api/config/app_config.json', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ foo: 'bar' }),
    });
  });

  it('throws on network failure', async () => {
    mockFetch.mockRejectedValue(new Error('Network error'));
    await expect(saveConfig('app_config.json', {})).rejects.toThrow('Network error');
  });
});
```

- [ ] **Step 2: Run tests — verify they fail**

```bash
cd ui && npm test
```
Expected: `FAIL src/lib/api.test.js` — "Cannot find module './api.js'"

- [ ] **Step 3: Create `ui/src/lib/api.js`**

```js
export const getConfig = (name) =>
  fetch(`/api/config/${name}`).then(r => r.json());

export const saveConfig = (name, data) =>
  fetch(`/api/config/${name}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data),
  });
```

- [ ] **Step 4: Run tests — verify they pass**

```bash
cd ui && npm test
```
Expected: `PASS src/lib/api.test.js` — 4 tests pass.

- [ ] **Step 5: Commit**

```bash
git add ui/src/lib/api.js ui/src/lib/api.test.js
git commit -m "feat(ui): add api layer with tests"
```

---

## Task 3: Global Styles and App Shell

**Files:**
- Create: `ui/src/assets/app.css`
- Create: `ui/src/App.svelte`

- [ ] **Step 1: Create `ui/src/assets/app.css`**

```css
*, *::before, *::after { box-sizing: border-box; }

body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  margin: 0;
  background: #f4f6f8;
  color: #333;
  font-size: 14px;
}

.app {
  max-width: 860px;
  margin: 0 auto;
  padding: 1rem;
}

nav {
  display: flex;
  gap: 0.25rem;
  border-bottom: 2px solid #dde2e8;
  margin-bottom: 1.5rem;
}

nav button {
  padding: 0.6rem 1.2rem;
  background: none;
  border: none;
  border-bottom: 3px solid transparent;
  margin-bottom: -2px;
  cursor: pointer;
  font-size: 0.9rem;
  color: #666;
  font-weight: 500;
}

nav button.active {
  color: #2563eb;
  border-bottom-color: #2563eb;
}

.section {
  background: #fff;
  border-radius: 8px;
  padding: 1.25rem 1.5rem;
  margin-bottom: 1rem;
  box-shadow: 0 1px 3px rgba(0,0,0,0.08);
}

.section h2 {
  margin: 0 0 1rem;
  font-size: 0.75rem;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: #888;
  font-weight: 600;
}

.field {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.45rem 0;
  border-bottom: 1px solid #f2f2f2;
}

.field:last-child { border-bottom: none; }

.field label { color: #444; }

.field input[type="text"],
.field input[type="number"],
.field input[type="time"] {
  width: 160px;
  padding: 0.3rem 0.5rem;
  border: 1px solid #d1d5db;
  border-radius: 5px;
  font-size: 0.875rem;
}

.card {
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  padding: 1rem;
  margin-bottom: 0.75rem;
  background: #fff;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.75rem;
  padding-bottom: 0.5rem;
  border-bottom: 1px solid #f2f2f2;
}

.card-header h3 { margin: 0; font-size: 0.95rem; }

.days { display: flex; gap: 0.4rem; flex-wrap: wrap; margin-top: 0.25rem; }

.days label {
  display: flex;
  align-items: center;
  gap: 0.2rem;
  font-size: 0.8rem;
  padding: 0.2rem 0.4rem;
  border: 1px solid #d1d5db;
  border-radius: 4px;
  cursor: pointer;
}

.days label:has(input:checked) {
  background: #dbeafe;
  border-color: #93c5fd;
}

.time-list { display: flex; flex-direction: column; gap: 0.3rem; margin-top: 0.25rem; }

.time-row { display: flex; gap: 0.4rem; align-items: center; }

.save-bar {
  display: flex;
  align-items: center;
  gap: 1rem;
  margin-top: 1.25rem;
  padding-top: 1rem;
  border-top: 1px solid #f2f2f2;
}

.btn {
  padding: 0.45rem 1.1rem;
  border: none;
  border-radius: 5px;
  cursor: pointer;
  font-size: 0.875rem;
  font-weight: 500;
}

.btn-primary { background: #2563eb; color: #fff; }
.btn-primary:disabled { background: #93c5fd; cursor: not-allowed; }
.btn-danger { background: #ef4444; color: #fff; padding: 0.25rem 0.6rem; font-size: 0.8rem; }
.btn-secondary { background: #6b7280; color: #fff; }
.btn-sm { padding: 0.2rem 0.6rem; font-size: 0.8rem; }

.status-ok { color: #16a34a; font-size: 0.85rem; }
.status-err { color: #dc2626; font-size: 0.85rem; }
.load-error { color: #dc2626; padding: 1rem; background: #fef2f2; border-radius: 6px; }

.toggle {
  position: relative;
  display: inline-block;
  width: 40px;
  height: 22px;
}

.toggle input { opacity: 0; width: 0; height: 0; }

.toggle-slider {
  position: absolute;
  inset: 0;
  background: #d1d5db;
  border-radius: 22px;
  transition: background 0.2s;
  cursor: pointer;
}

.toggle-slider::before {
  content: '';
  position: absolute;
  height: 16px;
  width: 16px;
  left: 3px;
  top: 3px;
  background: white;
  border-radius: 50%;
  transition: transform 0.2s;
}

.toggle input:checked + .toggle-slider { background: #2563eb; }
.toggle input:checked + .toggle-slider::before { transform: translateX(18px); }
```

- [ ] **Step 2: Create `ui/src/App.svelte`**

```svelte
<script>
  import '../assets/app.css';
  import AppTab from './tabs/AppTab.svelte';
  import ScheduleDrip from './tabs/ScheduleDrip.svelte';
  import ManualTab from './tabs/ManualTab.svelte';
  import FirmwareTab from './tabs/FirmwareTab.svelte';

  let activeTab = $state('app');
</script>

<div class="app">
  <nav>
    <button class:active={activeTab === 'app'} onclick={() => activeTab = 'app'}>App Settings</button>
    <button class:active={activeTab === 'schedules'} onclick={() => activeTab = 'schedules'}>Schedules</button>
    <button class:active={activeTab === 'manual'} onclick={() => activeTab = 'manual'}>Manual Control</button>
    <button class:active={activeTab === 'firmware'} onclick={() => activeTab = 'firmware'}>Firmware</button>
  </nav>

  {#if activeTab === 'app'}
    <AppTab />
  {:else if activeTab === 'schedules'}
    <ScheduleDrip />
  {:else if activeTab === 'manual'}
    <ManualTab />
  {:else if activeTab === 'firmware'}
    <FirmwareTab />
  {/if}
</div>
```

- [ ] **Step 3: Create placeholder tab files so the app compiles**

Create `ui/src/tabs/AppTab.svelte`:
```svelte
<p>App Settings (coming soon)</p>
```

Create `ui/src/tabs/ScheduleDrip.svelte`:
```svelte
<p>Schedules (coming soon)</p>
```

Create `ui/src/tabs/ManualTab.svelte`:
```svelte
<p>Manual Control (coming soon)</p>
```

Create `ui/src/tabs/FirmwareTab.svelte`:
```svelte
<p>Firmware (coming soon)</p>
```

- [ ] **Step 4: Verify app compiles**

```bash
cd ui && npm run dev
```
Open `http://localhost:5173` — four tabs visible, clicking switches content. Stop with Ctrl+C.

- [ ] **Step 5: Commit**

```bash
git add ui/src/App.svelte ui/src/assets/app.css ui/src/tabs/
git commit -m "feat(ui): add app shell and global styles"
```

---

## Task 4: AppTab — App Settings Form

**Files:**
- Modify: `ui/src/tabs/AppTab.svelte` (replace placeholder)

- [ ] **Step 1: Replace `ui/src/tabs/AppTab.svelte`**

```svelte
<script>
  import { onMount } from 'svelte';
  import { getConfig, saveConfig } from '../lib/api.js';

  let config = $state(null);
  let loadError = $state('');
  let saving = $state(false);
  let saveStatus = $state('');

  onMount(async () => {
    try {
      config = await getConfig('app_config.json');
    } catch (e) {
      loadError = e.message;
    }
  });

  async function save() {
    saving = true;
    saveStatus = '';
    try {
      await saveConfig('app_config.json', config);
      saveStatus = 'ok';
    } catch (e) {
      saveStatus = 'err:' + e.message;
    } finally {
      saving = false;
    }
  }
</script>

{#if loadError}
  <div class="load-error">Failed to load app_config.json: {loadError}</div>
{:else if !config}
  <p>Loading…</p>
{:else}
  <div class="section">
    <h2>Device</h2>
    <div class="field">
      <label>Device Name</label>
      <input type="text" bind:value={config.device_name} />
    </div>
  </div>

  <div class="section">
    <h2>Filling</h2>
    <div class="field">
      <label>Enabled</label>
      <label class="toggle">
        <input type="checkbox" bind:checked={config.filling.enabled} />
        <span class="toggle-slider"></span>
      </label>
    </div>
    <div class="field">
      <label>Max Minutes</label>
      <input type="number" min="1" bind:value={config.filling.max_minutes} />
    </div>
    <div class="field">
      <label>Level Filtering (s)</label>
      <input type="number" min="0" bind:value={config.filling.level_filtering_seconds} />
    </div>
    <div class="field">
      <label>Leakage Threshold</label>
      <input type="number" min="1" bind:value={config.filling.leakage_detector_threshold} />
    </div>
    <div class="field">
      <label>High Level Pressure</label>
      <input type="number" bind:value={config.filling.high_level_pressure} />
    </div>
    <div class="field">
      <label>Low Level Pressure</label>
      <input type="number" bind:value={config.filling.low_level_pressure} />
    </div>
  </div>

  {#each ['grass_irrigation', 'drip_irrigation'] as key}
    {@const section = config[key]}
    <div class="section">
      <h2>{key === 'grass_irrigation' ? 'Grass Irrigation' : 'Drip Irrigation'}</h2>
      <div class="field">
        <label>Enabled</label>
        <label class="toggle">
          <input type="checkbox" bind:checked={section.enabled} />
          <span class="toggle-slider"></span>
        </label>
      </div>
      <div class="field">
        <label>Description</label>
        <input type="text" bind:value={section.description} />
      </div>
      <div class="field">
        <label>Pump Number</label>
        <input type="number" min="1" bind:value={section.pump_number} />
      </div>
      <div class="field">
        <label>Pump Start Delay (s)</label>
        <input type="number" min="0" bind:value={section.pump_start_delay_seconds} />
      </div>
      <div class="field">
        <label>Main Valve Number</label>
        <input type="number" min="1" bind:value={section.main_valve_number} />
      </div>
      <div class="field">
        <label>Number of Zones</label>
        <input type="number" min="1" bind:value={section.number_of_zones} />
      </div>
      <div class="field">
        <label>Max Minutes</label>
        <input type="number" min="1" bind:value={section.max_minutes} />
      </div>
    </div>
  {/each}

  <div class="save-bar">
    <button class="btn btn-primary" onclick={save} disabled={saving}>
      {saving ? 'Saving…' : 'Save'}
    </button>
    {#if saveStatus === 'ok'}
      <span class="status-ok">Saved</span>
    {:else if saveStatus.startsWith('err:')}
      <span class="status-err">{saveStatus.slice(4)}</span>
    {/if}
  </div>
{/if}
```

- [ ] **Step 2: Verify in browser**

```bash
cd ui && npm run dev
```
Open `http://localhost:5173` → App Settings tab. All fields render. Change device name and click Save — check browser network tab: `POST /api/config/app_config.json` sent. (Will get connection refused since no ESP32 — that's OK.) Stop with Ctrl+C.

- [ ] **Step 3: Commit**

```bash
git add ui/src/tabs/AppTab.svelte
git commit -m "feat(ui): implement AppTab form for app_config.json"
```

---

## Task 5: AppTab — Backup and Restore

**Files:**
- Modify: `ui/src/tabs/AppTab.svelte`

- [ ] **Step 1: Add backup/restore section to `ui/src/tabs/AppTab.svelte`**

Add the following imports and functions inside the `<script>` block (before the closing `</script>`):

```svelte
  // --- backup / restore ---
  let restoreStatus = $state('');
  let restoreError = $state('');
  let restoring = $state(false);

  async function downloadBackup() {
    try {
      const [app, auto_schedule, manual_control] = await Promise.all([
        getConfig('app_config.json'),
        getConfig('schedule.json'),
        getConfig('manual_control.json'),
      ]);
      const blob = new Blob(
        [JSON.stringify({ app, auto_schedule, manual_control }, null, 2)],
        { type: 'application/json' }
      );
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `irrigation-backup-${new Date().toISOString().split('T')[0]}.json`;
      a.click();
      URL.revokeObjectURL(url);
    } catch (e) {
      restoreError = 'Backup failed: ' + e.message;
    }
  }

  async function handleRestoreFile(e) {
    const file = e.target.files[0];
    if (!file) return;
    restoreStatus = '';
    restoreError = '';
    restoring = true;
    try {
      const text = await file.text();
      let backup;
      try {
        backup = JSON.parse(text);
      } catch {
        restoreError = 'Invalid JSON file';
        return;
      }
      if (!backup.app || !backup.auto_schedule || !backup.manual_control) {
        restoreError = 'Missing keys: app, auto_schedule, manual_control';
        return;
      }
      const pairs = [
        ['app_config.json', backup.app],
        ['schedule.json', backup.auto_schedule],
        ['manual_control.json', backup.manual_control],
      ];
      const failed = [];
      for (const [name, data] of pairs) {
        try {
          await saveConfig(name, data);
        } catch (err) {
          failed.push(name);
        }
      }
      if (failed.length === 0) {
        config = await getConfig('app_config.json');
        restoreStatus = 'Restored successfully';
      } else {
        restoreError = 'Failed to save: ' + failed.join(', ');
      }
    } finally {
      restoring = false;
      e.target.value = '';
    }
  }
```

Add this section at the bottom of the template (after the save-bar div, inside the `{:else}` block):

```svelte
  <div class="section">
    <h2>Backup / Restore</h2>
    <div class="field">
      <label>Download all configs</label>
      <button class="btn btn-secondary" onclick={downloadBackup}>Download backup</button>
    </div>
    <div class="field">
      <label>Restore from backup</label>
      <input
        type="file"
        accept=".json"
        disabled={restoring}
        onchange={handleRestoreFile}
      />
    </div>
    {#if restoring}
      <p>Restoring…</p>
    {/if}
    {#if restoreStatus}
      <p class="status-ok">{restoreStatus}</p>
    {/if}
    {#if restoreError}
      <p class="status-err">{restoreError}</p>
    {/if}
  </div>
```

- [ ] **Step 2: Verify in browser**

```bash
cd ui && npm run dev
```
App Settings tab — Backup/Restore section visible at bottom. Click "Download backup" — browser attempts 3 parallel GETs (will fail without ESP32, which is expected). Stop with Ctrl+C.

- [ ] **Step 3: Commit**

```bash
git add ui/src/tabs/AppTab.svelte
git commit -m "feat(ui): add config backup/restore to AppTab"
```

---

## Task 6: ScheduleDrip

**Files:**
- Modify: `ui/src/tabs/ScheduleDrip.svelte` (replace placeholder)

- [ ] **Step 1: Replace `ui/src/tabs/ScheduleDrip.svelte`**

```svelte
<script>
  import { onMount } from 'svelte';
  import { getConfig, saveConfig } from '../lib/api.js';

  const DAY_LABELS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];

  let schedules = $state([]);
  let loadError = $state('');
  let saving = $state(false);
  let saveStatus = $state('');

  onMount(async () => {
    try {
      const raw = await getConfig('schedule.json');
      schedules = raw.map(s => ({
        enabled: s.enabled ?? true,
        zones: s.zones ?? [1],
        durationMinutes: s.durationMinutes ?? 30,
        suspendAboveTemp: s.suspendAboveTemp ?? 35,
        startTimes: s.startTimes ?? ['8:00'],
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

  function addSchedule() {
    schedules = [...schedules, {
      enabled: true,
      zones: [1],
      durationMinutes: 30,
      suspendAboveTemp: 35,
      startTimes: ['8:00'],
      daysOfWeek: [1, 2, 3, 4, 5, 6, 7],
      sunriseSchedule: { enabled: false, sunriseOffsetMinutes: 0 },
      sunsetSchedule: { enabled: false, sunsetOffsetMinutes: 0 },
    }];
  }

  function deleteSchedule(i) {
    schedules = schedules.filter((_, idx) => idx !== i);
  }

  function toggleDay(schedule, day, checked) {
    if (checked) {
      schedule.daysOfWeek = [...schedule.daysOfWeek, day].sort((a, b) => a - b);
    } else {
      schedule.daysOfWeek = schedule.daysOfWeek.filter(d => d !== day);
    }
  }

  function addTime(schedule) {
    schedule.startTimes = [...schedule.startTimes, '8:00'];
  }

  function removeTime(schedule, i) {
    schedule.startTimes = schedule.startTimes.filter((_, idx) => idx !== i);
  }

  function zonesString(schedule) {
    return schedule.zones.join(', ');
  }

  function parseZones(schedule, value) {
    schedule.zones = value.split(',').map(s => parseInt(s.trim(), 10)).filter(n => !isNaN(n));
  }

  async function save() {
    saving = true;
    saveStatus = '';
    try {
      await saveConfig('schedule.json', schedules);
      saveStatus = 'ok';
    } catch (e) {
      saveStatus = 'err:' + e.message;
    } finally {
      saving = false;
    }
  }
</script>

{#if loadError}
  <div class="load-error">Failed to load schedule.json: {loadError}</div>
{:else}
  {#each schedules as schedule, i}
    <div class="card">
      <div class="card-header">
        <h3>Schedule {i + 1}</h3>
        <div style="display:flex;gap:0.5rem;align-items:center;">
          <label class="toggle">
            <input type="checkbox" bind:checked={schedule.enabled} />
            <span class="toggle-slider"></span>
          </label>
          <button class="btn btn-danger" onclick={() => deleteSchedule(i)}>Delete</button>
        </div>
      </div>

      <div class="field">
        <label>Zones (comma-separated)</label>
        <input
          type="text"
          value={zonesString(schedule)}
          onchange={(e) => parseZones(schedule, e.target.value)}
        />
      </div>
      <div class="field">
        <label>Duration (min)</label>
        <input type="number" min="1" bind:value={schedule.durationMinutes} />
      </div>
      <div class="field">
        <label>Suspend above temp (°C)</label>
        <input type="number" bind:value={schedule.suspendAboveTemp} />
      </div>

      <div class="field" style="flex-direction:column;align-items:flex-start;gap:0.4rem;">
        <label>Days of Week</label>
        <div class="days">
          {#each [1,2,3,4,5,6,7] as day}
            <label>
              <input
                type="checkbox"
                checked={schedule.daysOfWeek.includes(day)}
                onchange={(e) => toggleDay(schedule, day, e.target.checked)}
              />
              {DAY_LABELS[day - 1]}
            </label>
          {/each}
        </div>
      </div>

      <div class="field" style="flex-direction:column;align-items:flex-start;gap:0.4rem;">
        <label>Start Times</label>
        <div class="time-list">
          {#each schedule.startTimes as _, ti}
            <div class="time-row">
              <input
                type="text"
                style="width:90px;"
                placeholder="7:00"
                bind:value={schedule.startTimes[ti]}
              />
              <button class="btn btn-danger btn-sm" onclick={() => removeTime(schedule, ti)}>✕</button>
            </div>
          {/each}
          <button class="btn btn-secondary btn-sm" onclick={() => addTime(schedule)}>+ Add time</button>
        </div>
      </div>

      <div class="field">
        <label>Sunrise offset enabled</label>
        <label class="toggle">
          <input type="checkbox" bind:checked={schedule.sunriseSchedule.enabled} />
          <span class="toggle-slider"></span>
        </label>
      </div>
      {#if schedule.sunriseSchedule.enabled}
        <div class="field">
          <label>Sunrise offset (min)</label>
          <input type="number" bind:value={schedule.sunriseSchedule.sunriseOffsetMinutes} />
        </div>
      {/if}

      <div class="field">
        <label>Sunset offset enabled</label>
        <label class="toggle">
          <input type="checkbox" bind:checked={schedule.sunsetSchedule.enabled} />
          <span class="toggle-slider"></span>
        </label>
      </div>
      {#if schedule.sunsetSchedule.enabled}
        <div class="field">
          <label>Sunset offset (min)</label>
          <input type="number" bind:value={schedule.sunsetSchedule.sunsetOffsetMinutes} />
        </div>
      {/if}
    </div>
  {/each}

  <button class="btn btn-secondary" onclick={addSchedule}>+ Add Schedule</button>

  <div class="save-bar">
    <button class="btn btn-primary" onclick={save} disabled={saving}>
      {saving ? 'Saving…' : 'Save'}
    </button>
    {#if saveStatus === 'ok'}
      <span class="status-ok">Saved</span>
    {:else if saveStatus.startsWith('err:')}
      <span class="status-err">{saveStatus.slice(4)}</span>
    {/if}
  </div>
{/if}
```

- [ ] **Step 2: Verify in browser**

```bash
cd ui && npm run dev
```
Schedules tab: no schedules shown (empty array, since no ESP32). Click "+ Add Schedule" — card appears with all fields. Toggle days — checkboxes respond. Click Delete — card removed. Stop with Ctrl+C.

- [ ] **Step 3: Commit**

```bash
git add ui/src/tabs/ScheduleDrip.svelte
git commit -m "feat(ui): implement ScheduleDrip for schedule.json"
```

---

## Task 7: ManualTab

**Files:**
- Modify: `ui/src/tabs/ManualTab.svelte` (replace placeholder)

- [ ] **Step 1: Replace `ui/src/tabs/ManualTab.svelte`**

```svelte
<script>
  import { onMount } from 'svelte';
  import { getConfig, saveConfig } from '../lib/api.js';

  // Internal representation: array of { name, enabled, zones, shuffle, durationMinutes, delayedStart }
  let areas = $state([]);
  let loadError = $state('');
  let saving = $state(false);
  let saveStatus = $state('');

  onMount(async () => {
    try {
      const raw = await getConfig('manual_control.json');
      areas = Object.entries(raw).map(([name, fields]) => ({
        name,
        enabled: fields.enabled === true || fields.enabled === 'true',
        zones: fields.zones ?? [1],
        shuffle: fields.shuffle ?? false,
        durationMinutes: fields.durationMinutes ?? 20,
        delayedStart: fields.delayedStart ?? '0h',
      }));
    } catch (e) {
      loadError = e.message;
    }
  });

  function addArea() {
    areas = [...areas, {
      name: 'new_area',
      enabled: true,
      zones: [1],
      shuffle: false,
      durationMinutes: 20,
      delayedStart: '0h',
    }];
  }

  function deleteArea(i) {
    areas = areas.filter((_, idx) => idx !== i);
  }

  function zonesString(area) {
    return area.zones.join(', ');
  }

  function parseZones(area, value) {
    area.zones = value.split(',').map(s => parseInt(s.trim(), 10)).filter(n => !isNaN(n));
  }

  async function save() {
    saving = true;
    saveStatus = '';
    try {
      const obj = {};
      for (const { name, ...fields } of areas) {
        obj[name] = fields;
      }
      await saveConfig('manual_control.json', obj);
      saveStatus = 'ok';
    } catch (e) {
      saveStatus = 'err:' + e.message;
    } finally {
      saving = false;
    }
  }
</script>

{#if loadError}
  <div class="load-error">Failed to load manual_control.json: {loadError}</div>
{:else}
  {#each areas as area, i}
    <div class="card">
      <div class="card-header">
        <input
          type="text"
          style="font-weight:600;font-size:0.95rem;border:1px solid #d1d5db;border-radius:4px;padding:0.2rem 0.4rem;"
          bind:value={area.name}
          placeholder="area_name"
        />
        <div style="display:flex;gap:0.5rem;align-items:center;">
          <label class="toggle">
            <input type="checkbox" bind:checked={area.enabled} />
            <span class="toggle-slider"></span>
          </label>
          <button class="btn btn-danger" onclick={() => deleteArea(i)}>Delete</button>
        </div>
      </div>

      <div class="field">
        <label>Zones (comma-separated)</label>
        <input
          type="text"
          value={zonesString(area)}
          onchange={(e) => parseZones(area, e.target.value)}
        />
      </div>
      <div class="field">
        <label>Shuffle zones</label>
        <label class="toggle">
          <input type="checkbox" bind:checked={area.shuffle} />
          <span class="toggle-slider"></span>
        </label>
      </div>
      <div class="field">
        <label>Duration (min)</label>
        <input type="number" min="1" bind:value={area.durationMinutes} />
      </div>
      <div class="field">
        <label>Delayed Start (e.g. "2h")</label>
        <input type="text" style="width:80px;" bind:value={area.delayedStart} />
      </div>
    </div>
  {/each}

  <button class="btn btn-secondary" onclick={addArea}>+ Add Area</button>

  <div class="save-bar">
    <button class="btn btn-primary" onclick={save} disabled={saving}>
      {saving ? 'Saving…' : 'Save'}
    </button>
    {#if saveStatus === 'ok'}
      <span class="status-ok">Saved</span>
    {:else if saveStatus.startsWith('err:')}
      <span class="status-err">{saveStatus.slice(4)}</span>
    {/if}
  </div>
{/if}
```

- [ ] **Step 2: Verify in browser**

```bash
cd ui && npm run dev
```
Manual Control tab: no cards (no ESP32). Click "+ Add Area" — card appears with editable name, toggles, fields. Click Delete — removed. Stop with Ctrl+C.

- [ ] **Step 3: Commit**

```bash
git add ui/src/tabs/ManualTab.svelte
git commit -m "feat(ui): implement ManualTab for manual_control.json"
```

---

## Task 8: FirmwareTab

**Files:**
- Modify: `ui/src/tabs/FirmwareTab.svelte` (replace placeholder)

- [ ] **Step 1: Replace `ui/src/tabs/FirmwareTab.svelte`**

The existing `elefant_ota.html` is already embedded and served at `/update`. FirmwareTab embeds it via iframe to avoid reimplementing the MD5 + upload flow.

```svelte
<div class="section" style="padding:0;overflow:hidden;border-radius:8px;">
  <iframe
    src="/update"
    title="Firmware Update"
    style="width:100%;height:580px;border:none;display:block;"
  ></iframe>
</div>
```

- [ ] **Step 2: Verify tab renders**

```bash
cd ui && npm run dev
```
Firmware tab shows an iframe (will display connection error without ESP32 — expected). Stop with Ctrl+C.

- [ ] **Step 3: Commit**

```bash
git add ui/src/tabs/FirmwareTab.svelte
git commit -m "feat(ui): add FirmwareTab with OTA iframe"
```

---

## Task 9: Update Config JSON Files

**Files:**
- Modify: `data/config/schedule.json`
- Modify: `data/config/manual_control.json`

- [ ] **Step 1: Update `data/config/schedule.json`**

Remove `pumpNumber` and `masterValveNumber` from each entry. Replace combo-code `daysOfWeek` with explicit `[1..7]` arrays:

```json
[
  {
    "enabled": true,
    "zones": [1, 2],
    "durationMinutes": 30,
    "daysOfWeek": [1, 2, 3, 4, 5, 6, 7],
    "startTimes": ["7:00", "19:00"],
    "suspendAboveTemp": 35
  },
  {
    "enabled": true,
    "zones": [3, 4],
    "durationMinutes": 30,
    "daysOfWeek": [1, 2, 3, 4, 5, 6, 7],
    "startTimes": ["7:00", "19:00"],
    "sunriseSchedule": {
      "enabled": true,
      "sunriseOffsetMinutes": 10
    },
    "sunsetSchedule": {
      "enabled": true,
      "sunsetOffsetMinutes": 10
    },
    "suspendAboveTemp": 35
  }
]
```

- [ ] **Step 2: Update `data/config/manual_control.json`**

Change `enabled` from string to bool:

```json
{
  "grass_area": {
    "enabled": true,
    "zones": [3, 4],
    "shuffle": true,
    "durationMinutes": 20,
    "delayedStart": "0h"
  },
  "drip_area": {
    "enabled": true,
    "zones": [13, 14],
    "shuffle": true,
    "durationMinutes": 60,
    "delayedStart": "0h"
  }
}
```

- [ ] **Step 3: Commit**

```bash
git add data/config/schedule.json data/config/manual_control.json
git commit -m "fix(config): remove pumpNumber/masterValveNumber from schedules, enabled as bool"
```

---

## Task 10: Update Firmware — `applyAppConfig()` for Nested JSON

**Files:**
- Modify: `src/main.cpp:99-138`

- [ ] **Step 1: Replace `applyAppConfig()` body in `src/main.cpp`**

Replace lines 99–138 with:

```cpp
void applyAppConfig(const JsonDocument& doc) {
  controllerConfig.fillingMaxMinutes        = doc["filling"]["max_minutes"]               | FILLING_MAX_MINUTES;
  controllerConfig.leakageDetectorThreshold = doc["filling"]["leakage_detector_threshold"] | LEAKAGE_DETECTOR_THRESHOLD;
  controllerConfig.levelFilteringSeconds    = doc["filling"]["level_filtering_seconds"]    | LEVEL_FILTERING_SECONDS;
  controllerConfig.highLevelPressure        = doc["filling"]["high_level_pressure"]        | HIGH_LEVEL_PRESSURE;
  controllerConfig.lowLevelPressure         = doc["filling"]["low_level_pressure"]         | LOW_LEVEL_PRESSURE;

  controllerConfig.grassMaxMinutes              = doc["grass_irrigation"]["max_minutes"]            | GRASS_MAX_MINUTES;
  controllerConfig.grassPumpStartDelaySeconds   = doc["grass_irrigation"]["pump_start_delay_seconds"] | GRASS_PUMP_START_DELAY_SECONDS;
  controllerConfig.numberOfGrassZones           = doc["grass_irrigation"]["number_of_zones"]        | MAX_NUMBER_OF_GRASS_ZONES;

  controllerConfig.dripMaxMinutes           = doc["drip_irrigation"]["max_minutes"]     | DRIP_MAX_MINUTES;
  controllerConfig.numberOfDripZones        = doc["drip_irrigation"]["number_of_zones"] | MAX_NUMBER_OF_DRIP_ZONES;

  controllerConfig.buttonFilteringMs        = doc["button_filtering_ms"] | BUTTON_FILTERING_MS;

  fillingMaxMs = controllerConfig.fillingMaxMinutes * 60 * 1000UL;
  grassMaxMs   = controllerConfig.grassMaxMinutes   * 60 * 1000UL;
  dripMaxMs    = controllerConfig.dripMaxMinutes    * 60 * 1000UL;

  levelFilteringMsThreshold = controllerConfig.levelFilteringSeconds * 1000UL;
  filterState.threshold[TANK_UPPER_LIMIT2_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_UPPER_LIMIT1_SWITCH - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_UPPER_MID_SWITCH    - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_LOWER_MID_SWITCH    - 1] = levelFilteringMsThreshold;
  filterState.threshold[TANK_LOWER_LIMIT_SWITCH  - 1] = levelFilteringMsThreshold;
  filterState.threshold[BUTTON_FILLING     - 1] = controllerConfig.buttonFilteringMs;
  filterState.threshold[BUTTON_GRASS       - 1] = controllerConfig.buttonFilteringMs;
  filterState.threshold[BUTTON_DRIP        - 1] = controllerConfig.buttonFilteringMs;
  filterState.threshold[BUTTON_ZONE_SWITCH - 1] = controllerConfig.buttonFilteringMs;

  if (controllerConfig.numberOfGrassZones > 6) {
    Serial.println("Warning: number_of_grass_zones exceeds maximum supported 6. Limiting to 6.");
    controllerConfig.numberOfGrassZones = 6;
  }
  grassZones[0] = GRASS_ZONE_1;
  grassZones[1] = GRASS_ZONE_2;
  grassZones[2] = GRASS_ZONE_3;
  grassZones[3] = GRASS_ZONE_4;
  grassZones[4] = GRASS_ZONE_5;
  grassZones[5] = GRASS_ZONE_6;
}
```

- [ ] **Step 2: Verify firmware compiles**

```bash
cd /path/to/irrigation-controller && pio run -e denky32 2>&1 | tail -20
```
Expected: `SUCCESS` — no compile errors.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "fix(firmware): update applyAppConfig to parse nested app_config.json structure"
```

---

## Task 11: Build and Wire Up

**Files:**
- No new files — verify build chain end-to-end

- [ ] **Step 1: Run Svelte build**

```bash
cd ui && npm run build
```
Expected: Vite prints build summary, files written to `../data/`. Check:
```bash
ls ../data/
```
Should show `index.html` plus `assets/` directory alongside existing `config/`, `style.css`, `elefant_ota.html`.

- [ ] **Step 2: Run embed_static.js**

```bash
cd .. && node tools/embed_static.js --input data --output include/embedded_files.h --gzip
```
Expected: `Wrote include/embedded_files.h with N files (gzip= true)` — N should be > 5.

- [ ] **Step 3: Compile firmware**

```bash
pio run -e denky32 2>&1 | tail -20
```
Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add include/embedded_files.h data/index.html data/assets/
git commit -m "build: compile Svelte UI and regenerate embedded_files.h"
```

---

## Self-Review Checklist

| Spec requirement | Task |
|---|---|
| App Settings form (all fields) | Task 4 |
| Backup / restore | Task 5 |
| Schedules: add/delete, day checkboxes, start times add/delete | Task 6 |
| Manual Control: add/delete areas, editable name, bool enabled | Task 7 |
| Firmware OTA tab | Task 8 |
| `enabled` → bool in manual_control.json | Task 9 |
| `pumpNumber`/`masterValveNumber` removed from schedules | Task 9 |
| `daysOfWeek` combo codes → explicit [1..7] | Task 6 (load normalises), Task 9 |
| Firmware `applyAppConfig` reads nested JSON | Task 10 |
| Error handling: load error shown, save error shown | Tasks 4,6,7 |
| Save button disabled during request | Tasks 4,6,7 |
| Build chain verified | Task 11 |
