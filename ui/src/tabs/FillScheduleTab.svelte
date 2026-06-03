<script>
  import { onMount } from 'svelte';
  import { getConfig, saveConfig } from '../lib/api.js';
  import { ws } from '../lib/ws.svelte.js';
  import { sortTimes } from '../lib/utils.js';
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
      const section = raw['filling'] ?? { enabled: true, schedules: [] };
      sectionEnabled = section.enabled ?? true;
      schedules = (section.schedules ?? []).map(s => ({
        enabled: s.enabled ?? true,
        durationMinutes: s.durationMinutes ?? 15,
        startTimes: s.startTimes ?? ['07:00'],
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

  // Reload sectionEnabled when manual filling stops — firmware restores schedule.json at that point.
  let prevManual = false;
  $effect(() => {
    const m = ws.status?.filling?.manuallyStarted ?? false;
    if (prevManual && !m) {
      getConfig('schedule.json').then(raw => {
        if (!raw) return;
        fullScheduleRaw = { ...(fullScheduleRaw ?? {}), ...raw };
        const section = raw['filling'];
        if (section != null) sectionEnabled = section.enabled ?? true;
      }).catch(() => {});
    }
    prevManual = m;
  });

  function addSchedule() {
    schedules = [...schedules, {
      enabled: true, durationMinutes: 15,
      startTimes: ['07:00'], daysOfWeek: [1,2,3,4,5,6,7],
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
      schedules.forEach(s => { s.startTimes = sortTimes(s.startTimes); });
      const payload = {
        ...(fullScheduleRaw ?? {}),
        filling: { enabled: sectionEnabled, schedules },
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
<div>
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
