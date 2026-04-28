# Svelte UI Implementation Plan

## Current Architecture Analysis

This ESP32 irrigation controller project currently has:
- PlatformIO-based ESP32 firmware with ESPAsyncWebServer
- Static file serving from embedded PROGMEM arrays via `embedded_files.h`
- JSON-based configuration stored in LittleFS (`/config/*.json`)
- Build tool (`tools/embed_static.js`) to convert static files to C header
- Sensor reading subsystem (water level, pump status, valve status, temperature, humidity, rain)
- Task runner tracking status of schedule execution and manual control operations
- Captive portal mode for initial WiFi setup
- Physical buttons + OLED screen for local control

## Implementation Strategy

### 1. Svelte Application Architecture

Create a complete Svelte SPA with:

- **Emergency bar** (above all tabs, always visible, not scrollable):
  - One button for filling + one per irrigation area (Grass, Drip, etc.)
  - **Water level indicators**: filling button always shows water tank level % in the center, color-coded (green ≥60%, yellow 30-59%, red <30%); on the left side of the button, show a large color coded graphic indicator, similar to battery indicators on smartphones, that visually represents the water level in the tank (e.g. a droplet icon that fills up with color as the level increases, with the fill color changing based on the thresholds mentioned above). It should animate filling when the filling is active.
  - **Active zone indicators**: when a process is active, show all zone groups on the left side of the button, next to the label, but show the currently active zone group highlighted; update in real-time as zones change during execution
  - 3-state cycle when started by a schedule or 2-state cycle when manually started, per button:
    1. **ON** (process running): "{Area}: Pause 1h" in area color (green, blue, etc.) — clicking sends `pause_1h` command, stops manual operation and disables global schedule flag for 1 hour
    2. **Paused for <nn>min** (when 1h timer running) in a semi-active/dimmer color — clicking sends `off` command, which cancels the timer
    3. **OFF** (no process): grayed out — no action until process starts again (by schedule or manual)

- **WebSocket connection indicator**: colored icon (green/orange/red) in nav area; tooltip on hover shows last connection time and error messages if any

- **Tab-based navigation** (left to right):
  - **Manual Control Tab** (default, leftmost): all areas' manual controls (zone selection, shuffle, duration, delayed start, start/stop) + filling control (start/stop, water level, pump status)
    - Show small dots next to the tab title in 2 raws - all for manual activated processes in the color of the function. Each dot to have fixed location, not to move around as the states change.
    - Show a small dot on the left side of the tab title for manual activated filling in the color of the function
  - **\<Area\> Schedule Tab** (one per area, e.g. Grass, Drip): schedule configuration for that area
    - Show a small dot in the area color on the right side of corresponding schedule tab title, if any schedule operation is currently active
  - **Settings Tab**: application configuration (device name, WiFi, filling config, per-area hardware config)
  - **Firmware Tab**: OTA firmware upload interface

- **UI visibility rules**: 
  - never hide UI elements based on state; show them disabled with tooltip explaining why (e.g. "disabled because water level is low")
  - never move UI elements around based on state; keep a consistent layout and use disabled states to indicate when actions are not possible
  - Exception: the schedule tabs themselves and the emergency buttons can be hidden if the corresponding area is disabled in settings, since they are not applicable at all in that case.

- **Mobile-first design**: UI must be usable on smartphones as well as desktops
  - Responsive layout that adapts to small screens
  - Touch-friendly controls (large enough tap targets)
  - Horizontally scrollable tab bar on narrow screens
  - Emergency buttons wrap to 2-column or 3-column grid on mobile, depending on the number of areas, to maintain large tap targets while fitting the screen width
  - Input fields go full-width on mobile

### 2. Development Workflow

- Use Vite + Svelte in the existing `ui/` directory
- Build system outputs to `data/` for static embedding
- ESP32 serves the compiled Svelte app via embedded files
- API endpoints for configuration CRUD operations

### 3. API Integration

