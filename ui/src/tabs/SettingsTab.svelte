<script>
  import { saveConfig, getConfig } from '../lib/api.js';

  let { config = $bindable() } = $props();

  let saving = $state(false);
  let saveMsg = $state('');
  let saveError = $state('');

  function addZone(area) {
    const nextId = area.zone_ids?.length ? Math.max(...area.zone_ids) + 1 : 1;
    area.zone_ids = [...(area.zone_ids ?? []), nextId];
    area.zone_names = [...(area.zone_names ?? []), ''];
  }

  function removeZone(area, i) {
    area.zone_ids = area.zone_ids.filter((_, idx) => idx !== i);
    area.zone_names = area.zone_names.filter((_, idx) => idx !== i);
  }

  async function save() {
    saving = true; saveMsg = ''; saveError = '';
    try {
      await saveConfig('app_config.json', config);
      saveMsg = 'Saved';
    } catch (e) { saveError = e.message; }
    finally { saving = false; }
  }

  let restoreMsg = $state('');
  let restoreError = $state('');
  let restoring = $state(false);

  async function downloadBackup() {
    restoreMsg = ''; restoreError = '';
    try {
      const [schedule, manual_control] = await Promise.all([
        getConfig('schedule.json'), getConfig('manual_control.json'),
      ]);
      const blob = new Blob(
        [JSON.stringify({ app: config, schedule, manual_control }, null, 2)],
        { type: 'application/json' }
      );
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `irrigation-backup-${new Date().toISOString().split('T')[0]}.json`;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
      restoreMsg = 'Backup downloaded';
    } catch (e) { restoreError = 'Backup failed: ' + e.message; }
  }

  async function handleRestoreFile(e) {
    const file = e.target.files[0];
    if (!file) return;
    restoreMsg = ''; restoreError = ''; restoring = true;
    try {
      let backup;
      try { backup = JSON.parse(await file.text()); }
      catch { restoreError = 'Invalid JSON file'; return; }
      if (!backup.app || !backup.schedule || !backup.manual_control) {
        restoreError = 'Missing keys: app, schedule, manual_control'; return;
      }
      const failed = [];
      for (const [name, data] of [['app_config.json', backup.app], ['schedule.json', backup.schedule], ['manual_control.json', backup.manual_control]]) {
        try { await saveConfig(name, data); } catch { failed.push(name); }
      }
      if (!failed.length) { config = backup.app; restoreMsg = 'Restored'; }
      else restoreError = 'Failed: ' + failed.join(', ');
    } finally { restoring = false; e.target.value = ''; }
  }
</script>

<div class="section">
  <h2>Device</h2>
  <div class="field">
    <label for="device-name">Device Name</label>
    <input id="device-name" type="text" bind:value={config.device_name} />
  </div>
</div>

<div class="section">
  <h2>WiFi</h2>
  <div class="field">
    <label for="wifi-ssid">SSID</label>
    <input id="wifi-ssid" type="text" bind:value={config.wifi_ssid} />
  </div>
  <div class="field">
    <label for="wifi-password">Password</label>
    <input id="wifi-password" type="password" bind:value={config.wifi_password} />
  </div>
</div>

<div class="section" style:opacity={config.filling.enabled ? 1 : 0.55}>
  <div style="display:flex;justify-content:space-between;align-items:center;margin:0 0 1rem;">
    <h2 style="margin:0;">Filling</h2>
    <span style="display:flex;align-items:center;gap:0.5rem;">
      <span style="font-size:0.75rem;text-transform:uppercase;letter-spacing:0.06em;color:#888;font-weight:600;">Enabled</span>
      <label class="toggle"><input type="checkbox" bind:checked={config.filling.enabled} /><span class="toggle-slider"></span></label>
    </span>
  </div>
  <div class="field">
    <label for="f-max">Max Minutes</label>
    <input id="f-max" type="number" min="1" bind:value={config.filling.max_minutes} />
  </div>
  <div class="field">
    <label for="f-pump">Pump ID</label>
    <input id="f-pump" type="number" min="1" bind:value={config.filling.pump_id} />
  </div>
  <div class="field">
    <label for="f-lvl">Level Filtering (s)</label>
    <input id="f-lvl" type="number" min="0" bind:value={config.filling.level_filtering_seconds} />
  </div>
  <div class="field">
    <label for="f-leak">Leakage Threshold</label>
    <input id="f-leak" type="number" min="1" bind:value={config.filling.leakage_detector_threshold} />
  </div>
  <div class="field">
    <label for="f-hi">High Level Pressure</label>
    <input id="f-hi" type="number" bind:value={config.filling.high_level_pressure} />
  </div>
  <div class="field">
    <label for="f-lo">Low Level Pressure</label>
    <input id="f-lo" type="number" bind:value={config.filling.low_level_pressure} />
  </div>
