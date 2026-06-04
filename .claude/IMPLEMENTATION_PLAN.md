# Svelte UI Implementation Plan

## Current Architecture

This project is an ESP32 irrigation controller with a Svelte SPA embedded into the firmware image.

- Firmware: PlatformIO project (`env:denky32`) using Arduino ESP32, ESPAsyncWebServer, LittleFS, AsyncWebSocket, OTA update support, physical buttons, sensors, and OLED display.
- UI: Vite + Svelte app in `ui/`, rendered as a single-page app and served from PROGMEM via `include/embedded_files.h`.
- Configuration: JSON files live at runtime in LittleFS under `/config/*.json`. The checked-in working copies are in `data/config/`; canonical sample schemas/default sources are in `data/config/samples/`.
- Preview: `preview.html` is a dependency-free mock of the production UI and must stay in sync with changes under `ui/src/**`.

## Build And Embedding Workflow

The current workflow is implemented by `platformio.ini`, `tools/embed_ui.py`, `tools/embed_configs.py`, and `tools/embed_static.js`.

1. Build firmware with PlatformIO. The `denky32` environment runs these pre-build scripts:
   - `tools/embed_configs.py`: embeds `data/config/samples/` into `include/embedded_configs.h` using URL prefix `/config/`.
   - `tools/embed_ui.py`: runs `npm run build` in `ui/`, then embeds the fresh `ui/dist/` directory into `include/embedded_files.h` using URL prefix `/` and gzip compression.
2. Firmware includes `include/embedded_files.h` in `src/webserver_embedded.cpp` and serves `/index.html`, `/assets/*`, and other UI assets from PROGMEM.

Important workflow notes:

- `data/` is not the Svelte build output. Do not copy `ui/dist` into `data/`.
- `tools/embed_ui.py` owns the production UI build before embedding; run `npm run build` manually only when you want a quick UI-only verification.
- `data/config/samples/` is source material for default/sample configs and schema review. Do not edit those files without explicit user confirmation.
- `include/embedded_files.h` and `include/embedded_configs.h` are generated files. Regenerate them through the build scripts instead of editing by hand.
- Current implementation detail to keep in mind: `include/embedded_configs.h` is generated, but runtime default copying in `src/webserver_embedded.cpp` currently scans the `embedded_files` manifest from `include/embedded_files.h`. If default config reset/copy behavior is changed, wire `embedded_configs.h` into the firmware explicitly or copy samples into LittleFS by another deliberate path.

## Runtime Interfaces

The ESP32 serves:

- Static SPA routes: `/`, `/index.html`, `/assets/*`, with SPA fallback to `/index.html`.
- Recovery page: `/recovery`, plus fallback filesystem upload/status APIs under `/api/fallback/*`.
- Config REST API:
  - `GET /api/config`
  - `GET /api/config/<file>.json`
  - `POST /api/config/<file>.json`
- OTA:
  - `GET /update`
  - `GET /ota/start`
  - `POST /ota/upload`
- WebSocket: `/ws`, with command, status, config, save, reset, and event messages. See `websocket_protocol.md` for the protocol contract.

## UI Structure

```
ui/src/
├── App.svelte              (emergency bar, WS indicator, tab nav, tab rendering)
├── assets/app.css          (global styles)
├── lib/
│   ├── api.js              (REST API helpers)
│   └── ws.svelte.js        (WebSocket client, shared reactive state)
└── tabs/
    ├── ManualTab.svelte        (all areas' manual controls + filling control)
    ├── AreaTab.svelte          (per-area schedule configuration)
    ├── ScheduleCard.svelte     (individual schedule entry card)
    ├── FillScheduleTab.svelte  (filling schedule configuration tab)
    ├── FillScheduleCard.svelte (individual filling schedule entry card)
    ├── SettingsTab.svelte      (app config, WiFi, filling config, area hardware config)
    └── FirmwareTab.svelte      (OTA update iframe)
```

## Configuration Management

Use the sample files as the schema reference instead of duplicating the full schema in this document:

- `data/config/samples/app_config.sample.json`
- `data/config/samples/manual_control.sample.json`
- `data/config/samples/schedule.sample.json`

Key invariants:

