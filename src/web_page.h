// src/web_page.h — the configuration page served by net.h.
#pragma once
#include <pgmspace.h>

static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>claudioscar-buddy</title>
<style>
:root{--bg:#000;--card:#111;--line:#222;--text:#eee;--dim:#888;--acc:#d97757;--ok:#3c3;--bad:#e44}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:560px;margin:0 auto;padding:16px}
h1{font-size:22px;margin:8px 0 2px}h1 span{color:var(--acc)}
.sub{color:var(--dim);font-size:13px;margin-bottom:14px}
section{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin:12px 0}
h2{font-size:14px;text-transform:uppercase;letter-spacing:.08em;color:var(--acc);margin:0 0 10px}
label{display:block;margin:10px 0 4px;color:var(--dim);font-size:13px}
input,select,textarea{width:100%;background:#000;color:var(--text);border:1px solid #333;border-radius:9px;padding:9px;font:inherit}
textarea{min-height:120px;resize:vertical}
input[type=range]{padding:0}
input[type=checkbox]{width:auto;margin-right:8px;transform:scale(1.2)}
.chk{display:flex;align-items:center;margin:10px 0;color:var(--text)}
button{background:var(--acc);color:#000;border:0;border-radius:9px;padding:9px 14px;font:inherit;font-weight:600;cursor:pointer;margin:8px 6px 0 0}
button.sec{background:#222;color:var(--text)}
button.small{padding:5px 10px;font-size:13px;margin:0 4px 0 0}
.grid{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;font-size:14px}
.grid div:nth-child(odd){color:var(--dim)}
.snd{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:8px 0;border-top:1px solid var(--line);flex-wrap:wrap}
.snd b{font-weight:500;min-width:80px}.snd small{color:var(--dim)}
.toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:#222;padding:10px 16px;border-radius:10px;opacity:0;transition:.3s}
.toast.on{opacity:1}
</style></head><body><main>
<h1><span>claudioscar</span>-buddy</h1>
<div class="sub" id="sub">loading…</div>

<section><h2>Status</h2><div class="grid" id="st"></div></section>

<section><h2>Buddy</h2>
<label>Pet name</label><input id="pet" maxlength="23">
<label>Your name (owner)</label><input id="owner" maxlength="31">
<label>Species</label><select id="species"></select>
<label>Brightness <span id="bv"></span></label><input type="range" id="bright" min="0" max="4">
<label>Corner flag</label><select id="flag"><option value="0">none</option><option value="1">Palestine</option><option value="2">Italy</option></select>
<div class="chk"><input type="checkbox" id="hud">Show transcript</div>
<div class="chk"><input type="checkbox" id="led">Attention indicator</div>
<button onclick="save()">Save</button>
</section>

<section><h2>Characters</h2>
<div class="sub" id="cst"></div>
<div id="chars"></div>
</section>

<section><h2>Sound</h2>
<div class="chk"><input type="checkbox" id="sound">Sound on</div>
<label>Theme</label><select id="theme"><option value="0">classic beeps</option><option value="1">meme</option><option value="2">SD sound pack</option></select>
<label>SD sound pack</label><select id="pack"></select>
<label>Volume <span id="vv"></span></label><input type="range" id="volume" min="0" max="100">
<button onclick="save()">Save</button>
<label style="margin-top:14px">Custom sounds — upload a .wav (PCM) per event. Stored on the SD card when present.</label>
<div id="snds"></div>
</section>

<section><h2>Angry mode</h2>
<label>Outburst chance on deny / shake: <span id="av"></span>%</label><input type="range" id="angry" min="0" max="100">
<label>Phrases (one per line, ASCII)</label><textarea id="phrases"></textarea>
<button onclick="save()">Save</button><button class="sec" onclick="act({do:'test',ev:6})">Test</button>
</section>

<section><h2>WiFi</h2>
<div class="sub" id="wst"></div>
<label>Network</label><select id="nets"><option value="">— scan to list networks —</option></select>
<button class="sec" onclick="scan()">Scan</button>
<label>SSID</label><input id="ssid" maxlength="32">
<label>Password</label><input id="pass" type="password" maxlength="64">
<button onclick="wifi()">Connect</button><button class="sec" onclick="wifiForget()">Forget network</button>
</section>

<section><h2>Updates</h2>
<div class="sub" id="ost"></div>
<div class="chk"><input type="checkbox" id="otaAuto" onchange="post('/api/settings',{otaAuto:this.checked}).then(()=>toast('Saved'))">Install updates automatically</div>
<button class="sec" onclick="act({do:'ota_check'});setTimeout(load,4000)">Check now</button>
<button id="otaBtn" style="display:none" onclick="if(confirm('Install the update? The buddy will reboot.'))act({do:'ota_install'})">Install update</button>
</section>

<section><h2>Device</h2>
<button class="sec" onclick="syncTime()">Sync clock from this device</button>
<button class="sec" onclick="if(confirm('Clear Bluetooth pairing?'))act({do:'unpair'})">Clear Bluetooth pairing</button>
<button class="sec" onclick="if(confirm('Reboot?'))act({do:'reboot'})">Reboot</button>
</section>
<div class="sub">Pair with Claude Desktop: Developer → Open Hardware Buddy → Connect.</div>
</main><div class="toast" id="toast"></div>
<script>
const $=id=>document.getElementById(id);
const EV=['boot','prompt','approve','deny','celebrate','dizzy','angry'];
let S={};
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('on');setTimeout(()=>e.classList.remove('on'),1800)}
async function api(p,o){const r=await fetch(p,o);if(!r.ok)throw new Error(r.status);return r.json()}
const post=(p,b)=>api(p,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});
function fmtUp(s){const h=Math.floor(s/3600),m=Math.floor(s/60)%60;return h+'h '+m+'m'}
function render(){
  $('sub').textContent='firmware '+S.fw+' · '+S.board;
  const w=S.wifi,b=S.bat;
  const rows=[['Bluetooth',S.bt.linked?'linked to Claude':'waiting ('+S.bt.name+')'],
    ['Battery',b.pct+'% '+(b.mV/1000).toFixed(2)+'V'+(b.usb?(b.charging?' · charging':' · USB'):'')],
    ['WiFi',w.mode=='sta'?w.ssid+' ('+w.rssi+' dBm)':w.mode=='ap'?'hotspot '+w.ap:w.mode],
    ['IP',w.ip],['SD card',S.sd?'inserted':'none'],['Uptime',fmtUp(S.up)],
    ['Stats','approved '+S.stats.approved+' · denied '+S.stats.denied+' · level '+S.stats.level]];
  $('st').innerHTML=rows.map(r=>'<div>'+r[0]+'</div><div>'+esc(String(r[1]))+'</div>').join('');
  const o=S.ota;
  $('ost').textContent='Installed v'+o.current+(o.repo?' · releases from github.com/'+o.repo:'')+' · '+
    ({idle:'not checked yet (needs home WiFi)',uptodate:'up to date',available:'v'+o.latest+' available',installing:'installing… '+o.pct+'%',failed:'error: '+o.error}[o.state]||o.state);
  $('otaBtn').style.display=o.state=='available'?'':'none';
  $('wst').textContent=w.mode=='sta'?'Connected to '+w.ssid+' — this page is also at http://claudioscar-buddy.local':'Not on a network: running the hotspot.';
}
function esc(s){return s.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function fill(){
  $('pet').value=S.pet;$('owner').value=S.owner;
  $('species').innerHTML=(S.gif?'<option value="255">GIF character</option>':'')+S.speciesList.map((n,i)=>'<option value="'+i+'">'+n+'</option>').join('');
  $('species').value=S.species;$('bright').value=S.bright;$('flag').value=S.flag;
  $('hud').checked=S.hud;$('led').checked=S.led;$('sound').checked=S.sound;
  $('theme').value=S.theme;$('volume').value=S.volume;$('angry').value=S.angry;
  $('phrases').value=S.phrases.split('|').join('\n');$('ssid').value=S.wifi.ssid;$('otaAuto').checked=S.ota.auto;
  ['bright','volume','angry'].forEach(k=>{$(k).oninput=lbl;});lbl();
}
function lbl(){$('bv').textContent=$('bright').value+'/4';$('vv').textContent=$('volume').value;$('av').textContent=$('angry').value}
async function load(first){try{S=await api('/api/status');if(first)fill();render()}catch(e){$('sub').textContent='offline'}}
async function save(){
  const b={pet:$('pet').value,owner:$('owner').value,species:+$('species').value,bright:+$('bright').value,
    flag:+$('flag').value,hud:$('hud').checked,led:$('led').checked,sound:$('sound').checked,
    theme:+$('theme').value,pack:$('pack').value,volume:+$('volume').value,angry:+$('angry').value,
    phrases:$('phrases').value.split('\n').map(s=>s.trim()).filter(Boolean).join('|')};
  try{await post('/api/settings',b);toast('Saved');load()}catch(e){toast('Error: '+e.message)}
}
async function act(b){try{await post('/api/action',b);toast('Done')}catch(e){toast('Error')}}
function syncTime(){const d=new Date();act({do:'time',epoch:Math.floor(d/1000),tz:-d.getTimezoneOffset()*60})}
async function scan(){toast('Scanning…');try{const l=await api('/api/scan');
  $('nets').innerHTML='<option value="">— pick a network —</option>'+l.map(n=>'<option>'+esc(n.ssid)+'</option>').join('');
  $('nets').onchange=()=>{if($('nets').value)$('ssid').value=$('nets').value}}catch(e){toast('Scan failed')}}
async function wifi(){try{await post('/api/wifi',{ssid:$('ssid').value,pass:$('pass').value});
  toast('Connecting… the buddy will show its new IP');}catch(e){toast('Error')}}
async function wifiForget(){if(!confirm('Forget the saved network?'))return;await post('/api/wifi',{ssid:'',pass:''});toast('Forgotten')}
async function sounds(){let m={};try{m=await api('/api/sounds')}catch(e){}
  $('snds').innerHTML=EV.map((e,i)=>'<div class="snd"><b>'+e+'</b><small>'+(m[e]?'custom ('+m[e]+')':'built-in')+'</small><span>'+
   '<button class="small sec" onclick="act({do:\'test\',ev:'+i+'})">▶</button>'+
   '<button class="small sec" onclick="up(\''+e+'\')">upload</button>'+
   (m[e]?'<button class="small sec" onclick="del(\''+e+'\')">reset</button>':'')+'</span></div>').join('')}
function up(e){const i=document.createElement('input');i.type='file';i.accept='.wav,audio/wav';
  i.onchange=async()=>{const f=new FormData();f.append('file',i.files[0]);toast('Uploading…');
   try{await api('/api/sound?ev='+e,{method:'POST',body:f});toast('Uploaded');sounds()}catch(x){toast('Upload failed')}};i.click()}
async function del(e){try{await api('/api/sound?ev='+e+'&del=1',{method:'POST'});sounds()}catch(x){}}
async function lib(){let L={packs:[],chars:[]};try{L=await api('/api/library')}catch(e){}
  $('pack').innerHTML=L.packs.length?L.packs.map(n=>'<option>'+esc(n)+'</option>').join(''):'<option value="">(no packs on SD)</option>';
  $('pack').value=S.pack||'';
  $('cst').textContent='Installed: '+(S.char||'none')+(L.chars.length?' — tap one to install from the SD card (takes a few seconds).':' — no characters on the SD card.');
  $('chars').innerHTML=L.chars.map(n=>'<button class="sec" data-n="'+esc(n)+'" onclick="inst(this.dataset.n)">'+esc(n)+'</button>').join('')}
async function inst(n){toast('Installing '+n+'…');try{await post('/api/action',{do:'char',name:n});toast('Installed '+n);await load(true);lib()}catch(e){toast('Install failed')}}
load(true).then(lib);sounds();setInterval(load,5000);
</script></body></html>)HTML";
