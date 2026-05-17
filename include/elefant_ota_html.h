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
  <div id="progress-wrap">
    <div id="progress-bar"><div id="progress-fill"></div></div>
    <div id="pct">0%</div>
  </div>
  <div id="status"></div>
</div>
<script>
const fileEl=document.getElementById('file'),btn=document.getElementById('btn'),
      statusEl=document.getElementById('status'),wrap=document.getElementById('progress-wrap'),
      fill=document.getElementById('progress-fill'),pct=document.getElementById('pct');
fileEl.addEventListener('change',()=>{btn.disabled=!fileEl.files.length;});
async function upload(){
  const file=fileEl.files[0];if(!file)return;
  const mode=document.getElementById('mode').value;
  btn.disabled=true;statusEl.className='';statusEl.textContent='Initialising…';
  wrap.style.display='block';fill.style.width='0%';pct.textContent='0%';
  try{
    const init=await fetch('/ota/start?mode='+mode,{method:'GET'});
    if(!init.ok)throw new Error('Init failed: '+await init.text());
    statusEl.textContent='Uploading…';
    await new Promise((resolve,reject)=>{
      const xhr=new XMLHttpRequest();
      xhr.open('POST','/ota/upload');
      xhr.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);fill.style.width=p+'%';pct.textContent=p+'%';}};
      xhr.onload=()=>{if(xhr.status===200){fill.style.width='100%';pct.textContent='100%';resolve();}else reject(new Error(xhr.responseText||'Upload error '+xhr.status));};
      xhr.onerror=()=>reject(new Error('Network error'));
      const fd=new FormData();fd.append('firmware',file,file.name);xhr.send(fd);
    });
    statusEl.className='ok';statusEl.textContent='Done! Device rebooting — reconnecting in 10 s…';
    setTimeout(()=>location.reload(),10000);
  }catch(e){statusEl.className='err';statusEl.textContent=e.message;btn.disabled=false;}
}
</script>
</body>
</html>)rawliteral";
