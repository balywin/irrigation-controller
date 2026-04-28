// fallback_page.h
// Minimal fallback page served from PROGMEM when LittleFS web files are
// missing or corrupt. Provides a filesystem upload form so the device
// is never bricked.
#pragma once

#include <pgmspace.h>

static const char FALLBACK_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Irrigation Controller - Recovery</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;display:flex;justify-content:center;padding:2rem}
.container{max-width:480px;width:100%}
h1{font-size:1.3rem;margin-bottom:.5rem;color:#4fc3f7}
h2{font-size:1rem;margin:1.5rem 0 .5rem;color:#81d4fa}
.warn{background:#2d2200;border:1px solid #f9a825;border-radius:6px;padding:.75rem;margin:1rem 0;font-size:.85rem;color:#ffe082}
.status{background:#1e1e3a;border-radius:6px;padding:.75rem;margin:.5rem 0;font-size:.85rem}
.status span{color:#aaa}
form{background:#1e1e3a;border-radius:8px;padding:1rem;margin:.5rem 0}
label{display:block;font-size:.85rem;margin-bottom:.4rem;color:#b0bec5}
input[type=file]{width:100%;padding:.4rem;margin-bottom:.75rem;font-size:.85rem}
button{background:#1565c0;color:#fff;border:none;border-radius:4px;padding:.5rem 1.2rem;cursor:pointer;font-size:.9rem;width:100%}
button:hover{background:#1976d2}
.progress{display:none;margin-top:.5rem}
.bar-bg{background:#333;border-radius:4px;height:20px;overflow:hidden}
.bar{background:#4caf50;height:100%;width:0%;transition:width .3s;text-align:center;font-size:.75rem;line-height:20px;color:#fff}
.msg{margin-top:.5rem;font-size:.85rem;text-align:center}
.links{margin-top:1.5rem;font-size:.85rem}
.links a{color:#4fc3f7;margin-right:1rem}
</style>
</head>
<body>
<div class="container">
<h1>Irrigation Controller - Recovery Mode</h1>
<div class="warn">Web UI files not found on filesystem. Use the form below to upload them, or use the OTA page to update firmware.</div>

<h2>Device Status</h2>
<div class="status">
<span>IP:</span> <span id="ip">loading...</span><br>
<span>Uptime:</span> <span id="uptime">loading...</span><br>
<span>Free Heap:</span> <span id="heap">loading...</span><br>
<span>LittleFS:</span> <span id="fs">loading...</span>
</div>

<h2>Upload Filesystem Image (.bin)</h2>
<form id="fsForm">
<label>Select LittleFS image file:</label>
<input type="file" id="fsFile" accept=".bin,.bin.gz">
<button type="submit">Upload Filesystem</button>
<div class="progress" id="fsProg">
<div class="bar-bg"><div class="bar" id="fsBar">0%</div></div>
<div class="msg" id="fsMsg"></div>
</div>
</form>

<h2>Upload Single File to LittleFS</h2>
<form id="fileForm">
<label>Path on device (e.g. /index.html):</label>
<input type="text" id="filePath" placeholder="/index.html"
  style="width:100%;padding:.4rem;margin-bottom:.5rem;background:#2a2a4a;border:1px solid #555;color:#e0e0e0;border-radius:4px;font-size:.85rem">
<label>Select file:</label>
<input type="file" id="singleFile">
<button type="submit">Upload File</button>
<div class="progress" id="fileProg">
<div class="bar-bg"><div class="bar" id="fileBar">0%</div></div>
<div class="msg" id="fileMsg"></div>
</div>
</form>

<div class="links">
<a href="/update">OTA Firmware Update</a>
</div>
</div>

<script>
fetch('/api/fallback/status').then(r=>r.json()).then(d=>{
  document.getElementById('ip').textContent=d.ip||'-';
  document.getElementById('uptime').textContent=d.uptime||'-';
  document.getElementById('heap').textContent=d.heap||'-';
  document.getElementById('fs').textContent=d.fs||'-';
}).catch(()=>{});

document.getElementById('fsForm').addEventListener('submit',function(e){
  e.preventDefault();
  var f=document.getElementById('fsFile').files[0];
  if(!f){alert('Select a file first');return;}
  var xhr=new XMLHttpRequest();
  var prog=document.getElementById('fsProg');
  var bar=document.getElementById('fsBar');
  var msg=document.getElementById('fsMsg');
  prog.style.display='block';
  xhr.open('POST','/api/fallback/fs-upload');
  xhr.upload.onprogress=function(ev){
    if(ev.lengthComputable){var p=Math.round(ev.loaded/ev.total*100);bar.style.width=p+'%';bar.textContent=p+'%';}
  };
  xhr.onload=function(){
    if(xhr.status===200){msg.textContent='Upload complete! Rebooting...';msg.style.color='#4caf50';setTimeout(function(){location.reload();},5000);}
    else{msg.textContent='Error: '+xhr.responseText;msg.style.color='#ef5350';}
  };
  xhr.onerror=function(){msg.textContent='Upload failed';msg.style.color='#ef5350';};
  var fd=new FormData();fd.append('file',f,f.name);
  xhr.send(fd);
});

document.getElementById('fileForm').addEventListener('submit',function(e){
  e.preventDefault();
  var f=document.getElementById('singleFile').files[0];
  var p=document.getElementById('filePath').value;
  if(!f||!p){alert('Select a file and enter a path');return;}
  var xhr=new XMLHttpRequest();
  var prog=document.getElementById('fileProg');
  var bar=document.getElementById('fileBar');
  var msg=document.getElementById('fileMsg');
  prog.style.display='block';
  xhr.open('POST','/api/fallback/file-upload?path='+encodeURIComponent(p));
  xhr.upload.onprogress=function(ev){
    if(ev.lengthComputable){var pc=Math.round(ev.loaded/ev.total*100);bar.style.width=pc+'%';bar.textContent=pc+'%';}
  };
  xhr.onload=function(){
    if(xhr.status===200){msg.textContent='File uploaded OK';msg.style.color='#4caf50';}
    else{msg.textContent='Error: '+xhr.responseText;msg.style.color='#ef5350';}
  };
  xhr.onerror=function(){msg.textContent='Upload failed';msg.style.color='#ef5350';};
  var fd=new FormData();fd.append('file',f,f.name);
  xhr.send(fd);
});
</script>
</body>
</html>)rawliteral";
