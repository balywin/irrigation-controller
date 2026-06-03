<script>
  import './assets/app.css';
  import wsIcon from './assets/ws-icon.png';
  import { onMount } from 'svelte';
  import { getConfig } from './lib/api.js';
  import { ws, sendCommand, initWs } from './lib/ws.svelte.js';
  import ManualTab from './tabs/ManualTab.svelte';
  import AreaTab from './tabs/AreaTab.svelte';
  import FillScheduleTab from './tabs/FillScheduleTab.svelte';
  import SettingsTab from './tabs/SettingsTab.svelte';
  import FirmwareTab from './tabs/FirmwareTab.svelte';

  const AREA_COLORS = ['#16a34a', '#2563eb', '#d97706', '#7c3aed', '#dc2626'];
  const WS_CLASSES = { connected: 'ws-connected', reconnecting: 'ws-reconnecting', disconnected: 'ws-disconnected' };

  let appConfig = $state(null);
  let configError = $state('');
  let activeTab = $state(null);

  let areas = $derived(appConfig?.areas ?? []);
  let waterLevel = $derived(ws.status?.sensors?.waterLevel ?? null);

  let manualZones = $state({});

  // Pending action per target ('stop' | 'pause' | null) — set on click,
  // cleared when WS reports the resulting backend state. To avoid acting
  // on a stale heartbeat already in-flight at click time, the effect only
  // considers WS messages received strictly after the click moment.
  let ePending = $state({});
  let ePendingTimers = {};
  let eClickedAt = {};
  const E_PENDING_TIMEOUT_MS = 5000;

  function setEPending(target, action) {
    ePending[target] = action;
    eClickedAt[target] = Date.now();
    if (ePendingTimers[target]) clearTimeout(ePendingTimers[target]);
    ePendingTimers[target] = setTimeout(() => {
      ePending[target] = null;
      ePendingTimers[target] = null;
    }, E_PENDING_TIMEOUT_MS);
  }
  function clearEPending(target) {
    ePending[target] = null;
    if (ePendingTimers[target]) { clearTimeout(ePendingTimers[target]); ePendingTimers[target] = null; }
  }

  $effect(() => {
    const lastMsgAt = ws.lastMessageAt;
    const check = (target, st) => {
      const p = ePending[target];
      if (!p) return;
      if (lastMsgAt <= (eClickedAt[target] ?? 0)) return;
      if (p === 'stop' && st === 'inactive') clearEPending(target);
      else if (p === 'pause' && st === 'paused') clearEPending(target);
    };
    for (const a of areas) check(a.id, eStateOf(a.id));
    check('filling', eStateOf('filling'));
  });

  onMount(async () => {
    initWs();
    try {
      appConfig = await getConfig('app_config.json');
      activeTab = 'manual';
    } catch (e) {
      configError = e.message;
    }
  });

  function areaColor(i) { return AREA_COLORS[i % AREA_COLORS.length]; }

  function targetObj(target) {
    return target === 'filling' ? ws.status?.filling : ws.status?.areas?.[target];
  }

  function isManualSource(target) {
    return targetObj(target)?.manuallyStarted ?? false;
  }

  function eStateOf(target) {
    const o = targetObj(target);
    if (!o) return 'inactive';
    if (o.pausedUntil) return 'paused';
    if (o.running) return 'active';
    return 'inactive';
  }

  function pausedSecondsLeft(target) {
    const v = targetObj(target)?.pausedUntil;
    if (!v) return 0;
    const m = String(v).match(/PT(\d+)S/);
    return m ? parseInt(m[1], 10) : 0;
  }
  function pausedMinutesLeft(target) {
    const s = pausedSecondsLeft(target);
    return s > 0 ? Math.max(1, Math.ceil(s / 60)) : 0;
  }

  // "X min" when ≥ 60s remain, otherwise "X sec". Empty when 0/null.
  function fmtRemaining(seconds) {
    if (!seconds || seconds <= 0) return '';
    if (seconds >= 60) return `${Math.ceil(seconds / 60)} min`;
    return `${seconds} sec`;
  }
  function totalRemainingLabel(target) {
    const o = targetObj(target);
    const st = eStateOf(target);
    if (st === 'paused') return fmtRemaining(pausedSecondsLeft(target));
    if (st === 'active') return fmtRemaining(o?.remainingSeconds ?? 0);
    return '';
  }
  function groupRemainingLabel(target) {
    if (target === 'filling') return '';
    if (eStateOf(target) !== 'active') return '';
    return fmtRemaining(targetObj(target)?.groupRemainingSeconds ?? 0);
  }

  function eClick(target) {
    if (ePending[target]) return;
    const st = eStateOf(target);
    const commandTarget = target;
    if (st === 'active') {
      if (isManualSource(target)) {
        setEPending(target, 'stop');
        sendCommand('stop', commandTarget);
      } else {
        setEPending(target, 'pause');
        sendCommand('pause_1h', commandTarget);
      }
    } else if (st === 'paused') {
      setEPending(target, 'stop');
      sendCommand('stop', commandTarget);
    }
  }

  function eClass(target) {
    const st = eStateOf(target);
    if (st === 'active') return 'emergency-btn e-active';
    if (st === 'paused') return 'emergency-btn e-paused';
    return 'emergency-btn e-inactive';
  }

  function eStateLabel(target) {
    const st = eStateOf(target);
    if (st === 'active') return 'ON';
    if (st === 'paused') return `Paused for ${pausedMinutesLeft(target)} min.`;
    return 'Off';
  }

  function fillingActive() {
    return ws.status?.filling?.running ?? false;
  }

  function eActionLabel(target) {
    const p = ePending[target];
    if (p === 'stop') return 'Stopping…';
    if (p === 'pause') return 'Pausing…';
    const st = eStateOf(target);
    if (st === 'active') return isManualSource(target) ? 'STOP' : 'Pause 1h';
    if (st === 'paused') return 'STOP';
    return '';
  }

  function eDisabled(target) {
    if (ws.state !== 'connected') return true;
    if (ePending[target]) return true;
    return eStateOf(target) === 'inactive';
  }

  function activeZones(areaId) {
    const wsZones = ws.status?.areas?.[areaId]?.activeZones;
    if (wsZones?.length) return wsZones;
    return manualZones[areaId] ?? [];
  }

  function waterLevelColor(level) {
    if (level == null) return '#9ca3af';
    if (level >= 60) return '#16a34a';
    if (level >= 30) return '#f59e0b';
    return '#dc2626';
  }

  function fmtTime(ms) {
    if (!ms) return '';
    return new Date(ms).toLocaleTimeString();
  }

  function wsTooltip() {
    if (ws.state === 'connected') {
      if (ws.hasEverDisconnected && ws.lastConnectedAt) return `Reconnected at ${fmtTime(ws.lastConnectedAt)}.`;
      return `Connected${ws.lastConnectedAt ? ' since ' + fmtTime(ws.lastConnectedAt) : ''}.`;
    }
    if (ws.lastError) return `Error: ${ws.lastError}`;
    const since = ws.lastMessageAt || ws.lastDisconnectAt;
    return `Disconnected since ${fmtTime(since)}.`;
  }

  function isScheduleActive(id) {
    return (ws.status?.areas?.[id]?.scheduleActive ?? -1) >= 0 || eStateOf(id) === 'paused';
  }

  function manualVis(target) {
    if (target === 'filling') return ws.status?.filling?.manuallyStarted ? 'visible' : 'hidden';
    return ws.status?.areas?.[target]?.manuallyStarted ? 'visible' : 'hidden';
  }
