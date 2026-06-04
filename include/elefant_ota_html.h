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
      xhr.timeout=30000;
      xhr.ontimeout=()=>reject(new Error('Upload timeout'));
      xhr.upload.onprogress=e=>{if(e.lengthComputable)setBar(Math.round(e.loaded/e.total*100));};
      xhr.onload=()=>{
        if(xhr.status===200){setBar(100);resolve();}
        else reject(Object.assign(new Error(xhr.responseText||'Upload error '+xhr.status),{fatal:true}));
      };
      xhr.onerror=()=>reject(new Error('Network error'));
      const fd=new FormData();fd.append('firmware',file,file.name);xhr.send(fd);
    });
    setStatus('Done! Device rebooting — reconnecting in 10 s…','ok');
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
