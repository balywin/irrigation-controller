# Config Editor UI — Design Spec

**Date:** 2026-04-28  
**Status:** Approved

## Goal

Build a Svelte SPA that lets users view and edit all three device config files (`app_config.json`, `schedule.json`, `manual_control.json`) via structured form fields, served from the ESP32 itself.

---

## Architecture

### Tech Stack
- **Svelte + Vite** in `ui/` directory
- Build output → `data/` → `tools/embed_static.js` → `embedded_files.h` → firmware

### Build Chain
```
npm run build (ui/)  →  data/  →  embed_static.js  →  embedded_files.h  →  PlatformIO compile
```

### File Structure
```
ui/
├── package.json
├── vite.config.js        (output: ../data/)
├── index.html
└── src/
    ├── App.svelte         (tab shell)
    ├── lib/
    │   └── api.js         (getConfig / saveConfig)
    ├── tabs/
    │   ├── AppTab.svelte
    │   ├── ScheduleDrip.svelte
    │   ├── ManualTab.svelte
    │   └── FirmwareTab.svelte
    └── assets/
        └── app.css
```

---

## API Layer (`lib/api.js`)

Backend endpoints already implemented in `webserver_embedded.cpp`:

| Method | URL | Purpose |
|--------|-----|---------|
| GET | `/api/config/<name>` | Fetch config JSON from LittleFS |
| POST | `/api/config/<name>` | Save config JSON to LittleFS |

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

---

## Data Flow (per tab)

1. Tab mounts → `getConfig('<name>.json')` → populate local reactive state
2. User edits form → Svelte reactive bindings update local state
3. User clicks **Save** → `saveConfig('<name>.json', state)` → POST to ESP32
4. Success/error shown inline (no toast library — simple text feedback)

---

## Tabs

### Tab 1 — App Settings (`app_config.json`)

Top-level:
- `device_name` — text input

`filling` section:
| Field | Input type |
|-------|-----------|
| `enabled` | toggle |
| `max_minutes` | number |
| `level_filtering_seconds` | number |
| `leakage_detector_threshold` | number |
| `high_level_pressure` | number |
| `low_level_pressure` | number |

`grass_irrigation` section (same shape for `drip_irrigation`):
| Field | Input type |
|-------|-----------|
| `enabled` | toggle |
| `description` | text |
| `pump_number` | number |
| `pump_start_delay_seconds` | number |
| `main_valve_number` | number |
| `number_of_zones` | number |
| `max_minutes` | number |

---

### Tab 2 — Schedules (`schedule.json`)

Array of schedule cards. User can **Add** (appends blank card) and **Delete** (removes card).

Each card fields:
| Field | Input type |
|-------|-----------|
| `enabled` | toggle |
| `zones` | comma-separated number input |
| `durationMinutes` | number |
| `suspendAboveTemp` | number |
| `startTimes` | list of time inputs (add/delete per item) |
| `daysOfWeek` | Mon–Sun checkboxes (Mon=1 … Sun=7, ISO week; stored as `[1..7]` subset) |
| `sunriseSchedule.enabled` | toggle |
| `sunriseSchedule.sunriseOffsetMinutes` | number (visible when sunrise enabled) |
| `sunsetSchedule.enabled` | toggle |
| `sunsetSchedule.sunsetOffsetMinutes` | number (visible when sunset enabled) |

**Schema change:** `pumpNumber` and `masterValveNumber` removed from schedule objects. These are global device settings configured in App Settings tab. Firmware must read pump/valve config from `app_config.json` only.

**Schema change:** `daysOfWeek` combo codes (e.g. `135`, `246`) replaced by explicit day array. UI saves only values `1–7`.

---

### Tab 3 — Manual Control (`manual_control.json`)

Object of named area cards. User can **Add** (new area with default values) and **Delete** (removes key).

Each card fields:
| Field | Input type |
|-------|-----------|
| area name (object key) | editable text |
| `enabled` | toggle (bool) |
| `zones` | comma-separated number input |
| `shuffle` | toggle |
| `durationMinutes` | number |
| `delayedStart` | text (format: `"Nh"`, e.g. `"2h"`) |

**Schema change:** `enabled` saved as `bool` (was string `"true"`/`"false"`). Firmware parser must be updated to match.

---

### Config Backup / Restore (App Settings tab, bottom section)

**Backup:**
- Button "Download backup" — fetches all 3 configs via `getConfig`, merges into single object, triggers browser file download as `irrigation-backup-<date>.json`
- Combined file format:
```json
{
  "app": { ...app_config.json contents... },
  "auto_schedule": [ ...schedule.json contents... ],
  "manual_control": { ...manual_control.json contents... }
}
```

**Restore:**
- File picker (`.json` only) — "Restore from backup"
- On file select: parse JSON, validate top-level keys (`app`, `auto_schedule`, `manual_control`), POST each to `/api/config/<name>.json`
- On success: reload all tab data from device
- On partial failure: report which files failed, leave others saved

---

### Tab 4 — Firmware (OTA)

- File picker (`.bin` only)
- Upload progress bar
- Reboot button
- Uses existing `/ota/upload` + `/ota/start` endpoints from `ElefantOTA.cpp`

---

## Known Firmware Parsing Mismatch

`applyAppConfig()` in `src/main.cpp` currently reads flat keys (e.g. `doc["filling_max_minutes"]`) but `app_config.json` uses nested objects (`filling.max_minutes`). The UI reads and writes the nested structure as it exists in the file. Firmware parser must be updated separately to consume the nested format — this is pre-existing debt, not introduced by this feature.

---

## Schema Changes Required

These changes are made by the UI on save — firmware parsing must be updated accordingly:

| File | Field | Before | After |
|------|-------|--------|-------|
| `manual_control.json` | `enabled` | `"true"` / `"false"` (string) | `true` / `false` (bool) |
| `schedule.json` | `daysOfWeek` | mixed day + combo codes | explicit `[1..7]` subset only |
| `schedule.json` | `pumpNumber` | present | removed |
| `schedule.json` | `masterValveNumber` | present | removed |

---

## Error Handling

- Failed GET on mount: show error message in tab body, disable Save
- Failed POST on save: show inline error, keep form state (do not reset)
- No loading spinners beyond a simple disabled-state on Save button during request

---

## Out of Scope

- Authentication / access control
- Real-time status display (separate future feature)
- WebSocket / live polling
