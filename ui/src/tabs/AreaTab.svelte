<script>
  import { onMount } from 'svelte';
  import { getConfig, saveConfig } from '../lib/api.js';
  import { ws, sendCommand } from '../lib/ws.svelte.js';
  import ScheduleCard from './ScheduleCard.svelte';

  let { area, color = '#2563eb' } = $props();

  let wsConnected = $derived(ws.state === 'connected');
  let areaRunning = $derived(ws.status?.areas?.[area.id]?.running ?? false);
  let areaPaused = $derived(ws.status?.areas?.[area.id]?.pausedUntil != null);

  let fullScheduleRaw = null;
  let sectionEnabled = $state(true);
  let suspendAboveTempEnabled = $state(true);
  let suspendAboveTemp = $state(35);
  let suspendOnRainEnabled = $state(false);
  let suspendOnRainAbove = $state(2);
  let schedules = $state([]);
  let loadError = $state('');
  let saving = $state(false);
  let saveMsg = $state('');
  let saveError = $state('');

  onMount(async () => {
    try {
      const raw = await getConfig('schedule.json');
      fullScheduleRaw = raw;
      const section = raw[area.id] ?? { enabled: true, schedules: [] };
      sectionEnabled = section.enabled ?? true;
      suspendAboveTempEnabled = section.suspendAboveTempEnabled ?? true;
      suspendAboveTemp = section.suspendAboveTemp ?? 35;
      suspendOnRainEnabled = section.suspendOnRainEnabled ?? false;
      suspendOnRainAbove = section.suspendOnRainAbove ?? 2;
      schedules = (section.schedules ?? []).map(s => ({
        enabled: s.enabled ?? true,
        zones: s.zones ?? [],
        durationMinutes: s.durationMinutes ?? 30,
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

  // Reload the global section enable from disk when the area transitions
  // from manually running to stopped — firmware restores schedule.json at
  // that point and the UI must reflect the restored value.
  let prevManual = false;
  $effect(() => {
    const m = ws.status?.areas?.[area.id]?.manuallyStarted ?? false;
    if (prevManual && !m) {
      getConfig('schedule.json').then(raw => {
        if (!raw) return;
        fullScheduleRaw = { ...(fullScheduleRaw ?? {}), ...raw };
        const section = raw[area.id];
        if (section != null) sectionEnabled = section.enabled ?? true;
      }).catch(() => {});
    }
    prevManual = m;
  });

  function addSchedule() {
    schedules = [...schedules, {
      enabled: true, zones: [], durationMinutes: 30,
      startTimes: ['08:00'], daysOfWeek: [1,2,3,4,5,6,7],
      sunriseSchedule: { enabled: false, sunriseOffsetMinutes: 0 },
      sunsetSchedule: { enabled: false, sunsetOffsetMinutes: 0 },
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
        [area.id]: { enabled: sectionEnabled, suspendAboveTempEnabled, suspendAboveTemp, suspendOnRainEnabled, suspendOnRainAbove, schedules },
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
<div style="--area-color:{color};">
  <div class="field" style="flex-wrap:wrap;gap:0.4rem;">
    <span>{area.id} Schedules enabled</span>
    <label class="toggle">
      <input type="checkbox" bind:checked={sectionEnabled} />
      <span class="toggle-slider"></span>
    </label>
    <span class:field-disabled={!sectionEnabled} style="display:flex;align-items:center;gap:0.4rem;flex-wrap:wrap;">
      <span style="display:inline-flex;align-items:center;gap:0.25rem;">
        Suspend above
        <input type="number" style="width:55px;" bind:value={suspendAboveTemp}
          disabled={!suspendAboveTempEnabled || !sectionEnabled} />
        °C
        <label class="toggle" style="margin-left:0.25rem;">
          <input type="checkbox" bind:checked={suspendAboveTempEnabled} disabled={!sectionEnabled} />
          <span class="toggle-slider"></span>
        </label>
      </span>
      <span style="display:inline-flex;align-items:center;gap:0.25rem;">
        Suspend on rain above
        <input type="number" style="width:55px;" bind:value={suspendOnRainAbove}
          disabled={!suspendOnRainEnabled || !sectionEnabled} />
        L/m²
        <label class="toggle" style="margin-left:0.25rem;">
          <input type="checkbox" bind:checked={suspendOnRainEnabled} disabled={!sectionEnabled} />
          <span class="toggle-slider"></span>
        </label>
      </span>
    </span>
  </div>

  <div style:opacity={sectionEnabled ? 1 : 0.55}>
    {#each schedules as schedule, i}
      <ScheduleCard {schedule} zoneIds={area.zone_ids ?? []} zoneNames={area.zone_names ?? []}
        index={i} type={area.id} ondelete={() => deleteSchedule(i)}
        {wsConnected} {areaRunning} {areaPaused}
        onstart={() => sendCommand('run_schedule', area.id, { scheduleIndex: i })} />
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
