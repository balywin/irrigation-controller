<script>
  import { normalizeGroups, toggleDay as _toggleDay, addTime as _addTime, removeTime as _removeTime } from '../lib/utils.js';

  const DAY_LABELS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  const DURATION_PRESETS = [5, 10, 15, 20, 30, 45, 60, 90];

  let { schedule, zoneIds, zoneNames, index, type, ondelete, onstart, wsConnected = true, areaRunning = false, areaPaused = false } = $props();

  let startDisabled = $derived(
    !wsConnected || areaRunning || areaPaused || !schedule.enabled ||
    schedule.zones.length === 0 || !schedule.durationMinutes || schedule.durationMinutes <= 0
  );
  let startTitle = $derived(
    !wsConnected ? 'WebSocket disconnected' :
    areaRunning ? 'Area already running' :
    areaPaused ? 'Area is paused' :
    !schedule.enabled ? 'Schedule disabled' :
    schedule.zones.length === 0 ? 'No zones configured' :
    (!schedule.durationMinutes || schedule.durationMinutes <= 0) ? 'Duration must be > 0' : ''
  );

  let groups = $state(normalizeGroups(schedule.zones));
  let currentGroupIdx = $state(0);
  let curGroup = $derived(groups[currentGroupIdx] ?? []);

  function syncZones() {
    schedule.zones = groups.map(g => [...g]);
  }

  function addGroup() {
    groups = [...groups, []];
    currentGroupIdx = groups.length - 1;
    syncZones();
  }
  function removeGroup() {
    if (groups.length === 0) return;
    groups = groups.filter((_, i) => i !== currentGroupIdx);
    currentGroupIdx = Math.max(0, Math.min(currentGroupIdx, groups.length - 1));
    syncZones();
  }
  function navGroup(delta) {
    currentGroupIdx = Math.max(0, Math.min(groups.length - 1, currentGroupIdx + delta));
  }
  function toggleZone(z) {
    if (groups.length === 0) return;
    const g = [...(groups[currentGroupIdx] ?? [])];
    const idx = g.indexOf(z);
    if (idx >= 0) g.splice(idx, 1);
    else { g.push(z); g.sort((a, b) => a - b); }
    groups = groups.map((og, i) => i === currentGroupIdx ? g : og);
    syncZones();
  }

  let dragSrc = null;
  function onChipDragStart(idx, e) {
    dragSrc = idx;
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', String(idx));
  }
  function onChipDragOver(e) {
    if (dragSrc == null) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
  }
  function onChipDrop(targetIdx, e) {
    e.preventDefault();
    const src = dragSrc;
    dragSrc = null;
    if (src == null || src === targetIdx) return;
    const arr = [...groups];
    const [moved] = arr.splice(src, 1);
    arr.splice(targetIdx, 0, moved);
    groups = arr;
    if (currentGroupIdx === src) currentGroupIdx = targetIdx;
    else if (src < currentGroupIdx && targetIdx >= currentGroupIdx) currentGroupIdx -= 1;
    else if (src > currentGroupIdx && targetIdx <= currentGroupIdx) currentGroupIdx += 1;
    syncZones();
  }
  function onChipDragEnd() { dragSrc = null; }

  function toggleDay(day, checked) { schedule.daysOfWeek = _toggleDay(schedule.daysOfWeek, day, checked); }
  function addTime() { schedule.startTimes = _addTime(schedule.startTimes); }
  function removeTime(ti) { schedule.startTimes = _removeTime(schedule.startTimes, ti); }
</script>

<div class="card" style:opacity={schedule.enabled ? 1 : 0.65}>
  <div class="card-header">
    <div style="display:flex;align-items:center;gap:0.5rem;">
      <h3 style="margin:0;">{type} Schedule {index + 1}</h3>
      <label class="toggle">
        <input type="checkbox" bind:checked={schedule.enabled} />
        <span class="toggle-slider"></span>
      </label>
      <button type="button" class="btn btn-sm btn-primary"
        disabled={startDisabled}
        title={startTitle}
        onclick={onstart}
      >Start</button>
    </div>
    <button type="button" class="btn btn-danger" onclick={ondelete}>Delete</button>
  </div>

  <div class="field" style="flex-wrap:wrap;gap:0.5rem;align-items:center;justify-content:flex-start;">
    <span>Zones in group</span>
    <div style="display:flex;flex-wrap:wrap;gap:0.3rem;">
      {#each zoneIds as z, zi}
        <button type="button" class="btn btn-sm"
          class:btn-primary={curGroup.includes(z)}
          class:btn-secondary={!curGroup.includes(z)}
          disabled={groups.length === 0}
          onclick={() => toggleZone(z)}
          title={`Toggle zone ${z}`}><b>{z}</b> {zoneNames[zi] ?? ''}</button>
      {/each}
    </div>
  </div>

  <div class="field" style="flex-wrap:wrap;gap:0.3rem;align-items:center;">
    <span>Sequence</span>
    <div style="display:flex;flex-wrap:wrap;gap:0.3rem;align-items:center;flex:1;">
      <span style="display:inline-flex;gap:0.2rem;margin-right:1rem;">
        <button type="button" class="btn btn-sm btn-danger"
          onclick={removeGroup} disabled={groups.length === 0} title="Remove current group">✕</button>
        <button type="button" class="btn btn-sm btn-primary"
          onclick={addGroup} title="Add new group">+</button>
      </span>
      {#each groups as g, gi}
        <button type="button" class="btn btn-sm"
          class:btn-primary={gi === currentGroupIdx}
          class:btn-secondary={gi !== currentGroupIdx}
          style="font-family:monospace;min-width:2rem;cursor:grab;"
          draggable="true"
          ondragstart={(e) => onChipDragStart(gi, e)}
          ondragover={onChipDragOver}
          ondrop={(e) => onChipDrop(gi, e)}
          ondragend={onChipDragEnd}
          onclick={() => currentGroupIdx = gi}
          title={`Group ${gi + 1} — drag to reorder`}>{g.join('') || '·'}</button>
      {/each}
    </div>
  </div>

  <div class="field" style="flex-wrap:wrap;gap:0.3rem;">
    <span>Duration per zone group (min)</span>
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
            id="time-{type}-{index}-{ti}"
            type="time"
            lang="en-BG"
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
