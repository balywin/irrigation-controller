# OTA Upload Retry on WiFi Drop — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Auto-retry OTA firmware upload (up to 10 times, 3 s apart) when WiFi drops mid-transfer, without user intervention.

**Architecture:** On network failure the JS state machine resets the progress bar to 0%, probes `GET /update` every 3 s to detect reconnect, then restarts the full `/ota/start` → `/ota/upload` sequence. The firmware `/ota/start` handler calls `Update.abort()` before `Update.begin()` so every retry starts from a clean flash-write state. Fatal server errors (HTTP 4xx) skip retry and show a manual Retry button.

**Tech Stack:** ESP32 Arduino, ESPAsyncWebServer, `Update` OTA library, inline HTML/JS in PROGMEM C++ raw string literal.

**Spec:** `docs/superpowers/specs/2026-06-04-ota-resume-design.md`

---

## File Map

| File | Change |
|------|--------|
| `src/ElefantOTA.cpp` | Add `Update.abort()` + counter reset before `Update.begin()` in both async (line 93) and non-async (line 183) `/ota/start` handlers |
| `include/elefant_ota_html.h` | Replace `<script>` block and add retry button to HTML markup |

---

### Task 1: Firmware — clean Update state on each /ota/start call

**Files:**
- Modify: `src/ElefantOTA.cpp:93-102` (async ESP32 block)
- Modify: `src/ElefantOTA.cpp:183-192` (non-async ESP32 block)

> There is no automated test framework for this firmware code. Correctness is verified by build + manual OTA flash cycle.

- [ ] **Step 1: Add abort + reset in the async handler (line 93)**

In `src/ElefantOTA.cpp`, find the async `/ota/start` ESP32 block (around line 93). Change:

```cpp
      #elif defined(ESP32)  
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, mode == OTA_MODE_FILESYSTEM ? U_SPIFFS : U_FLASH)) {
```

to:

```cpp
      #elif defined(ESP32)
        if (Update.isRunning()) {
          Update.abort();
        }
        _current_progress_size = 0;
        _update_error_str = "";
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, mode == OTA_MODE_FILESYSTEM ? U_SPIFFS : U_FLASH)) {
```

- [ ] **Step 2: Add abort + reset in the non-async handler (line 183)**

In `src/ElefantOTA.cpp`, find the non-async `/ota/start` ESP32 block (around line 183). Apply the identical change:

```cpp
      #elif defined(ESP32)
        if (Update.isRunning()) {
          Update.abort();
        }
        _current_progress_size = 0;
        _update_error_str = "";
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, mode == OTA_MODE_FILESYSTEM ? U_SPIFFS : U_FLASH)) {
```

- [ ] **Step 3: Build to verify compilation**

```bash
cd /Users/baa1sf3/balywin/PlatformIO/irrigation-controller
pio run -e denky32 2>&1 | tail -5
```

Expected: last line contains `SUCCESS` with no errors. If not, fix compile errors before proceeding.

- [ ] **Step 4: Commit**

```bash
git add src/ElefantOTA.cpp
git commit -m "fix: abort stale Update session on /ota/start retry"
```

---

### Task 2: JS state machine — retry on WiFi drop

**Files:**
- Modify: `include/elefant_ota_html.h` (HTML markup + `<script>` block)

> No automated tests possible for inline PROGMEM JS. Correctness verified by build + behavioral checklist in Task 3.

- [ ] **Step 1: Replace the entire content of `include/elefant_ota_html.h`**

The file is a single C++ header with a `PROGMEM` raw string literal. Replace the full file content with:

