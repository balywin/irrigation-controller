<script>
  import { toggleDay as _toggleDay, addTime as _addTime, removeTime as _removeTime } from '../lib/utils.js';

  const DAY_LABELS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  const DURATION_PRESETS = [5, 10, 15, 20, 30, 45, 60, 90];

  let { schedule, index, ondelete } = $props();

  function toggleDay(day, checked) { schedule.daysOfWeek = _toggleDay(schedule.daysOfWeek, day, checked); }
  function addTime() { schedule.startTimes = _addTime(schedule.startTimes); }
  function removeTime(ti) { schedule.startTimes = _removeTime(schedule.startTimes, ti); }
</script>

<div class="card" style:opacity={schedule.enabled ? 1 : 0.65}>
  <div class="card-header">
    <div style="display:flex;align-items:center;gap:0.5rem;">
      <h3 style="margin:0;">Filling Schedule {index + 1}</h3>
      <label class="toggle">
        <input type="checkbox" bind:checked={schedule.enabled} />
        <span class="toggle-slider"></span>
      </label>
    </div>
    <button type="button" class="btn btn-danger" onclick={ondelete}>Delete</button>
  </div>

  <div class="field" style="flex-wrap:wrap;gap:0.3rem;">
    <span>Duration (min)</span>
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
            id="time-filling-{index}-{ti}"
            type="time"
            lang="en-GB"
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