</div>

{#each (config.areas ?? []) as area}
  <div class="section" style:opacity={area.enabled ? 1 : 0.55}>
    <div style="display:flex;justify-content:space-between;align-items:center;margin:0 0 1rem;">
      <h2 style="margin:0;">{area.id}</h2>
      <span style="display:flex;align-items:center;gap:0.5rem;">
        <span style="font-size:0.75rem;text-transform:uppercase;letter-spacing:0.06em;color:#888;font-weight:600;">Enabled</span>
        <label class="toggle"><input type="checkbox" bind:checked={area.enabled} /><span class="toggle-slider"></span></label>
      </span>
    </div>
    <div class="field">
      <label for="{area.id}-desc">Description</label>
      <input id="{area.id}-desc" type="text" style="flex:1;min-width:200px;width:auto;" bind:value={area.description} />
    </div>
    <div class="field">
      <label for="{area.id}-pump">Pump ID</label>
      <input id="{area.id}-pump" type="number" min="1" bind:value={area.pump_id} />
    </div>
    <div class="field">
      <label for="{area.id}-delay">Pump Start Delay (s)</label>
      <input id="{area.id}-delay" type="number" min="0" bind:value={area.pump_start_delay_seconds} />
    </div>
    <div class="field">
      <label for="{area.id}-valve">Main Valve ID</label>
      <input id="{area.id}-valve" type="number" min="1" bind:value={area.main_valve_id} />
    </div>
    <div class="field">
      <label for="{area.id}-max">Max Minutes</label>
      <input id="{area.id}-max" type="number" min="1" bind:value={area.max_minutes} />
    </div>
    <div class="field" style="flex-direction:column;align-items:flex-start;gap:0.4rem;">
      <span>Zones (ID / Name)</span>
      <div style="display:flex;flex-direction:column;gap:0.3rem;width:100%;">
        {#each (area.zone_ids ?? []) as _, zi}
          <div style="display:flex;gap:0.5rem;align-items:center;">
            <input type="number" style="width:45px;" bind:value={area.zone_ids[zi]} min="1" />
            <input type="text" style="flex:1;" bind:value={area.zone_names[zi]} placeholder="Name" />
            <button type="button" class="btn btn-danger btn-sm" onclick={() => removeZone(area, zi)}>✕</button>
          </div>
        {/each}
        <button type="button" class="btn btn-secondary btn-sm" onclick={() => addZone(area)}>+ Add zone</button>
      </div>
    </div>
  </div>
{/each}

<div class="save-bar">
  <button type="button" class="btn btn-primary" onclick={save} disabled={saving}>
    {saving ? 'Saving…' : 'Save'}
  </button>
  {#if saveMsg}<span class="status-ok">{saveMsg}</span>{/if}
  {#if saveError}<span class="status-err">{saveError}</span>{/if}
</div>

<div class="section">
  <h2>Backup / Restore</h2>
  <div class="field">
    <span>Download all configs</span>
    <button type="button" class="btn btn-secondary" onclick={downloadBackup}>Download</button>
  </div>
  <div class="field">
    <span>Restore from backup</span>
    <input type="file" accept=".json" disabled={restoring} onchange={handleRestoreFile} />
  </div>
  {#if restoring}<p style="margin:0.5rem 0 0;color:#666;">Restoring…</p>{/if}
  {#if restoreMsg}<p class="status-ok" style="margin:0.5rem 0 0;">{restoreMsg}</p>{/if}
  {#if restoreError}<p class="status-err" style="margin:0.5rem 0 0;">{restoreError}</p>{/if}
</div>