The ESP32 will serve:
- **Static Files**: Svelte app (`/`, `/index.html`, `/assets/*`)
- **API Endpoints**:
  - `GET/PUT /config/app_config.json` (device settings)
  - `GET/PUT /config/schedule.json` (irrigation schedules)
  - `GET/PUT /config/manual_control.json` (manual controls)
  - `GET /update` (OTA web UI page)
  - `GET /ota/start` (OTA session start: mode/hash validation and update init)
  - `POST /ota/upload` (OTA binary upload)
  - `GET /status` (system status)
- **WebSocket** (`ws://<host>/ws`):
  - Bidirectional real-time channel between UI and controller
  - Controller → UI: status updates (running zones, filling state, sensor readings, errors)
  - UI → Controller: user commands (start/stop irrigation, start/stop filling, pause_1h, off, etc.)
  - JSON message format with `type` field for routing
  - Auto-reconnect on disconnect with exponential backoff
  - WebSocket commands (to the backend) and events (from the backend) are defined in `websocket_protocol.md` file.

### 4. UI Components Structure

```
ui/src/
├── App.svelte              (emergency bar, WS indicator, tab nav, tab rendering)
├── assets/app.css          (global styles)
├── lib/
│   ├── api.js              (REST API helpers)
│   └── ws.svelte.js        (WebSocket client, shared reactive state)
└── tabs/
    ├── ManualTab.svelte     (all areas' manual controls + filling control)
    ├── AreaTab.svelte       (per-area schedule configuration)
    ├── ScheduleCard.svelte  (individual schedule entry card)
    ├── SettingsTab.svelte   (app config, WiFi, filling config, area hardware config)
    └── FirmwareTab.svelte   (OTA update iframe)
```

### 5. Build Process Integration

- Modify existing build process to:
  1. Run `tools/embed_static.js` on `data/config/samples/` → `include/embedded_configs.h` (via `tools/embed_configs.py` pre-build script)
  2. Run `npm run build` in `ui/` directory
  3. Copy built files to `data/`
  4. Run `tools/embed_static.js` on `data/` → `include/embedded_files.h`
  5. Compile firmware with embedded UI and embedded default configs

### 6. Configuration Management

Config files live in LittleFS at `/config/*.json`. Canonical schema is defined by sample files in `data/config/samples/` — treat those as the authoritative specification.

#### `app_config.json` — Schema

| Field | Type | Description |
|---|---|---|
| `device_name` | string | Human-readable device identifier |
| `wifi_ssid` | string | WiFi network name |
| `wifi_password` | string | WiFi password |
| `filling` | object | Tank auto-fill subsystem config |
| `areas` | array | Irrigation areas (dynamic, order preserved) |

**`filling` object:**

| Field | Type | Description |
|---|---|---|
| `enabled` | bool | Enable auto tank-filling |
| `max_minutes` | int | Max fill cycle duration before abort |
| `pump_id` | int | Hardware pump index (1-based) |
| `level_filtering_seconds` | int | Debounce delay for water level sensors |
| `leakage_detector_threshold` | int | Max fill cycles without irrigation before leakage alarm |
| `high_level_pressure` | int | Pressure sensor raw ADC value for tank full (Pa × scale) |
| `low_level_pressure` | int | Pressure sensor raw ADC value for tank empty (negative = below sensor) |

**Area object (element of `areas` array):**

| Field | Type | Description |
|---|---|---|
| `id` | string | Area identifier used as key in schedule/manual_control (e.g. `"Grass"`) |
| `enabled` | bool | Enable this irrigation subsystem |
| `description` | string | Human label |
| `pump_id` | int | Hardware pump index (1-based) |
| `pump_start_delay_seconds` | int | Delay after valve open before pump starts |
| `main_valve_id` | int | Hardware main valve index (1-based) |
| `zone_ids` | array of int | Hardware zone numbers for this area (1-based) |
| `zone_names` | array of string | Human-readable names matching `zone_ids` by index |
| `max_minutes` | int | Safety cap — max run time per zone |