```cpp
#pragma once
#include <pgmspace.h>

static const char ELEGANT_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OTA Update</title>
<style>
*,*::before,*::after{box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;background:#f4f6f8;color:#333;font-size:14px;padding:1rem}
h2{color:#1e40af;margin:0 0 0.25rem}
.version{color:#6b7280;font-size:0.85rem;margin-bottom:1.25rem}
.card{background:#fff;border-radius:8px;padding:1.25rem;box-shadow:0 1px 4px rgba(0,0,0,.1);margin-bottom:1rem}
label{display:block;font-weight:600;font-size:0.85rem;color:#374151;margin-bottom:0.3rem}
select,input[type=file]{width:100%;padding:0.45rem 0.6rem;border:1px solid #d1d5db;border-radius:6px;font-size:0.9rem;background:#fff;margin-bottom:0.75rem}
.btn{display:block;width:100%;background:#2563eb;color:#fff;border:none;padding:0.6rem 1rem;border-radius:6px;cursor:pointer;font-size:0.95rem;font-weight:600;margin-top:0.25rem;transition:background 0.15s}
.btn:hover:not(:disabled){background:#1d4ed8}
.btn:disabled{background:#9ca3af;cursor:default}
.btn-retry{background:#16a34a}
.btn-retry:hover:not(:disabled){background:#15803d}
#progress-wrap{margin-top:0.75rem;display:none}
#progress-bar{height:8px;background:#dbeafe;border-radius:4px;overflow:hidden}
#progress-fill{height:100%;background:#2563eb;width:0%;transition:width 0.2s}
#pct{font-size:0.78rem;color:#6b7280;margin-top:0.25rem;text-align:right}
#status{margin-top:0.6rem;font-size:0.88rem;min-height:1.2em}
.ok{color:#16a34a;font-weight:600}
.err{color:#dc2626;font-weight:600}
</style>
</head>
<body>
<h2>OTA Update</h2>
<p class="version">Current firmware: <strong>%FW_VERSION%</strong></p>
<div class="card">
  <label for="mode">Update type</label>
  <select id="mode">
    <option value="firmware">Firmware (.bin)</option>
    <option value="fs">Filesystem (.bin)</option>
  </select>
  <label for="file">Binary file</label>
  <input type="file" id="file" accept=".bin">
  <button class="btn" id="btn" onclick="upload()" disabled>Upload</button>
  <button class="btn btn-retry" id="retry-btn" onclick="retry()" style="display:none;margin-top:0.5rem">Retry Upload</button>
  <div id="progress-wrap">
    <div id="progress-bar"><div id="progress-fill"></div></div>
    <div id="pct">0%</div>
  </div>
  <div id="status"></div>
</div>
<script>
const fileEl=document.getElementById('file'),
      btn=document.getElementById('btn'),
      retryBtn=document.getElementById('retry-btn'),
      statusEl=document.getElementById('status'),
      wrap=document.getElementById('progress-wrap'),
      fill=document.getElementById('progress-fill'),
      pct=document.getElementById('pct');

const MAX_RETRIES=10,PROBE_MS=3000;

fileEl.addEventListener('change',()=>{btn.disabled=!fileEl.files.length;});

function setBar(p){fill.style.width=p+'%';pct.textContent=p+'%';}
function setStatus(msg,cls){statusEl.className=cls||'';statusEl.textContent=msg;}

function showFatal(msg){
  setStatus(msg,'err');
  retryBtn.style.display='block';
  btn.disabled=false;
}

async function doUpload(file,mode,attempt){
  retryBtn.style.display='none';
  setBar(0);
  wrap.style.display='block';
  setStatus('Uploading… (attempt '+attempt+'/'+MAX_RETRIES+')');
  try{
    const ac=new AbortController();
    const t=setTimeout(()=>ac.abort(),8000);
    let init;
    try{init=await fetch('/ota/start?mode='+mode,{signal:ac.signal});}
    finally{clearTimeout(t);}
    if(!init.ok)throw Object.assign(new Error('Init failed: '+await init.text()),{fatal:true});
    await new Promise((resolve,reject)=>{
      const xhr=new XMLHttpRequest();
      xhr.open('POST','/ota/upload');
      xhr.upload.onprogress=e=>{if(e.lengthComputable)setBar(Math.round(e.loaded/e.total*100));};
      xhr.onload=()=>{
        if(xhr.status===200){setBar(100);resolve();}
        else reject(Object.assign(new Error(xhr.responseText||'Upload error '+xhr.status),{fatal:true}));
      };
      xhr.onerror=()=>reject(new Error('Network error'));
      const fd=new FormData();fd.append('firmware',file,file.name);xhr.send(fd);
    });
    setStatus('Done! Device rebooting — reconnecting in 10 s…','ok');
    setTimeout(()=>location.reload(),10000);
  }catch(e){
    setBar(0);
    if(e.fatal){showFatal(e.message);return;}
    if(attempt>=MAX_RETRIES){showFatal('Upload failed after '+MAX_RETRIES+' attempts.');return;}
    waitRetry(file,mode,attempt);
  }
}

function waitRetry(file,mode,attempt){
  setStatus('Connection lost — attempt '+attempt+'/'+MAX_RETRIES+'. Reconnecting…');
  const probe=()=>{
    const ac=new AbortController();
    const t=setTimeout(()=>ac.abort(),8000);
    fetch('/update',{signal:ac.signal})
      .then(r=>{clearTimeout(t);if(r.ok)doUpload(file,mode,attempt+1);else setTimeout(probe,PROBE_MS);})
      .catch(()=>{clearTimeout(t);setTimeout(probe,PROBE_MS);});
  };
  setTimeout(probe,PROBE_MS);
}

function upload(){
  const file=fileEl.files[0];if(!file)return;
  btn.disabled=true;
  doUpload(file,document.getElementById('mode').value,1);
}

function retry(){
  const file=fileEl.files[0];if(!file)return;
  btn.disabled=true;
  doUpload(file,document.getElementById('mode').value,1);
}
</script>
</body>
</html>)rawliteral";
```

