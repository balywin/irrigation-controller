<script>
  import { onMount } from 'svelte';
  import { getConfig, saveConfig } from '../lib/api.js';
  import { ws, sendCommand } from '../lib/ws.svelte.js';

  const DELAY_PRESETS = ['0m', '1m', '5m', '10m', '20m', '30m', '1h', '2h'];
  const DURATION_PRESETS = [5, 10, 15, 20, 30, 45, 60, 90];
  const AREA_COLORS = ['#16a34a', '#2563eb', '#d97706', '#7c3aed', '#dc2626'];

  let { appConfig, manualZones = $bindable({}) } = $props();

  let areaConfigs = $state({});
  let loadError = $state('');

  let areas = $derived(appConfig?.areas ?? []);
  let fillingCfg = $derived(appConfig?.filling ?? {});
  let fillingEnabled = $derived(fillingCfg.enabled !== false);
  let fillingRunning = $derived(ws.status?.filling?.running ?? false);

  // ac.zones is an array of groups; each group is an array of zone IDs that
  // fire simultaneously. Cycle iterates through the outer array. Backwards
  // compat: a flat array of IDs is migrated to one-element groups.
  function normalizeGroups(zones) {
    if (!Array.isArray(zones)) return [];
    if (zones.length === 0) return [];
    if (Array.isArray(zones[0])) return zones.map(g => g.map(Number));
    return zones.map(z => [Number(z)]);
  }

  let currentGroupIdxs = $state({});

  function navGroup(areaId, delta) {
    const ac = areaConfigs[areaId];
    const cur = currentGroupIdxs[areaId] ?? 0;
    currentGroupIdxs[areaId] = Math.max(0, Math.min(ac.zones.length - 1, cur + delta));
  }

  function addGroup(areaId) {
    const ac = areaConfigs[areaId];
    const arr = [...ac.zones, []];
    setGroups(areaId, arr);
    currentGroupIdxs[areaId] = arr.length - 1;
  }

  function removeGroup(areaId) {
    const ac = areaConfigs[areaId];
    if (ac.zones.length === 0) return;
    const cur = currentGroupIdxs[areaId] ?? 0;
    const arr = ac.zones.filter((_, i) => i !== cur);
    setGroups(areaId, arr);
    currentGroupIdxs[areaId] = Math.max(0, Math.min(cur, arr.length - 1));
  }

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
  }

  let dragSrc = { areaId: null, idx: null };

  function onChipDragStart(areaId, idx, e) {
    dragSrc = { areaId, idx };
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', String(idx));
  }
  function onChipDragOver(areaId, e) {
    if (dragSrc.areaId !== areaId) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
  }
  function onChipDrop(areaId, targetIdx, e) {
    e.preventDefault();
    const src = dragSrc;
    dragSrc = { areaId: null, idx: null };
    if (src.areaId !== areaId || src.idx == null || src.idx === targetIdx) return;
    const ac = areaConfigs[areaId];
    const arr = [...ac.zones];
    const [moved] = arr.splice(src.idx, 1);
    arr.splice(targetIdx, 0, moved);
    setGroups(areaId, arr);
    const cur = currentGroupIdxs[areaId] ?? 0;
    if (cur === src.idx) currentGroupIdxs[areaId] = targetIdx;
    else if (src.idx < cur && targetIdx >= cur) currentGroupIdxs[areaId] = cur - 1;
    else if (src.idx > cur && targetIdx <= cur) currentGroupIdxs[areaId] = cur + 1;
  }
  function onChipDragEnd() {
    dragSrc = { areaId: null, idx: null };
  }

  onMount(async () => {
    try {
      const raw = await getConfig('manual_control.json');
      for (const area of areas) {
        const ac = raw[area.id] ?? {};
        const groups = normalizeGroups(ac.zones);
        areaConfigs[area.id] = {
          zones: groups,
          shuffle: ac.shuffle ?? false,
          durationMinutes: ac.durationMinutes ?? 20,
          delayedStart: ac.delayedStart ?? '0m',
          presets: Array.isArray(ac.presets) ? ac.presets : [],
        };
        manualZones[area.id] = groups.map(g => g.join(''));
        currentGroupIdxs[area.id] = 0;
      }
    } catch (e) {
      loadError = e.message;
    }
  });

  const STOP_GRACE_MS = 5000;
  let stopTimers = {};

  function setGroups(areaId, groups) {
    const ac = areaConfigs[areaId];
    ac.zones = groups;
    manualZones[areaId] = groups.map(g => g.join(''));

    const running = ws.status?.areas?.[areaId]?.running ?? false;
    if (running && groups.length === 0) {
      if (!stopTimers[areaId]) {
        stopTimers[areaId] = setTimeout(() => {
          stopTimers[areaId] = null;
          if ((areaConfigs[areaId]?.zones?.length ?? 0) === 0) sendCommand('stop', areaId);
        }, STOP_GRACE_MS);
      }
    } else if (stopTimers[areaId]) {
      clearTimeout(stopTimers[areaId]);
      stopTimers[areaId] = null;
    }
  }

  // Drop empty groups; preserve currentGroupIdxs to point at same group when possible.
  function pruneEmptyGroups(areaId) {
    const ac = areaConfigs[areaId];
    if (!ac) return;
    const cleaned = ac.zones.filter(g => g.length > 0);
    if (cleaned.length === ac.zones.length) return;
    const cur = currentGroupIdxs[areaId] ?? 0;
    const wasGroup = ac.zones[cur];
    const newCur = wasGroup && wasGroup.length > 0
      ? cleaned.indexOf(wasGroup)
      : Math.max(0, Math.min(cur, cleaned.length - 1));
    setGroups(areaId, cleaned);
    currentGroupIdxs[areaId] = newCur >= 0 ? newCur : 0;
  }

  // Transient pending state per target while waiting for the backend to
  // confirm the manual start/stop via WS status. 'start' shows "Starting…",
  // 'stop' shows "Stopping…"; both render the button disabled. To avoid
  // confirming on a stale heartbeat that was already in-flight at click
  // time, we ignore any status received before the WS clock has advanced
  // past the click moment (see `clickedAt`). A watchdog clears the pending
  // flag if no fresh status arrives within the timeout.
  let pendingAction = $state({});
  let pendingTimers = {};
  let clickedAt = {};
  const PENDING_TIMEOUT_MS = 5000;

  function setPending(target, action) {
    pendingAction[target] = action;
    clickedAt[target] = Date.now();
    if (pendingTimers[target]) clearTimeout(pendingTimers[target]);
    pendingTimers[target] = setTimeout(() => {
      pendingAction[target] = null;
      pendingTimers[target] = null;
    }, PENDING_TIMEOUT_MS);
  }
  function clearPending(target) {
    pendingAction[target] = null;
    if (pendingTimers[target]) { clearTimeout(pendingTimers[target]); pendingTimers[target] = null; }
  }

  // Watch WS for confirmation. Only consider status messages received
  // strictly AFTER the click moment — skips the stale heartbeat that may
  // arrive in the same TCP frame and reflects the pre-command state.
  $effect(() => {
    const lastMsgAt = ws.lastMessageAt;
    for (const area of areas) {
      const p = pendingAction[area.id];
      if (!p) continue;
      if (lastMsgAt <= (clickedAt[area.id] ?? 0)) continue;
      const m = ws.status?.areas?.[area.id]?.manuallyStarted ?? false;
      if (p === 'start' && m) clearPending(area.id);
      if (p === 'stop' && !m) clearPending(area.id);
    }
    const fp = pendingAction['filling'];
    if (fp && lastMsgAt > (clickedAt['filling'] ?? 0)) {
      const fm = ws.status?.filling?.manuallyStarted ?? false;
      if (fp === 'start' && fm) clearPending('filling');
      if (fp === 'stop' && !fm) clearPending('filling');
    }
  });

  // Backend auto-disarms schedule.enabled when a manual run starts (and
  // restores on stop / pause / natural end). On start, we pass the user's
  // current per-zone-group duration and the group count so the firmware
  // computes the actual run length (durationMinutes * zoneGroupCount) and
  // the per-group switch interval (durationMinutes).
  function toggleArea(areaId) {
    const manual = ws.status?.areas?.[areaId]?.manuallyStarted;
    if (pendingAction[areaId]) return;
    if (!manual) pruneEmptyGroups(areaId);
    setPending(areaId, manual ? 'stop' : 'start');
    if (manual) {
      sendCommand('stop', areaId);
    } else {
      const ac = areaConfigs[areaId];
      sendCommand('start', areaId, {
        durationMinutes: Number(ac?.durationMinutes) || 0,
        zoneGroupCount: ac?.zones?.length || 0,
      });
    }
  }

  function shuffle(arr) {
    for (let i = arr.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [arr[i], arr[j]] = [arr[j], arr[i]];
    }
    return arr;
  }

  function groupKey(g) { return [...g].map(Number).sort((a, b) => a - b).join(','); }

  // While running: pull active group out, shuffle the rest, reinsert active
  // at a different index so its position in the cycle changes but it stays
  // active. Otherwise: full shuffle.
  function shuffleNow(areaId) {
    pruneEmptyGroups(areaId);
    const ac = areaConfigs[areaId];
    if (ac.zones.length <= 1) return;

    const running = ws.status?.areas?.[areaId]?.running ?? false;
    const activeZones = ws.status?.areas?.[areaId]?.activeZones ?? [];
    const arr = ac.zones.map(g => [...g]);

    let activeIdx = -1;
    if (running) {
      if (activeZones.length > 0) {
        const activeKey = groupKey(activeZones);
        activeIdx = arr.findIndex(g => groupKey(g) === activeKey);
      }
      if (activeIdx < 0) activeIdx = currentGroupIdxs[areaId] ?? 0;
      if (activeIdx >= arr.length) activeIdx = -1;
    }

    if (activeIdx >= 0) {
      const [active] = arr.splice(activeIdx, 1);
      shuffle(arr);
      const slots = [];
      for (let i = 0; i <= arr.length; i++) if (i !== activeIdx) slots.push(i);
      const newIdx = slots.length > 0
        ? slots[Math.floor(Math.random() * slots.length)]
        : 0;
      arr.splice(newIdx, 0, active);
      setGroups(areaId, arr);
      currentGroupIdxs[areaId] = newIdx;
      return;
    }

    setGroups(areaId, shuffle(arr));
    currentGroupIdxs[areaId] = 0;
  }

  // Auto-shuffle when an area transitions running -> stopped, if its
  // 'Zone Shuffle on Stop' toggle is enabled.
  let prevRunning = {};
  $effect(() => {
    for (const area of areas) {
      const r = ws.status?.areas?.[area.id]?.running ?? false;
      const prev = prevRunning[area.id] ?? false;
      if (prev && !r) {
        const ac = areaConfigs[area.id];
        if (ac?.shuffle && ac.zones.length > 1) {
          setGroups(area.id, shuffle(ac.zones.map(g => [...g])));
          currentGroupIdxs[area.id] = 0;
        }
      }
      prevRunning[area.id] = r;
    }
  });

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

  let presetError = $state('');

  async function persistPresets() {
    presetError = '';
    try { await saveConfig('manual_control.json', areaConfigs); }
    catch (e) { presetError = `Save failed: ${e.message}`; }
  }

  function snapshotCurrent(ac, name) {
    return {
      name,
      zones: ac.zones.map(g => [...g]),
      shuffle: ac.shuffle,
      durationMinutes: ac.durationMinutes,
      delayedStart: ac.delayedStart,
    };
  }

  function loadPreset(areaId, p) {
    const ac = areaConfigs[areaId];
    ac.zones = normalizeGroups(p.zones ?? []);
    ac.shuffle = !!p.shuffle;
    ac.durationMinutes = p.durationMinutes;
    ac.delayedStart = p.delayedStart;
    manualZones[areaId] = ac.zones.map(g => g.join(''));
    currentGroupIdxs[areaId] = 0;
  }

  async function savePresetAs(areaId) {
    const ac = areaConfigs[areaId];
    const raw = prompt('Preset name:');
    if (raw == null) return;
    const name = raw.trim();
    if (!name) return;
    const idx = ac.presets.findIndex(p => p.name === name);
    if (idx >= 0) {
      if (!confirm(`Overwrite existing preset "${name}"?`)) return;
      ac.presets[idx] = snapshotCurrent(ac, name);
    } else {
      ac.presets = [...ac.presets, snapshotCurrent(ac, name)];
    }
    await persistPresets();
  }

  async function updatePreset(areaId, name) {
    const ac = areaConfigs[areaId];
    const idx = ac.presets.findIndex(p => p.name === name);
    if (idx < 0) return;
    if (!confirm(`Overwrite preset "${name}" with current values?`)) return;
    ac.presets[idx] = snapshotCurrent(ac, name);
    await persistPresets();
  }

  async function renamePreset(areaId, oldName) {
    const ac = areaConfigs[areaId];
    const raw = prompt('Rename preset:', oldName);
    if (raw == null) return;
    const name = raw.trim();
    if (!name || name === oldName) return;
    if (ac.presets.some(p => p.name === name)) { alert(`Preset "${name}" already exists`); return; }
    const idx = ac.presets.findIndex(p => p.name === oldName);
    if (idx < 0) return;
    ac.presets[idx] = { ...ac.presets[idx], name };
    await persistPresets();
  }

  async function deletePreset(areaId, name) {
    if (!confirm(`Delete preset "${name}"?`)) return;
    areaConfigs[areaId].presets = areaConfigs[areaId].presets.filter(p => p.name !== name);
    await persistPresets();
  }