Example: see `data/config/samples/app_config.sample.json`.

#### `schedule.json` — Schema

Top-level keys are area `id` strings matching `app_config.json` areas. Each area object:

| Field | Type | Description |
|---|---|---|
| `enabled` | bool | Area scheduling active |
| `suspendAboveTempEnabled` | bool | Enable temperature-based suspension |
| `suspendAboveTemp` | int | Suspend irrigation when temp exceeds this value (°C) |
| `suspendOnRainEnabled` | bool | Enable rain-based suspension |
| `suspendOnRainAbove` | int | Suspend irrigation when rain accumulation exceeds this value (L/m²) |
| `schedules` | array | List of schedule entries |

**Schedule entry object:**

| Field | Type | Description |
|---|---|---|
| `enabled` | bool | This schedule entry active |
| `zones` | array of int | Zone IDs to irrigate (subset of area's `zone_ids`) |
| `durationMinutes` | int | Run duration per zone in minutes |
| `daysOfWeek` | array of int | Days to run (1=Mon … 7=Sun) |
| `startTimes` | array of string | Fixed start times in `"HH:MM"` format |
| `sunriseSchedule` | object | `{ "enabled": bool, "sunriseOffsetMinutes": int }` |
| `sunsetSchedule` | object | `{ "enabled": bool, "sunsetOffsetMinutes": int }` |

Example: see `data/config/samples/schedule.sample.json`.

#### `manual_control.json` — Schema

Top-level keys are area `id` strings matching `app_config.json` areas. Each area object:

| Field | Type | Description |
|---|---|---|
| `enabled` | bool | Area active for manual run |
| `zones` | array of int | Zone IDs to activate |
| `shuffle` | bool | Randomize zone order each run |
| `durationMinutes` | int | Run duration per zone in minutes |
| `delayedStart` | string | Delay before start, format `"Nm"` (e.g. `"30m"`, `"0m"` = immediate) |
| `presets` | array of object | Saved named snapshots of `{zones, shuffle, durationMinutes, delayedStart}` for one-click recall. Each entry has fields `name` (string, unique within area) plus the four parameter fields. May be empty `[]`. |

Example: see `data/config/samples/manual_control.sample.json`.

Area keys must match `id` fields in `app_config.json`. Firmware iterates all top-level keys dynamically.

### 6a. Sample Config Files as Firmware Defaults

Sample files in `data/config/samples/` serve two roles:
1. **Specification** — canonical schema reference; do not modify without explicit confirmation
2. **Firmware defaults** — embedded in firmware binary (PROGMEM) via `include/embedded_configs.h`; firmware copies them to LittleFS on first boot or when config files are missing/corrupted

**Build step:** A PlatformIO pre-build script (`tools/embed_configs.py`) runs `tools/embed_static.js` against `data/config/samples/` before each firmware build, regenerating `include/embedded_configs.h`. This guarantees sample content is always in sync with the binary.

### 7. WebSocket Real-Time Communication

#### Connection
- UI opens WebSocket to `ws://<host>/ws` on load
- Auto-reconnect with exponential backoff (1s → 2s → 4s → … max 10s)
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

#### UI → Controller (commands)
- Manual start/stop/resume irrigation per area
- Manual start/stop/resume filling
- Emergency commands: `pause_1h` (pause area/filling for 1 hour), `off` (cancel pause, full stop)
- Commands sent as JSON: `{"type":"command","action":"start|stop|pause_1h|off","target":"Grass|Drip|filling"}`
- Controller acknowledges with status update

### 8. Features Implementation

#### Emergency Bar (above tabs, always visible)
- One button per area + one for filling.
- Behavior depends on the **source** of the active operation (`manuallyStarted` vs `scheduleActive` from WS status):
  - **Schedule-active** → 3-state cycle:
    1. **Active**: `{Area}: Pause 1h` in area color — click sends `pause_1h` (firmware stops the run and blocks `start` for 1h).
    2. **Paused**: `{Area}: STOP` in dimmer color — click sends `stop` to cancel the 1h timer early.
    3. **Inactive**: grayed out, non-interactive — auto-transitions when a process starts.
  - **Manual-active** → 2-state, no pause:
    1. **Active**: `{Area}: STOP` in area color — click sends `stop` directly. **`pause_1h` is never sent for manual runs.** Manual control runs are stopped, not paused.
    2. **Inactive**: grayed out.
- Source is determined live from `ws.status.<target>.manuallyStarted`. If the area was started by schedule and the user later clicks Manual Start while running, the Manual click overrides; subsequent emergency clicks treat it as manual.

#### Manual Control Tab (default, leftmost)
All areas' manual controls in a single tab. **Filling card is rendered first (top), followed by per-area cards.** No Save button — manual control state is transient and applied via `start`/`stop` commands; it is not persisted to `manual_control.json` from this tab.

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
- Duration per zone input
- Delayed start presets on same line as input (0m, 1m, 5m, 10m, 20m, 30m, 1h, 2h)

#### Schedule Tabs (one per area)
- Global enable/disable toggle for area schedules
- Temperature suspension config
- List of schedule cards (add/edit/delete)
- Each card: zones, duration, days, start times, sunrise/sunset options
- Save button for schedule.json

**Compact layout rule**: titles and controls on same line wherever possible:
- Schedule card header: "{Area} Schedule N" + enabled toggle + delete button on one line
- Delayed start: label + preset buttons + input all on one line
- Add Schedule + Save buttons on one line

#### Settings Tab
- All parameters from `data/config/samples/app_config.sample.json`:
  - Device name, WiFi SSID/password
  - Filling: enabled, max minutes, pump ID, level filtering, leakage threshold, high/low level pressure
  - Per-area: enabled, description, pump ID, pump start delay, main valve ID, zone IDs/names, max minutes
- Backup / restore all configs

#### Firmware Tab
- OTA update via iframe to `/update`
- Backend OTA workflow used by the page:
  - `GET /ota/start` initializes update (supports `mode` and optional `hash` query params)
  - `POST /ota/upload` streams firmware/filesystem binary payload

#### System Status (shown in area tabs and filling tab)
- Sensor readings: temperature, humidity, rain, water level
- Per-pump and per-valve status
- Active task info with progress

### 9. UI Visibility Rule

Never hide UI elements based on the system state. Instead:
- Show elements in **disabled** state when not applicable
- Add **tooltip** explaining why disabled (e.g. "Disabled: water level is low", "Disabled: area not enabled")
- Buttons, toggles, inputs all follow this rule
UI elements could be hidden only in case of functionality disabled in Settings tab - as <Area> Schedule tab itself and emergency buttons 

### 10. Implementation Steps

1. **Setup** — Vite + Svelte dev environment in `ui/`
2. **Infrastructure** — API layer, WebSocket client, shared stores
3. **App shell** — Emergency bar, WS indicator, tab navigation
4. **Area tabs** — Manual control + schedule per area
5. **Filling tab** — Filling manual control
6. **Settings tab** — App config editor
7. **Firmware tab** — OTA iframe
8. **Build integration** — Embedded file generation, verify on ESP32
9. **Testing** — Hardware testing, responsive design, validation

## Technical Considerations

- **Memory Constraints**: ESP32 flash storage limits for embedded files
- **Build Size**: Optimize Svelte bundle size
- **Compatibility**: Ensure modern JS features work on target browsers
- **WebSocket**: Single persistent connection for real-time status and commands; ESP32 AsyncWebSocket handles concurrent clients
- **Offline Capability**: Local storage for pending configuration changes

## Risk Mitigation

- Implement graceful degradation for older browsers
- Ensure configuration validation on both client and server
- Backup/restore functionality for configurations