- Area keys in `schedule.json` and `manual_control.json` must match `app_config.json` area `id` values.
- `manual_control.json` and `schedule.json` use zone groups: `zones` is an array of groups, where each group is an array of zone IDs that run simultaneously.
- The UI keeps backward compatibility with legacy flat zone arrays by migrating them to single-zone groups.
- Runtime writes go to LittleFS `/config/*.json` through the REST API or WebSocket config messages.

## WebSocket Real-Time Communication

#### Connection
- UI opens WebSocket to `ws://<host>/ws` on load
- Auto-reconnect with exponential backoff
- Connection status indicator shown above tabs as colored dot:
  - **Green**: connected
  - **Orange**: reconnecting
  - **Red**: disconnected
- When no message received for >3s, show warning tooltip with the last time connection was alive and change status to disconnected: "Disconnected since HH:MM:SS."
- When an error message is received, show error tooltip with message content and change status to disconnected: "Error: {message}"
- When connection is re-established, show success tooltip: "Reconnected at HH:MM:SS."

#### Controller → UI (status updates)
- Pushed on state change and periodically (heartbeat)
- Payload includes:
  - Filling state (running, water level)
  - Sensor readings: water level pressure, pump status per pump, valve status per valve, temperature, humidity, rain sensor
  - Active tasks: areas/zones, schedule execution progress, manual control timers (delayed start countdown, zone duration remaining)
  - Uptime, errors, firmware version, diagnostic info 
- UI updates all tabs reactively from WebSocket status messages

#### UI -> Controller (commands)
- Manual start/stop irrigation per area
- Manual start/stop filling
- Emergency commands: `pause_1h` pauses a target for 1 hour; `stop` cancels a pause or stops an active run
- Commands sent as JSON: `{"type":"command","action":"start|stop|pause_1h","target":"Grass|Drip|Filling"}`
- Controller acknowledges with status update

## UI Behavior

### Global Rules

- Keep layouts stable. Do not move controls based on runtime state.
- Prefer disabled controls with explanatory `title` tooltips over hiding controls.
- It is acceptable to hide whole schedule tabs and emergency buttons for disabled areas because those functions do not apply.
- Keep the UI mobile-first with touch-friendly controls and horizontally scrollable tabs on narrow screens.
- When `ui/src/**` changes, update `preview.html` in the same change.

### Emergency Bar 
 
- Located above tabs, never scrolled and always visible, not resizable
- One button for filling, one per each area - in this order
- Behavior depends on the **source** of the active operation (`manuallyStarted` vs `scheduleActive` from WS status):
  - **Schedule-active** → 3-state cycle:
    1. **Active**: `{Area}: Pause 1h` in area color — click sends `pause_1h` (firmware stops the run and blocks `start` for 1h).
    2. **Paused**: `{Area}: STOP` in dimmer color — click sends `stop` to cancel the 1h timer early.
    3. **Inactive**: grayed out, non-interactive — auto-transitions when a process starts.
  - **Manual-active** → 2-state, no pause:
    1. **Active**: `{Area}: STOP` in area color — click sends `stop` directly. **`pause_1h` is never sent for manual runs.** Manual control runs are stopped, not paused.
    2. **Inactive**: grayed out.
- Source is determined live from `ws.status.<target>.manuallyStarted`. If the area was started by schedule and the user later clicks Manual Start while running, the Manual click overrides; subsequent emergency clicks treat it as manual.

### Manual Control Tab

This tab is the most left tab and acts as default
All areas' manual controls are placed in this single tab. **Filling card is rendered first (top), followed by per-area cards.** No Save button — manual control state is transient and applied via `start`/`stop` commands; it is not persisted to `manual_control.json` from this tab.

**Filling card (top, compact single-line layout):**
- Single horizontal row: `[Filling title] [Start/Stop button]  ...  [Pump N status] [Water level]`
- Pump status pushed to the right (Pump first, Water level second).
- Orange accent border (`#fb923c`); reduced padding to keep the row tight.
- Start/Stop button — same two-state logic as per-area button (Start when idle, Stop when running). Enable rules: not schedule-activated, filling enabled, and not currently `pausedUntil != null`. Filling has no zone/duration prerequisites.