</script>

{#if loadError}
  <div class="load-error">Failed to load manual_control.json: {loadError}</div>
{:else}
  <!-- Filling (compact, top) -->
  {@const fillingManual = ws.status?.filling?.manuallyStarted ?? false}
  {@const fillingScheduleActive = ws.status?.filling?.scheduleActive ?? false}
  {@const canStartFilling = fillingEnabled && !fillingScheduleActive}
  {@const fillingPaused = ws.status?.filling?.pausedUntil != null}
  {@const fillingDisabledReason = !fillingEnabled ? 'Filling disabled in settings' : fillingScheduleActive ? 'Running by schedule' : fillingPaused ? 'Paused by schedule' : ''}
  {@const fillingPending = pendingAction['filling']}
  {@const fillingBtnLabel = fillingPending === 'start' ? 'Starting…' : fillingPending === 'stop' ? 'Stopping…' : (fillingManual ? 'Stop' : 'Start')}
  {@const fillingBtnBg = fillingManual ? '#ef4444' : '#fb923c'}
  {@const wsDown = ws.state !== 'connected'}
  {@const fillingBtnDisabled = wsDown || !!fillingPending || (fillingManual ? false : (!canStartFilling || fillingPaused))}
  <div class="card" class:content-disabled={!fillingEnabled} style="--area-color:#fb923c;border-color:var(--area-color);border-width:2px;padding:0.5rem 1rem;">
    <div style="display:flex;align-items:center;gap:1rem;flex-wrap:wrap;">
      <h3 style="margin:0;">Filling</h3>
      <button type="button" class="btn"
        style={`background:${fillingBtnBg};color:#fff;min-width:6rem;`}
        disabled={fillingBtnDisabled}
        title={fillingManual ? '' : fillingDisabledReason}
        onclick={toggleFilling}
      >{fillingBtnLabel}</button>
      <span style="margin-left:auto;display:flex;gap:1rem;align-items:center;">
        <span><span class="status-label" style="margin-right:0.3rem;">Pump {fillingCfg.pump_id ?? '?'}</span><span class="status-value" class:status-on={fillingRunning} class:status-off={!fillingRunning}>{fillingRunning ? 'ON' : 'OFF'}</span></span>
        <span><span class="status-label" style="margin-right:0.3rem;">Water</span><span class="status-value">{ws.status?.sensors?.waterLevel != null ? `${ws.status.sensors.waterLevel}%` : '—'}</span></span>
        {#if fillingRunning && ws.status?.filling?.remaining}
          <span class="status-value status-on">{ws.status.filling.remaining}</span>
        {/if}
      </span>
    </div>
  </div>

  {#each areas as area, i}
    {@const ac = areaConfigs[area.id]}
    {@const running = ws.status?.areas?.[area.id]?.running ?? false}
    {@const manuallyStarted = ws.status?.areas?.[area.id]?.manuallyStarted ?? false}
    {@const scheduleActive = ws.status?.areas?.[area.id]?.scheduleActive ?? false}
    {@const enabled = area.enabled ?? true}
    {@const noZones = ac.zones.length === 0}
    {@const noDuration = !ac.durationMinutes || ac.durationMinutes <= 0}
    {@const canStart = enabled && !scheduleActive && !noZones && !noDuration}
    {@const canStop = enabled && manuallyStarted}
    {@const isPaused = ws.status?.areas?.[area.id]?.pausedUntil != null}
    {@const startDisabledReason = !enabled ? 'Area disabled in settings' : scheduleActive ? 'Running by schedule' : isPaused ? 'Paused by schedule' : noZones ? 'No zones selected' : noDuration ? 'Duration must be > 0' : ''}
    {@const pending = pendingAction[area.id]}
    {@const btnLabel = pending === 'start' ? 'Starting…' : pending === 'stop' ? 'Stopping…' : (manuallyStarted ? 'Stop' : 'Start')}
    {@const btnBg = manuallyStarted ? '#ef4444' : AREA_COLORS[i % AREA_COLORS.length]}
    {@const btnDisabled = wsDown || !!pending || (manuallyStarted ? !canStop : (!canStart || isPaused))}
    {#if ac}
      {@const curIdx = currentGroupIdxs[area.id] ?? 0}
      {@const curGroup = ac.zones[curIdx] ?? []}
      <div class="card" class:content-disabled={!enabled} style={`--area-color:${AREA_COLORS[i % AREA_COLORS.length]};border-color:var(--area-color);border-width:2px;`}>
        <div class="card-header" style="justify-content:flex-start;gap:0.5rem;flex-wrap:wrap;">
          <h3>{area.id}</h3>
          <button type="button" class="btn"
            style={`background:${btnBg};color:#fff;min-width:6rem;`}
            disabled={btnDisabled}
            title={manuallyStarted ? '' : startDisabledReason}
            onclick={() => toggleArea(area.id)}
          >{btnLabel}</button>
          <span style="margin-left:auto;display:inline-flex;flex-wrap:wrap;gap:0.3rem;align-items:center;justify-content:flex-end;">
            {#each ac.presets as p}
              <span style="display:inline-flex;align-items:center;border:1px solid #d1d5db;border-radius:5px;overflow:hidden;">
                <button type="button" class="btn btn-sm btn-secondary" style="border-radius:0;" onclick={() => loadPreset(area.id, p)} title="Load preset">{p.name}</button>
                <button type="button" class="btn btn-sm btn-secondary" style="border-radius:0;border-left:1px solid #4b5563;padding:0.2rem 0.4rem;" onclick={() => updatePreset(area.id, p.name)} title="Overwrite with current values">↻</button>
                <button type="button" class="btn btn-sm btn-secondary" style="border-radius:0;border-left:1px solid #4b5563;padding:0.2rem 0.4rem;" onclick={() => renamePreset(area.id, p.name)} title="Rename preset">✎</button>
                <button type="button" class="btn btn-sm btn-danger" style="border-radius:0;padding:0.2rem 0.4rem;" onclick={() => deletePreset(area.id, p.name)} title="Delete preset">✕</button>
              </span>
            {/each}
            <button type="button" class="btn btn-sm btn-primary" onclick={() => savePresetAs(area.id)} title="Save current values as new preset">+ Save as preset</button>
          </span>
        </div>

        <div class="field" style="flex-wrap:wrap;gap:0.5rem;align-items:center;justify-content:flex-start;">
          <span>Zones in group</span>
          <div style="display:flex;flex-wrap:wrap;gap:0.3rem;">
            {#each area.zone_ids as z, zi}
              <button type="button" class="btn btn-sm"
                class:btn-primary={curGroup.includes(z)}
                class:btn-secondary={!curGroup.includes(z)}
                disabled={ac.zones.length === 0}
                onclick={() => toggleZone(area.id, z)}
                title={`Toggle zone ${z}`}><b>{z}</b> {area.zone_names[zi] ?? ''}</button>
            {/each}
          </div>
          <span style="margin-left:auto;display:inline-flex;align-items:center;gap:0.5rem;">
            <span style="display:inline-flex;align-items:center;gap:0.3rem;font-size:0.8rem;">
              <span>Zone Shuffle on Stop</span>
              <label class="toggle">
                <input type="checkbox" bind:checked={ac.shuffle} />
                <span class="toggle-slider"></span>
              </label>
            </span>
            <button type="button" class="btn btn-sm btn-primary"
              onclick={() => shuffleNow(area.id)}
              title="Randomize group sequence order">Shuffle now</button>
          </span>
        </div>

        <div class="field" style="flex-wrap:wrap;gap:0.3rem;align-items:center;">
          <span>Sequence</span>
          <div style="display:flex;flex-wrap:wrap;gap:0.3rem;align-items:center;flex:1;">
            <span style="display:inline-flex;gap:0.2rem;margin-right:1rem;">
              <button type="button" class="btn btn-sm btn-danger"
                onclick={() => removeGroup(area.id)}
                disabled={ac.zones.length === 0} title="Remove current group">✕</button>
              <button type="button" class="btn btn-sm btn-primary"
                onclick={() => addGroup(area.id)} title="Add new group">+</button>
            </span>
            {#each ac.zones as g, gi}
              <button type="button" class="btn btn-sm"
                class:btn-primary={gi === curIdx}
                class:btn-secondary={gi !== curIdx}
                style="font-family:monospace;min-width:2rem;cursor:grab;"
                draggable="true"
                ondragstart={(e) => onChipDragStart(area.id, gi, e)}
                ondragover={(e) => onChipDragOver(area.id, e)}
                ondrop={(e) => onChipDrop(area.id, gi, e)}
                ondragend={onChipDragEnd}
                onclick={() => currentGroupIdxs[area.id] = gi}
                title={`Group ${gi + 1} — drag to reorder`}>{g.join('') || '·'}</button>
            {/each}
          </div>
        </div>

        <div class="field" style="flex-wrap:wrap;gap:0.3rem;">
          <span>Duration per zone group (min)</span>
          <div class="preset-row" style="margin-top:0;">
            {#each DURATION_PRESETS as d}
              <button type="button" class="btn btn-sm"
                class:btn-primary={ac.durationMinutes === d}
                class:btn-secondary={ac.durationMinutes !== d}
                onclick={() => ac.durationMinutes = d}>{d}</button>
            {/each}
            <input type="number" min="1" style="width:60px;" bind:value={ac.durationMinutes} />
          </div>
        </div>

        <div class="field" style="flex-wrap:wrap;gap:0.3rem;">
          <span>Delayed Start</span>
          <div class="preset-row" style="margin-top:0;">
            {#each DELAY_PRESETS as p}
              <button type="button" class="btn btn-sm"
                class:btn-primary={ac.delayedStart === p}
                class:btn-secondary={ac.delayedStart !== p}
                onclick={() => ac.delayedStart = p}>{p}</button>
            {/each}
            <input type="text" style="width:60px;" bind:value={ac.delayedStart} placeholder="e.g. 3h" />
          </div>
        </div>
      </div>
    {/if}
  {/each}

  {#if presetError}
    <div class="status-err" style="padding:0.5rem;">{presetError}</div>
  {/if}
{/if}
