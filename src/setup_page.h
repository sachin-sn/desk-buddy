#pragma once
#include <Arduino.h>

// First-boot WiFi setup page, served from firmware so it works before the filesystem is uploaded.
static const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Buddy setup</title>
<style>
 body{font-family:system-ui,sans-serif;background:#0e0f12;color:#e8e8ea;margin:0;padding:20px;max-width:420px;margin:auto}
 h1{font-size:20px} label{display:block;margin:14px 0 4px;font-size:13px;color:#9a9aa3}
 input,select,button{width:100%;box-sizing:border-box;padding:12px;border-radius:8px;border:1px solid #2a2c33;background:#17181d;color:#fff;font-size:16px}
 button{background:#00a7d1;border:0;margin-top:20px;font-weight:600} small{color:#777}
 #msg{margin-top:14px;font-size:14px}
</style></head><body>
<h1>Buddy setup</h1>
<label>WiFi network (2.4 GHz)</label>
<select id="ssid"><option>scanning...</option></select>
<label>WiFi password</label><input id="pass" type="password" autocomplete="off">
<label>Admin password for this buddy (min 6 characters)</label><input id="admin" type="password" autocomplete="off">
<small>Needed to change shortcuts, settings and firmware. If the buddy already has one, enter the existing password.</small>
<label>Name (opens at name.local)</label><input id="host" value="buddy">
<label>Timezone (POSIX string)</label><input id="tz" value="IST-5:30">
<button onclick="save()">Save and restart</button>
<div id="msg"></div>
<script>
const $=id=>document.getElementById(id);
fetch('/api/scan').then(r=>r.json()).then(l=>{
  $('ssid').innerHTML=l.map(n=>'<option>'+n.replace(/</g,'&lt;')+'</option>').join('')||'<option>none found</option>';
}).catch(()=>{$('ssid').innerHTML='<option>scan failed</option>'});
function save(){
  const b=new URLSearchParams({ssid:$('ssid').value,pass:$('pass').value,admin:$('admin').value,host:$('host').value,tz:$('tz').value});
  $('msg').textContent='Saving...';
  fetch('/api/wifi',{method:'POST',body:b}).then(r=>r.json()).then(j=>{
    $('msg').textContent=j.ok?'Saved. The buddy is restarting - reconnect your phone to your normal WiFi, then open http://'+$('host').value+'.local or the IP shown on the buddy (long-press the knob).':('Error: '+j.error);
  }).catch(()=>{$('msg').textContent='Restarting... if the page does not respond, the settings were probably saved.'});
}
</script></body></html>)HTML";

static const char NO_FS_HTML[] PROGMEM =
  "<html><body style='font-family:sans-serif;padding:24px'><h2>Web UI not installed</h2>"
  "<p>The firmware is running, but the filesystem image is missing.</p>"
  "<p>On your computer run <code>pio run -t uploadfs</code> in the project folder.</p></body></html>";
