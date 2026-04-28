# Irrigation controller

This repository contains the following software parts of ESP32-based irrigation controller:
## 0. Hardware
The hardware consists of:
 - a DIN-rail 10A fuse
 - 24V DC power supply on a DIN rail
 - ESP32 microcontroller board Kincony KC868-A16 rev.1.6 ([schematic](https://www.kincony.com/download/KC868-A16-schematic.pdf), [details](https://www.kincony.com/esp32-board-16-channel-relay-hardware.html)), powered by 24V DC with:
   - 16 optically isolated inputs for connecting physical buttons and sensors (e.g. hall effect water level switches, rain sensor)
   - 16 transistor outputs for controlling pumps and valves (e.g. 3 pumps, 2 main motorized valves, 5 electromagnetic zone valves in the current configuration)
   - 2-relay module for controlling 220V pumps (one for filling the water tank and one for grass area irrigation)
   - Physical buttons for manual control 
   - SSD1306 or SH1106 OLED screen for status display and user interaction (e.g. navigating through schedules, manual control, settings) - connected via I2C bus
   - DS1321 real-time clock module (with LIR2032 rechargeable coin battery) for accurate timekeeping during power outages, which is important for schedule execution - connected to I2C bus
   - all X01-X16 inputs and Y01-Y16 outputs are based on PCF8574 I/O expanders, which are connected to the ESP32 via I2C bus
   - AJ-SR04M ultrasonic distance sensor ([descriptions](https://manuals.plus/arduino/aj-sr04m-distance-measuring-transducer-sensor-manual)) for water level measurement in the tank, connected via a buffer to 433M_R (GPIO2) and 433M_T (GPIO15) pins to use it in UART mode, the sensor is powered by 5V from the power supply, and the buffer converts 5V signals to 3.3V for the ESP32. The sensor is put in 433M receive header P2, but RX pin is bent and connected to header P7 pin 2 or pin 3
 - Few hall effect water level switches in the tank, two on the top level for redundancy
 - 1 220V pump in the well for filling the tank
 - 1 pump for grass area irrigation with a motorized shut-off valve to prevent leaking when inactive
 - 4-5 electromagnetic zone valves for grass area irrigation, controlled by the grass pump
 - Planned: 
   - Various sensors for monitoring and to suspend irrigation (e.g. temperature, humidity, rain)
   - 1 pump for drip area irrigation with a motorized shut-off valve to prevent leaking when inactive

Current controller connections:
  - Outputs Ynn - all 24V DC:
    - Y01 - Filling pump relay control
    - Y02 - Grass   pump relay control
    - Y03 - Planned: Drip pump relay control
    - Y04 - Planned: Drip zone 1 EM-valve control
    - Y05 - Planned: Drip zone 2 EM-valve control
    - Y06 - Planned: Drip zone 3 EM-valve control
    - Y09 - Grass zone 4 EM-valve control
    - Y10 - Grass zone 6 EM-valve control
    - Y11 - Grass zone 5 EM-valve control
    - Y12 - Grass zone 3 EM-valve control
    - Y13 - Grass zone 1 EM-valve control
    - Y14 - Grass zone 2 EM-valve control
    - Y15 - Drip  area main motorized valve control
    - Y16 - Grass area main motorized valve control

  - Inputs Xnn:
    - X01 - tank upper limit switch 2
    - X02 - tank upper limit switch 1
    - X03 - tank upper mid switch
    - X04 - tank lower mid switch
    - X05 - tank lower limit switch
    - X13 - button 3 (zone switch)
    - X14 - button 4 (drip start/stop)
    - X15 - button 2 (grass start/stop)
    - X16 - button 1 (filling start/stop)
    - HT1, HT2 - SCK, OUT pins of pressure sensor, which is not reliable for water level measurement, will be replaced by another sensor in the future

## 1. Firmware
The firmware is built using PlatformIO and runs on ESP32. It: 
 - allows users to control the irrigation system via:
   - web UI
   - physical buttons connected to GPIOs and small OLED screen
 - can control according to configured schedules or 'manually' 
 - handles OTA updates 
 - stores configuration as JSON files in LittleFS
 - supports default configuration embedded in the firmware binary, which is copied to LittleFS on first boot or when configs are missing or corrupted
 - serves a web interface as a single page app, which is embedded in the firmware binary using PROGMEM.
 - reading various sensors (like water level, pump status, valve status, temperature, humidity, rain) and keeping system status; make them available in the web interface similarly to a real-time monitoring and controlling system.
 - runs various tasks, keeping track of their status, like:
   - schedule execution, involving timers and sensor readings
   - manual control execution, which could involve timers for delayed start and duration-based control

## 2. Configuration
Configuration is stored as JSON files in LittleFS directory `./data/config`. The main config files are:
 - `app_config.json` - as per sample config file `app_config.sample.json` inside `samples` directory. Areas should be an array, zones in each area should be an array of numbers, and zone names should be an array of strings
 - `schedule.json` - contains irrigation schedules for each area. Each schedule's `zones` uses the **same zone-groups format as `manual_control.json`**: an array of groups where each group is an array of zone IDs that fire simultaneously, and the cycle iterates through groups for `durationMinutes` each. Backwards compat: flat array `[3, 4]` is auto-migrated to `[[3], [4]]`. See sample config file `schedule.sample.json` inside `samples` directory.
 - `manual_control.json` - contains manual control state for each area. Per area:
   - `zones` is an **array of zone groups**. Each group is an array of zone IDs that fire **simultaneously**. The cycle iterates through groups in order, holding each group for `durationMinutes`. Single-zone groups behave like the legacy flat list (e.g. `[[3], [4]]` ≡ old `[3, 4]`). Multi-zone groups (e.g. `[[1, 3], [2, 4]]`) energize all member valves at once.
   - Backwards compat: a flat array `[3, 4]` is auto-migrated by the UI to `[[3], [4]]`.
   - `shuffle` (boolean) — when `true`, the group sequence is randomized automatically each time the area transitions running → stopped (UI side; persists only via preset save).
   - `durationMinutes`, `delayedStart`, `presets` as before. Preset `zones` use the same group structure.
   - See sample config file `manual_control.sample.json` in the `samples` directory.

>#### IMPORTANT: do not touch sample files without explicit confirmation as they are used as a specification for the config file structure and will be copied to LittleFS on first boot if actual config files are missing or corrupted.

## Backend API reference
Frontend-facing backend endpoints:

- Static SPA:
  - `GET /`
  - `GET /index.html`
  - `GET /assets/*`
- Config CRUD:
  - `GET/PUT /config/app_config.json`
  - `GET/PUT /config/schedule.json`
  - `GET/PUT /config/manual_control.json`
- Runtime status:
  - `GET /status`
- OTA:
  - `GET /update` - OTA web page
  - `GET /ota/start` - initialize OTA session (mode/hash validation + update init)
  - `POST /ota/upload` - upload firmware/filesystem binary
- Real-time channel:
  - `GET /ws` (WebSocket upgrade)

## 3. Web interface
A WebSocket connection between the backend and the Web interface is used for real-time updates and control: 
  - the Web interface sends commands to the backend
  - keeps track of control states and sensor readings
  - sends and receives configuration data to/from the backend to allow viewing, editing and saving of schedules and all other settings.

States and readings are displayed in the UI in real-time, allowing users to monitor the system status and control it effectively.

**IMPORTANT:** WebSocket commands (to the backend) and events (from the backend) are defined in `websocket_protocol.md` file.

The Web interface supports: 
 - monitoring system status (e.g. water level, pumps status, valves status, active schedule), viewing active schedules and manual control status
 - emergency stop buttons above the tabs for filling and irrigation for each area - when process (started by schedule or manually) is active
 - **Emergency button layout** (3-column grid, fixed slots — sizes/positions stay constant across all states):
   - **Left**: single-letter target title — `F` (Filling), `G` (Grass), `D` (Drip). The full name is in the button's tooltip. For areas, a small inline list of currently energized zone numbers (from `ws.status.areas[id].activeZones`) follows the letter.
   - **Center**: current state plus countdown timers.
     - For areas: optional **per-group countdown** (`groupRemainingSeconds` from WS) on the left, then the state text (`ON` / `Off` / `Paused for <n> min.`), then the **total state countdown** on the right (`remainingSeconds` while running, `pausedUntil` while paused). Both countdowns format as `<n> min` while ≥ 60 s remain and switch to `<n> sec` for the final minute.
     - For Filling: an Android-style **battery indicator** plus a numeric percentage and the total countdown to the right. The static fill width equals the current water level (color-coded green/yellow/red). When filling is active, a translucent "rising" overlay grows repeatedly from the current level toward 100% then fades — visually mimicking the empty portion filling up (charging-style). When inactive, the fill is static.
   - **Right**: action label — exactly the verb that the next click will execute: `STOP`, `Pause 1h`, `Stopping…`, or `Pausing…`. Empty when the button is inactive (nothing to do). Right-edge slot has a fixed `min-width` so the label position never shifts.
 - **Active zone-group highlight** (in the Manual Control area card's Sequence chips and the matching schedule cards): the chip whose zones match `ws.status.areas[<area>].activeZones` gets an orange glow with a pulsing outline so the currently firing group is unmistakably visible regardless of which group the editor cursor is on.
 - emergency buttons behave differently depending on the **source** of the active operation:
   - **If activated by schedule**: cycle is `Pause 1h → STOP → inactive`.
     - First click — action label `Pause 1h`, active color (green, blue, orange) — sends `pause_1h` (stops the run and blocks any `start` for 1 hour). State flips to `Paused for <n> min.` once WS confirms.
     - Second click — action label `STOP`, semi-active color — sends `stop` to clear the 1h pause early. Inactive afterward.
   - **If activated manually** (via the Manual Control tab): single click — action label `STOP`, active color — sends `stop` directly. **Pause 1h is never used for manual runs** — it only makes sense for schedule-driven runs.
   - **Inactive** (nothing running): button is gray, non-interactive.
 - **Transient emergency-button states** (`Stopping…` / `Pausing…`): on click, the action label flips to `Stopping…` or `Pausing…` and the button is disabled until WS confirms the corresponding backend transition (running=false, or pausedUntil set). Same stale-heartbeat guard as the manual Start/Stop button: only WS messages received strictly after the click count. Re-clicks are ignored while pending. 5 s watchdog clears the pending flag if no fresh status arrives.
 - manual control of irrigation areas and zones, according to 'manual_control.json' config, allowing zones selection defined in 'areas' config in 'manual_control.json' config. Each area should have a separate tab in the UI
 - manual control of filling the water tank, according to 'manual_control.json' config
 - colorized manual control 'Start/Stop' button must: 
   - be enabled only when the area/filling is not already activated by a schedule and area/filling enable flag is true
   - for areas: 'Start' must be **disabled** when no zone is selected or when duration is `<= 0` (tooltip explains the reason)
   - 'Start' must also be **disabled** when the area/filling is currently paused by a schedule's `pause_1h` (tooltip: "Paused by schedule"). The Manual Control tab does NOT offer a Resume action — pause/resume semantics belong to schedules only.
   - disabled state must be visually distinct (reduced opacity + grayscale filter) so user can tell at a glance the button is not actionable
   - be titled 'Stop' (red color) when the process is manually activated and send command 'stop' to the backend
   - be titled 'Start' (area color, or orange for filling) when the process is not active and if enabled, send command 'start' to the backend
   - **Transient pending states** (`Starting…` / `Stopping…`): the moment the user clicks Start (or Stop), the button switches to `Starting…` (area color, or orange for filling) or `Stopping…` (red), and is **disabled**, until the WS `status` confirms `manuallyStarted` matches the intent. To avoid acting on a stale heartbeat that may already be in-flight at click time, only WS messages received **strictly after the click moment** count toward confirmation (the click timestamp is stored per target and compared to `ws.lastMessageAt`). Re-clicks are ignored while pending. A 5 s watchdog clears the pending flag if no fresh status arrives.
   - **Constant button size**: the button has a fixed `min-width` so its width doesn't jump as the label switches between `Start` / `Stop` / `Starting…` / `Stopping…`.
   - in each card, render the button immediately to the right of the title (not space-between)
 - manual control tab layout:
   - **Filling card is rendered first (at the top)**, with a compact single-row layout: `[Filling title] [Start/Resume/Stop button]  ...  [Pump N status] [Water level]` (status pushed to the right; Pump first, Water level second). The card uses an orange accent border.
   - Per-area cards follow the filling card.
   - **No Save button** on the manual control tab — manual control state is transient and applied via `start`/`stop` commands; selections are not persisted from this tab.
 - **Zone group editor** (used in both Manual Control area cards and Schedule cards): replaces the legacy flat zone list. Two adjacent rows compose the editor:
   1. **Zones in group** — toggle buttons, one per zone defined in `app_config.json` for the area, labeled `<id> <name>`. Clicking adds/removes that zone from the **currently selected group**. In the Manual Control card only, this row is right-aligned with the `Zone Shuffle on Stop` toggle and `Shuffle now` button (Schedule cards do not expose shuffle).
   2. **Sequence** — chips representing each group (showing the joined zone IDs, or `·` for an empty group). Left of the chips: `‹` `›` (previous/next group), `✕` (delete current group), `+` (add new empty group). Chips are draggable to reorder; the editor cursor follows the dragged group's new position.
 - **`Shuffle now` semantics**: randomizes the group sequence. While the area is **running**, the currently-active group is pinned to the same physical run but moved to a new position in the cycle (so it remains active without disruption); detection uses `ws.status.areas[id].activeZones`, falling back to the editor cursor when WS info is unavailable. While **stopped**, performs a full Fisher–Yates shuffle. Empty groups are pruned before shuffle. Both `Start` and `Shuffle now` prune empty groups first.
 - **Auto-shuffle on stop**: when `Zone Shuffle on Stop` is enabled and the area transitions running → stopped (any source: button, schedule end, manual stop), the UI re-shuffles the group sequence. Reorderings are UI-only — they do not persist to `manual_control.json` until the user saves a preset or restarts. Backend keeps using the on-disk sequence for the running cycle.
 - **Schedule auto-disarm during manual run** (firmware-side, disk-persisted): when the firmware receives a `start` WS command for an area it:
   1. Reads the current `schedule.json[<area>].enabled` (the **global area flag**).
   2. Stashes that original value in `/config/disarm.json` (so it survives a reboot mid-run).
   3. Writes **only** `schedule.json[<area>].enabled = false` — all per-schedule card data (zones, durations, per-card `enabled`, etc.) is untouched because the firmware reads the disk doc fresh, flips one field, and writes back.
   4. Sets an in-RAM `armed` flag (future schedule engine consults this flag before firing).
   - On `stop` / `pause_1h`, or when the irrigation flag drops to false (timer expiry, hardware button, etc.), the firmware restores `schedule.json[<area>].enabled` from the stash and clears `/config/disarm.json` for that area.
   - On next boot, `setupWebSocketProtocol()` calls `recoverDisarmOnBoot()` which reads any remaining `/config/disarm.json` entries and restores them. This handles the reboot-mid-run case so schedule.json is never left permanently disabled.
   - The Settings tab **does not** independently touch `schedule.json` when the area enable toggle is changed — that was a UI-side workaround that has been removed. The firmware owns the disarm/restore lifecycle.
 - **Per-area presets**: each area card has a `Presets` row listing named snapshots of the area's parameters (zones, shuffle, durationMinutes, delayedStart). User actions per chip:
   - **Click name**: load the preset's values into the form (overwrites current working values).
   - **↻**: overwrite the preset with the current working values (confirm prompt).
   - **✎**: rename the preset (prompt; rejects duplicate names).
   - **✕**: delete the preset (confirm prompt).
   - **+ Save as preset**: save current working values as a new named preset (prompt for name; if name already exists, confirm overwrite).
   Preset add/edit/rename/delete operations auto-persist `manual_control.json` (no Save button required). Presets are stored in `manual_control.json` under each area's `presets` array (see sample).
 - to avoid user confusion, manual control must be in a separate tab (the first one, default), which should not hide any UI elements depending on state, but show them in disabled state with tooltip explaining why they are disabled (e.g. "disabled because water level is low")
 - WebSocket connection indicator: colored icon (green/orange/red) in nav area indicating WebSocket connection status (connected/warning/disconnected) for real-time updates; tooltip on hover shows last connection time and error messages if any.
 - captive portal mode for initial WiFi setup

It supports loading, editing and saving of:
 - schedule of each area via data structures stored in json files in LittleFS. Each area should have a separate tab in the UI
 - application configuration in a separate tab in the UI, which includes parameters listed in the sample config file `app_config.sample.json` inside `data/config/samples` directory.

Wherever possible, place titles and editing control elements on a single line for better readability and compactness. For example, the title of the schedule cards for <Area> should be "<Area> Schedule 1" and the editing controls (e.g. add/remove schedule, save button) should be on the same line as the title; 'delayed start' to be on the same line as the time selection elements 