- [ ] **Step 2: Build to verify compilation**

```bash
cd /Users/baa1sf3/balywin/PlatformIO/irrigation-controller
pio run -e denky32 2>&1 | tail -5
```

Expected: last line contains `SUCCESS` with no errors.

- [ ] **Step 3: Commit**

```bash
git add include/elefant_ota_html.h
git commit -m "feat: OTA upload auto-retry on WiFi drop (10 attempts, 3s probe)"
```

---

### Task 3: Behavioral verification

> Flash the device and run through the checklist manually. No automated test runner available for this feature.

Flash command (after Task 1 + 2 builds pass):

```bash
pio run -e denky32 --target upload
```

- [ ] **Happy path** — select a valid `.bin`, click Upload. Bar fills 0→100%, status reads "Done! Device rebooting — reconnecting in 10 s…", page reloads, new firmware version shown on `/update`.

- [ ] **WiFi drop at ~50%** — simulate by toggling the router off mid-upload. Bar resets to 0%, status reads "Connection lost — attempt 1/10. Reconnecting…". Turn router back on within 30 s. Bar fills again from 0%, upload completes, device reboots.

- [ ] **WiFi already at 100% (TCP buffer full) then drop** — turn router off when bar shows 100% but before "Done" appears. Same retry behaviour as above.

- [ ] **10 consecutive failures** — keep router off for all 10 attempts (~30 s). Status becomes "Upload failed after 10 attempts." in red. Green "Retry Upload" button appears. Clicking it restarts from attempt 1.

- [ ] **Bad MD5 hash** — use `/ota/start?mode=firmware&hash=deadbeef`. Status shows "Init failed: MD5 parameter invalid" in red immediately. No retry loop entered. "Retry Upload" button appears.

- [ ] **Second upload after first failure** — after any fatal state, click "Retry Upload" with the same file selected. Attempt counter resets to 1, upload begins fresh.

- [ ] **Probe interval** — with router off, watch status. Each probe fires every 3 s (visible if browser DevTools Network tab is open — should see `GET /update` requests spaced ~3 s apart).