**Per-area cards:**
- Card header: `[Area title] [Start/Stop button]` — button rendered immediately to the right of the title (left-grouped, not space-between).
- Start/Stop button — two-state only. **No Resume.** Pause/resume semantics belong to the schedule emergency flow, not to manual control.
  - **Start** (area color background): shown when area is idle. **Disabled** if any of: area is schedule-activated, area enable flag is false, no zones selected, duration `<= 0`, or `pausedUntil != null` (paused by schedule). Tooltip explains the disabled reason (e.g. `Paused by schedule`, `No zones selected`). Click sends `start`.
  - **Stop** (red background `#ef4444`): shown when area is manually running. Click sends `stop` directly.
- Disabled buttons render with reduced opacity + grayscale filter so the disabled state is visually distinct from the enabled colored state.
- Active task status (zone, time remaining) when running
- **Presets row** (named snapshots of all per-area parameters):
  - Renders chips for each entry of `manual_control.json`'s `<area>.presets` array.
  - Each chip exposes 4 actions: **load** (click chip name → applies preset's zones/shuffle/duration/delayedStart to the working form), **↻ overwrite** (replaces preset's stored values with current working values, with confirm), **✎ rename** (prompt; rejects duplicate names within the same area), **✕ delete** (with confirm).
  - **+ Save as preset** button at the end of the row: prompts for a name, snapshots current working values into a new preset; if the name already exists, asks to overwrite.
  - Add/overwrite/rename/delete operations auto-persist `manual_control.json` via the existing `saveConfig` API. No manual Save button is required (and none is rendered on this tab).
- Zone selection checkboxes (from area's zone_ids/zone_names)
  - multiple zones can be selected only while the area is idle. While active, the zones must be selected with radio buttons such as only one and exactly one zone can be active at a time. 
- Shuffle button - one time randomization of zone order per click.
  - **While running**: active group keeps running, relocated to a random new position (any slot), editor cursor follows it there.
  - **While stopped**: full Fisher–Yates shuffle, cursor resets to 0.
  - **Selected-group cursor invariant**: highlighted chip always shows the *currently active* group while running (tracked in real-time via `activeZones` content-match) or *group 0* while stopped. Cursor snaps to 0 on every start and every stop.
  - **Emergency bar active-group highlight**: identified by content-match of `activeZones` (sorted zone IDs joined as string) against the UI's current group list — **not** by firmware's `activeGroupIndex` positional field — so it stays correct after a UI shuffle.
- Duration per zone input
- Delayed start presets on same line as input (0m, 1m, 5m, 10m, 20m, 30m, 1h, 2h)

### Schedule Tabs

- Global enable/disable toggle for each area schedules
- Temperature suspension config
- List of schedule cards (add/edit/delete)
- Each card: zones, duration, days, start times, sunrise/sunset options
- Save button for schedule.json

**Compact layout rule**: titles and controls on same line wherever possible:
- Schedule card header: "{Area} Schedule N" + enabled toggle + delete button on one line
- Delayed start: label + preset buttons + input all on one line
- Add Schedule + Save buttons on one line

### Settings Tab

- Allows editing all parameters existing in `data/config/samples/app_config.sample.json` (schema reference)
- Loads via WS `get_config` and saves via WS `save_config` to LittleFS `/config/app_config.json`
- Backup / restore all configs as a separate file on LittleFS (e.g. `/config/app_config_bkp1.json`)
- Reset app_config via WS `reset_config`, which copies `/config/samples/app_config.sample.json` → `/config/app_config.json` on LittleFS

### Firmware Tab

- always at the last position
- OTA update via iframe to `/update`
- Backend OTA workflow used by the page:
  - `GET /ota/start` initializes update (supports `mode` and optional `hash` query params)
  - `POST /ota/upload` streams firmware/filesystem binary payload

### System Status

- Sensor readings: temperature, humidity, rain, water level
- Per-pump and per-valve status
- Active task info with progress shown in area and filling tabs, as well as in emergency button areas

## Implementation Focus

- Keep `ui/src/**` and `preview.html` visually and behaviorally aligned.
- Keep `websocket_protocol.md`, firmware handlers, and UI WebSocket messages aligned when changing commands or status fields.
- Keep generated embedded headers out of hand edits; change the source files and regenerate.
- Validate UI changes with `npm run build` from `ui/`; validate firmware-impacting changes with `pio run` when feasible.

## Technical Considerations

- ESP32 flash and RAM constrain embedded asset size and JSON parsing.
- Vite output must remain small enough for firmware embedding.
- WebSocket status should remain the source of truth for running/paused/manual/schedule state.
- Configuration validation should happen on both UI and firmware paths when adding fields.