</script>

<div class="app">
  <div class="emergency-bar">
    {#if appConfig?.filling?.enabled !== false}
      <button
        class={eClass('filling')}
        style={eStateOf('filling') !== 'inactive' || ePending['filling'] ? 'background:#fb923c' : ''}
        onclick={() => eClick('filling')}
        disabled={eDisabled('filling')}
        title={`Filling — water level ${waterLevel != null ? waterLevel + '%' : 'n/a'}`}
      >
        <span class="e-title e-title-battery">
          <span class="battery" class:filling={fillingActive()}
            style="--fill-pct:{waterLevel != null ? Math.max(0, Math.min(100, waterLevel)) : 0}%;">
            <span class="battery-cell">
              <span class="battery-fill"></span>
              <span class="battery-rise"></span>
            </span>
            <span class="battery-nub"></span>
          </span>
        </span>
        <span class="e-state">
          <span class="battery-pct">{waterLevel != null ? `${waterLevel}%` : '—'}</span>
          {#if totalRemainingLabel('filling')}
            <span class="e-remaining">{totalRemainingLabel('filling')}</span>
          {/if}
        </span>
        <span class="e-action">{eActionLabel('filling')}</span>
      </button>
    {/if}
    {#each areas as area, i}
      {#if area.enabled !== false}
        {@const allGroups = manualZones[area.id] ?? []}
        {@const wsZones = ws.status?.areas?.[area.id]?.activeZones ?? []}
        {@const wsZonesStr = wsZones.length > 0 ? [...wsZones].sort((a,b)=>a-b).join('') : ''}
        {@const activeGrpIdx = wsZonesStr ? allGroups.findIndex(g => g === wsZonesStr) : -1}
        <button
          class={eClass(area.id)}
          style={eStateOf(area.id) !== 'inactive' || ePending[area.id] ? `background:${areaColor(i)}` : ''}
          onclick={() => eClick(area.id)}
          disabled={eDisabled(area.id)}
          title={area.id}
        >
          <span class="e-title">
            {area.id[0]}
            {#if allGroups.length > 0}
              <span class="e-zones">
                {#each allGroups as grp, gi}
                  {#if gi > 0}{' '}{/if}<span class:e-zone-active={activeGrpIdx >= 0 && gi === activeGrpIdx}>{grp}</span>
                {/each}
              </span>
            {:else if wsZones.length > 0}
              <span class="e-zones"><span class="e-zone-active">{wsZones.join('')}</span></span>
            {/if}
          </span>
          <span class="e-state">
            {#if groupRemainingLabel(area.id)}
              <span class="e-remaining e-remaining-left">{groupRemainingLabel(area.id)}</span>
            {/if}
            <span class="e-state-text">{eStateLabel(area.id)}</span>
            {#if totalRemainingLabel(area.id)}
              <span class="e-remaining">{totalRemainingLabel(area.id)}</span>
            {/if}
          </span>
          <span class="e-action">{eActionLabel(area.id)}</span>
        </button>
      {/if}
    {/each}
  </div>

  <nav>
    <img src={wsIcon} alt="WebSocket {ws.state}" class="ws-icon {WS_CLASSES[ws.state] ?? 'ws-disconnected'}" title={wsTooltip()} />
    <button class:active={activeTab === 'manual'} onclick={() => activeTab = 'manual'}>
      {#if appConfig?.filling?.enabled !== false}
        <span style="display:inline-block;width:8px;height:8px;border-radius:50%;background:#fb923c;margin-right:6px;vertical-align:middle;visibility:{manualVis('filling')};"></span>
      {/if}
      Manual Control
      {#if areas.length > 0}
        <span style="display:inline-grid;grid-template-rows:repeat(2,auto);grid-auto-flow:column;gap:2px;margin-left:6px;vertical-align:middle;">
          {#each areas as area, i}
            {#if area.enabled !== false}
              <span style="display:inline-block;width:8px;height:8px;border-radius:50%;background:{areaColor(i)};visibility:{manualVis(area.id)};"></span>
            {/if}
          {/each}
        </span>
      {/if}
    </button>
    {#each areas as area, i}
      {#if area.enabled !== false}
        <button class:active={activeTab === area.id}
          style={`color:${areaColor(i)};${activeTab === area.id ? `border-bottom-color:${areaColor(i)};` : ''}`}
          onclick={() => activeTab = area.id}>
          {area.id}
          {#if isScheduleActive(area.id)}
            <span class="status-dot" style="background:{areaColor(i)}"></span>
          {/if}
        </button>
      {/if}
    {/each}
    {#if appConfig?.filling?.enabled !== false}
      <button class:active={activeTab === 'fill_schedule'}
        style={`color:#fb923c;${activeTab === 'fill_schedule' ? 'border-bottom-color:#fb923c;' : ''}`}
        onclick={() => activeTab = 'fill_schedule'}>
        Fill Sched
        {#if ws.status?.filling?.scheduleActive === true}
          <span class="status-dot" style="background:#fb923c"></span>
        {/if}
      </button>
    {/if}
    <button class:active={activeTab === 'settings'} onclick={() => activeTab = 'settings'}>Settings</button>
    <button class:active={activeTab === 'firmware'} onclick={() => activeTab = 'firmware'}>Firmware</button>
  </nav>

  {#if configError}
    <div class="load-error">Failed to load app config: {configError}</div>
  {:else if !appConfig}
    <p style="padding:1rem;color:#666;">Loading…</p>
  {:else if activeTab === 'manual'}
    <ManualTab {appConfig} bind:manualZones />
  {:else if activeTab === 'fill_schedule'}
    <FillScheduleTab />
  {:else if activeTab === 'settings'}
    <SettingsTab bind:config={appConfig} />
  {:else if activeTab === 'firmware'}
    <FirmwareTab />
  {:else}
    {#each areas as area, i}
      {#if activeTab === area.id}
        <AreaTab {area} color={areaColor(i)} />
      {/if}
    {/each}
  {/if}
</div>
