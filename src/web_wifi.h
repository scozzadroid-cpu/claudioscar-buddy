// src/web_wifi.h — dedicated WiFi setup page (/wifi) served by net.h.
// Scanning is asynchronous: the page polls /api/scan until results arrive,
// so the web server (and the buddy's UI loop) never blocks on a scan.
#pragma once
#include <pgmspace.h>

static const char WEB_WIFI_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi · claudioscar-buddy</title>
<style>
:root{--bg:#000;--card:#111;--line:#222;--text:#eee;--dim:#888;--acc:#d97757}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:560px;margin:0 auto;padding:16px}
a{color:var(--acc);text-decoration:none}
h1{font-size:22px;margin:8px 0 14px}
section{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin:12px 0}
.sub{color:var(--dim);font-size:13px}
.net{display:flex;justify-content:space-between;align-items:center;padding:12px 4px;border-top:1px solid var(--line);cursor:pointer}
.net:first-child{border-top:0}.net:hover{background:#181818}
.bars{font-family:monospace;color:var(--acc)}
label{display:block;margin:10px 0 4px;color:var(--dim);font-size:13px}
input{width:100%;background:#000;color:var(--text);border:1px solid #333;border-radius:9px;padding:10px;font:inherit}
button{background:var(--acc);color:#000;border:0;border-radius:9px;padding:10px 16px;font:inherit;font-weight:600;cursor:pointer;margin:10px 6px 0 0}
button.sec{background:#222;color:var(--text)}
.spin{display:inline-block;width:14px;height:14px;border:2px solid #444;border-top-color:var(--acc);border-radius:50%;animation:s 0.8s linear infinite;vertical-align:-2px;margin-right:6px}
@keyframes s{to{transform:rotate(360deg)}}
</style></head><body><main>
<a href="/">← back</a>
<h1>WiFi</h1>
<section><div class="sub" id="st">…</div></section>
<section>
<div style="display:flex;justify-content:space-between;align-items:center">
<b>Networks</b><button class="sec" id="rescan" onclick="scan()">Scan again</button></div>
<div id="list"><div class="sub"><span class="spin"></span>scanning…</div></div>
</section>
<section>
<label>Network name (SSID)</label><input id="ssid" maxlength="32" autocomplete="off">
<label>Password</label><input id="pass" type="password" maxlength="64">
<button onclick="go()">Connect</button><button class="sec" onclick="forget()">Forget saved network</button>
<div class="sub" id="msg" style="margin-top:10px"></div>
</section>
</main><script>
const $=id=>document.getElementById(id);
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function bars(r){return r>-55?'▂▄▆█':r>-67?'▂▄▆_':r>-78?'▂▄__':'▂___'}
async function status(){try{const s=await (await fetch('/api/status')).json();const w=s.wifi;
 $('st').textContent=w.mode=='sta'?'Connected to '+w.ssid+' · '+w.ip+' · http://claudioscar-buddy.local':
  w.mode=='connecting'?'Connecting to '+w.ssid+'…':'Hotspot '+w.ap+(w.ssid?' · saved network "'+w.ssid+'" not reachable':' · no network saved');
 if(!$('ssid').value)$('ssid').value=w.ssid}catch(e){$('st').textContent='offline'}}
let timer=null;
async function scan(){clearTimeout(timer);$('rescan').disabled=true;
 $('list').innerHTML='<div class="sub"><span class="spin"></span>scanning…</div>';poll(true)}
async function poll(start){try{const r=await (await fetch('/api/scan'+(start?'?start=1':''))).json();
 if(r.state!='done'){timer=setTimeout(()=>poll(false),1000);return}
 $('rescan').disabled=false;
 const l=r.nets.sort((a,b)=>b.rssi-a.rssi);
 $('list').innerHTML=l.length?l.map(n=>'<div class="net" data-s="'+esc(n.ssid)+'"><span>'+esc(n.ssid)+(n.open?'':' 🔒')+
  '</span><span class="bars">'+bars(n.rssi)+'</span></div>').join(''):'<div class="sub">no networks found</div>';
 document.querySelectorAll('.net').forEach(e=>e.onclick=()=>{$('ssid').value=e.dataset.s;$('pass').focus()})
}catch(e){timer=setTimeout(()=>poll(false),1500)}}
async function send(ssid,pass){const r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,pass})});if(!r.ok)throw 0}
async function go(){if(!$('ssid').value)return;try{await send($('ssid').value,$('pass').value);
 $('msg').textContent='Saved. The buddy is connecting — if it works, the hotspot closes and the page moves to http://claudioscar-buddy.local (the WIFI info page on the buddy shows the new IP).';
 setInterval(status,3000)}catch(e){$('msg').textContent='Error saving.'}}
async function forget(){if(!confirm('Forget the saved network?'))return;try{await send('','');$('ssid').value='';$('msg').textContent='Forgotten.';status()}catch(e){}}
status();scan();
</script></body></html>)HTML";
