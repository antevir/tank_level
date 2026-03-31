#pragma once

#include <WebServer.h>
#include "greenhouse.h"
#include "pump_state.h"

// Forward declarations — set by main.cpp each tick
extern volatile bool      g_button_pressed;
extern volatile PumpState g_pump_state;
extern volatile bool      g_tank_connected;

// ─── Common CSS (shared across pages) ──────────────────────────────────────
#define GH_CSS \
"*{box-sizing:border-box;margin:0;padding:0}" \
"body{font-family:Arial,sans-serif;max-width:960px;margin:0 auto;padding:10px;background:#1a1a2e;color:#e0e0e0}" \
"h1{color:#16c79a;margin:10px 0;font-size:1.5em}" \
"h2{color:#16c79a;margin:8px 0;font-size:1.2em}" \
"h3{color:#aaa;margin:6px 0;font-size:1em}" \
".card{background:#162447;border-radius:8px;padding:15px;margin:10px 0}" \
"input[type=number]{width:70px;background:#1b2838;color:#e0e0e0;border:1px solid #555;padding:4px;border-radius:4px}" \
"input[type=time]{background:#1b2838;color:#e0e0e0;border:1px solid #555;padding:4px;border-radius:4px}" \
"input[type=text]{background:#1b2838;color:#e0e0e0;border:1px solid #555;padding:4px;border-radius:4px}" \
"label{display:inline-block;margin:4px 2px}" \
".btn{background:#16c79a;color:#1a1a2e;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;margin:4px;font-weight:bold;text-decoration:none;display:inline-block}" \
".btn:hover{background:#1df0b0}" \
".btn-sm{padding:4px 10px;font-size:0.85em}" \
".btn-del{background:#c73e1d;color:#fff}" \
".btn-del:hover{background:#e8421e}" \
".btn-warn{background:#e6a117;color:#000}" \
".btn-sec{background:#555;color:#fff}" \
".btn-sec:hover{background:#777}" \
".ind{display:inline-block;padding:3px 10px;border-radius:4px;font-size:0.85em;font-weight:bold;margin:2px 4px}" \
".ind-on{background:#16c79a;color:#000}" \
".ind-off{background:#555;color:#ccc}" \
".ind-warn{background:#e6a117;color:#000}" \
".ind-err{background:#c73e1d;color:#fff}" \
".status-val{font-size:1.3em;font-weight:bold;color:#16c79a}" \
".row{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin:6px 0}" \
".ts{background:#1b2838;padding:8px;margin:4px 0;border-radius:4px;display:flex;flex-wrap:wrap;align-items:center;gap:6px}" \
".days{display:flex;gap:2px;flex-wrap:wrap}" \
".days label{background:#0d1b2a;padding:2px 6px;border-radius:3px;font-size:0.85em;cursor:pointer}" \
".days input{display:none}" \
".days input:checked+span{color:#16c79a;font-weight:bold}"

// ─── Page 1: Main Dashboard (read-only) ────────────────────────────────────
static const char GH_HTML_MAIN[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Greenhouse</title>
<style>
)rawliteral" GH_CSS R"rawliteral(
canvas{width:100%;background:#0d1b2a;border-radius:4px;margin-top:8px}
.bar-bg{background:#0d1b2a;border-radius:4px;overflow:hidden;height:16px;margin:4px 0}
.bar{height:16px;border-radius:4px;transition:width 0.5s}
.legend{display:flex;gap:12px;margin:4px 0;font-size:0.8em}
.legend i{display:inline-block;width:20px;height:3px;vertical-align:middle;margin-right:3px}
#connBanner{display:none;background:#c73e1d;color:#fff;padding:12px 16px;border-radius:8px;margin:8px 0;font-weight:bold;font-size:1.05em;text-align:center}
.hdr{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap}
</style></head><body>
<h1>&#127793; Greenhouse Controller</h1>
<div id="connBanner">&#9888; DISCONNECTED FROM PUMP SERVER</div>

<div class="card"><h2>System Status</h2>
<div class="row">
<span id="ntp" style="font-size:0.8em;color:#888">NTP: ?</span>
<span id="connInd" class="ind ind-off">Server: -</span>
<span id="btnInd" class="ind ind-off">Button: -</span>
<span id="pumpInd" class="ind ind-off">Pump: -</span>
</div></div>

<div class="card">
<div class="hdr"><h2>&#x1F319; Light Sensor &amp; Lamp</h2><a href="/light" class="btn btn-sm btn-sec">&#9881; Settings</a></div>
<div class="row">
<span>LDR: <span class="status-val" id="ldr">-</span>/1023</span>
<span id="lightInd" class="ind ind-off">Lamp: OFF</span>
<span id="darkInd" class="ind ind-off">Dark: No</span>
</div>
<div class="bar-bg"><div class="bar" id="ldrBar" style="width:0%;background:linear-gradient(90deg,#16c79a,#e2f76e)"></div></div>
<div class="legend"><span><i style="background:#16c79a"></i>LDR</span><span><i style="background:#ff6b6b"></i>Thresholds</span></div>
<canvas id="ldrChart" height="150"></canvas>
</div>

<div class="card">
<div class="hdr"><h2>&#x1F4A7; Soil Moisture &amp; Irrigation</h2><a href="/irrigation" class="btn btn-sm btn-sec">&#9881; Settings</a></div>
<div class="row">
<span>Moisture: <span class="status-val" id="moistPct">-</span>% (<span id="moistV">-</span> V)</span>
<span id="moistSensorInd" class="ind ind-off">Sensor: -</span>
<span id="valveInd" class="ind ind-off">Valve: OFF</span>
<span id="irrInd" class="ind ind-off">State: Idle</span>
<span id="cycleInd" style="font-size:0.8em;color:#888">Cycles: 0</span>
</div>
<div class="bar-bg"><div class="bar" id="moistBar" style="width:0%;background:linear-gradient(90deg,#0d47a1,#4fc3f7)"></div></div>
<div class="legend"><span><i style="background:#4fc3f7"></i>Moisture</span><span><i style="background:#e6a117"></i>Dry</span><span><i style="background:#16c79a"></i>Wet</span></div>
<canvas id="moistChart" height="150"></canvas>
</div>

<div class="card">
<div class="hdr"><h2>&#x1F50C; Nexa Smart Plugs</h2><a href="/nexa" class="btn btn-sm btn-sec">&#9881; Settings</a></div>
<div id="nexaBox"><span style="color:#888">No plugs configured</span></div>
</div>

<script>
var cfg={},ldrH=[],moistH=[],curLdr=0,curMPct=0;
function init(){fetchCfg();fetchHist();fetchSt();setInterval(fetchSt,2000);setInterval(fetchHist,60000);}
function adcPct(a,w,d){if(d<=w)return 50;var p=100-(a-w)*100/(d-w);return Math.max(0,Math.min(100,Math.round(p)));}

function fetchSt(){
fetch('/api/status').then(function(r){return r.json();}).then(function(d){
curLdr=d.ldr;curMPct=d.moist_ok?d.moist_pct:0;
document.getElementById('ldr').textContent=d.ldr;
document.getElementById('ldrBar').style.width=(d.ldr/1023*100)+'%';
document.getElementById('moistV').textContent=(d.moist/1023*3.3).toFixed(2);
var ms=document.getElementById('moistSensorInd');
if(d.moist_ok){ms.className='ind ind-on';ms.textContent='Sensor: OK';
document.getElementById('moistPct').textContent=d.moist_pct;
document.getElementById('moistBar').style.width=d.moist_pct+'%';
}else{ms.className='ind ind-err';ms.textContent='Sensor: DISCONNECTED';
document.getElementById('moistPct').textContent='--';document.getElementById('moistBar').style.width='0%';}
document.getElementById('ntp').textContent='NTP: '+(d.ntp?'synced':'waiting...');
var banner=document.getElementById('connBanner'),ci=document.getElementById('connInd');
if(d.conn){banner.style.display='none';ci.className='ind ind-on';ci.textContent='Server: Connected';}
else{banner.style.display='block';ci.className='ind ind-err';ci.textContent='Server: DISCONNECTED';}
var le=document.getElementById('lightInd');
if(d.light){le.className='ind ind-on';le.textContent='Lamp: ON';}else{le.className='ind ind-off';le.textContent='Lamp: OFF';}
var de=document.getElementById('darkInd');
if(d.dark){de.className='ind ind-on';de.textContent='Dark: Yes';}else{de.className='ind ind-off';de.textContent='Dark: No';}
var ve=document.getElementById('valveInd');
if(!d.moist_ok){ve.className='ind ind-err';ve.textContent='Valve: Fault';}
else if(d.valve){ve.className='ind ind-on';ve.textContent='Valve: ON';}
else{ve.className='ind ind-off';ve.textContent='Valve: OFF';}
var ie=document.getElementById('irrInd');
if(!d.moist_ok){ie.className='ind ind-err';ie.textContent='State: No Sensor';}
else{var iS=['Idle','Watering','Soaking','Paused'],iC=['ind ind-off','ind ind-on','ind ind-warn','ind ind-err'];
ie.className=iC[d.irr_st]||'ind ind-off';ie.textContent='State: '+(iS[d.irr_st]||'?');}
document.getElementById('cycleInd').textContent='Cycles: '+(d.irr_cc||0);
var be=document.getElementById('btnInd');
if(d.btn){be.className='ind ind-on';be.textContent='Button: PRESSED';}else{be.className='ind ind-off';be.textContent='Button: -';}
var pe=document.getElementById('pumpInd');
if(!d.conn){pe.className='ind ind-err';pe.textContent='Pump: Unknown';}
else{var pL={'-2':'DryRun','-1':'Off','0':'Idle','1':'Running','2':'Warning'},pC={'-2':'ind ind-err','-1':'ind ind-off','0':'ind ind-off','1':'ind ind-on','2':'ind ind-warn'};
var ps=String(d.pump);pe.className=pC[ps]||'ind ind-off';pe.textContent='Pump: '+(pL[ps]||'?');}
// Nexa plugs
var nx=d.nexa||[];var nb=document.getElementById('nexaBox');
if(nx.length==0){nb.innerHTML='<span style="color:#888">No plugs configured</span>';}
else{var h='';for(var i=0;i<nx.length;i++){
var cls=nx[i].ok?(nx[i].on?'ind ind-on':'ind ind-off'):'ind ind-err';
var txt=nx[i].name+': '+(nx[i].ok?(nx[i].on?'ON':'OFF'):'Unreachable');
h+='<div class="row"><span class="'+cls+'">'+txt+'</span>';
if(nx[i].ok){h+='<button class="btn btn-sm '+(nx[i].on?'btn-del':'')+
'" onclick="nexaToggle('+i+','+(nx[i].on?0:1)+')">'+(nx[i].on?'Turn OFF':'Turn ON')+'</button>';}
h+='</div>';}
nb.innerHTML=h;}
}).catch(function(){document.getElementById('connBanner').style.display='block';
document.getElementById('connInd').className='ind ind-err';document.getElementById('connInd').textContent='Server: DISCONNECTED';});}

function nexaToggle(idx,on){fetch('/api/nexa/toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'idx='+idx+'&on='+on})
.then(function(){setTimeout(fetchSt,500);}).catch(function(){});}

function fetchCfg(){fetch('/api/config').then(function(r){return r.json();}).then(function(d){cfg=d;}).catch(function(){});}
function fetchHist(){fetch('/api/history').then(function(r){return r.json();}).then(function(d){ldrH=d.ldr||[];moistH=d.moist||[];drawCharts();}).catch(function(){});}

function drawCharts(){
drawChart('ldrChart',ldrH,curLdr,1023,'#16c79a',
[{v:cfg.light?cfg.light.twi_on:300,c:'#ff6b6b',d:[5,3]},{v:cfg.light?cfg.light.twi_off:400,c:'#ff6b6b',d:[2,3]}]);
var cW=100,cD=(cfg.irr&&cfg.irr.cal_dry!=null)?cfg.irr.cal_dry:775;
var mP=moistH.map(function(v){return adcPct(v,cW,cD);});
drawChart('moistChart',mP,curMPct,100,'#4fc3f7',
[{v:(cfg.irr&&cfg.irr.dry_pct!=null)?cfg.irr.dry_pct:30,c:'#e6a117',d:[5,3]},
 {v:(cfg.irr&&cfg.irr.wet_pct!=null)?cfg.irr.wet_pct:60,c:'#16c79a',d:[2,3]}]);}

function drawChart(id,hist,cur,yMax,lc,th){
var c=document.getElementById(id);if(!c)return;var ctx=c.getContext('2d');
var dpr=window.devicePixelRatio||1,rect=c.getBoundingClientRect();
c.width=rect.width*dpr;c.height=150*dpr;ctx.scale(dpr,dpr);
var W=rect.width,H=150;ctx.clearRect(0,0,W,H);ctx.fillStyle='#0d1b2a';ctx.fillRect(0,0,W,H);
ctx.strokeStyle='#222';ctx.lineWidth=0.5;
for(var i=0;i<=4;i++){var y=H*i/4;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(W,y);ctx.stroke();
ctx.fillStyle='#555';ctx.font='10px Arial';ctx.fillText(Math.round(yMax*(1-i/4)),2,y+10);}
var n=hist.length;ctx.fillStyle='#555';
for(var i=0;i<=6;i++){var x=W*i/6;var m=Math.round(n-n*i/6);
var l;if(m>=120)l='-'+Math.round(m/60)+'h';else if(m>0)l='-'+m+'m';else l='now';
ctx.fillText(l,i<6?x+2:x-22,H-3);}
for(var ti=0;ti<th.length;ti++){var t=th[ti];ctx.strokeStyle=t.c;ctx.lineWidth=1;ctx.setLineDash(t.d||[4,3]);
var ty=H-(t.v/yMax*H);ctx.beginPath();ctx.moveTo(0,ty);ctx.lineTo(W,ty);ctx.stroke();ctx.setLineDash([]);}
if(n>0){ctx.strokeStyle=lc;ctx.lineWidth=1.5;ctx.beginPath();
for(var i=0;i<n;i++){var x=n>1?i/(n-1)*W:W/2;var y=H-(hist[i]/yMax*H);
if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);}
ctx.stroke();var cy=H-(cur/yMax*H);ctx.beginPath();ctx.arc(W-2,cy,3,0,Math.PI*2);ctx.fillStyle=lc;ctx.fill();}}

window.addEventListener('resize',drawCharts);init();
</script></body></html>)rawliteral";

// ─── Page 2: Night Light Settings ──────────────────────────────────────────
static const char GH_HTML_LIGHT[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Night Light Settings</title>
<style>
)rawliteral" GH_CSS R"rawliteral(
</style></head><body>
<a href="/" class="btn btn-sm btn-sec" style="margin-bottom:8px">&#8592; Dashboard</a>
<h1>&#x1F319; Night Light Settings</h1>
<p style="color:#888;font-size:0.85em;margin:4px 0">LDR thresholds are shared with Nexa smart plugs.</p>

<div class="card"><h2>Twilight Thresholds</h2>
<div class="row">
<label>Night (on below): <input type="number" min="0" max="1023" id="twi_on"></label>
<label>Day (off above): <input type="number" min="0" max="1023" id="twi_off"></label>
</div>
</div>

<div class="card"><h2>Time Schedules</h2>
<div id="tsC"></div>
<button class="btn btn-sm" onclick="addTS()">+ Add Schedule</button>
</div>

<div style="margin:15px 0">
<button class="btn" onclick="save()">&#x1F4BE; Save</button>
<span id="msg" style="color:#16c79a;margin-left:10px"></span>
</div>

<script>
var cfg={},dn=['Mon','Tue','Wed','Thu','Fri','Sat','Sun'];
function pad(n){return n<10?'0'+n:''+n;}
function init(){fetch('/api/config').then(function(r){return r.json();}).then(function(d){cfg=d;render();}).catch(function(){});}

function render(){
var l=cfg.light||{};
document.getElementById('twi_on').value=l.twi_on||300;
document.getElementById('twi_off').value=l.twi_off||400;
renderTS(l.ts||[]);
}

function renderTS(ts){
if(!cfg.light)cfg.light={};cfg.light.ts=ts;
var h='';for(var t=0;t<ts.length;t++){var s=ts[t];
h+='<div class="ts"><input type="time" value="'+pad(s.sh)+':'+pad(s.sm)+'" id="s_'+t+'">';
h+=' &rarr; <input type="time" value="'+pad(s.eh)+':'+pad(s.em)+'" id="e_'+t+'">';
h+=' <div class="days">';
for(var d=0;d<7;d++){var ck=((s.days>>d)&1)?'checked':'';
h+='<label><input type="checkbox" '+ck+' id="d_'+t+'_'+d+'"><span>'+dn[d]+'</span></label>';}
h+='</div><label><input type="checkbox" '+(s.en?'checked':'')+' id="en_'+t+'"> On</label>';
h+='<button class="btn btn-sm btn-del" onclick="delTS('+t+')">X</button></div>';}
document.getElementById('tsC').innerHTML=h;
}

function gather(){
if(!cfg.light)cfg.light={};
cfg.light.twi_on=parseInt(document.getElementById('twi_on').value)||0;
cfg.light.twi_off=parseInt(document.getElementById('twi_off').value)||0;
var ts=cfg.light.ts||[];
for(var t=0;t<ts.length;t++){
var sv=document.getElementById('s_'+t).value.split(':'),ev=document.getElementById('e_'+t).value.split(':');
ts[t].sh=parseInt(sv[0])||0;ts[t].sm=parseInt(sv[1])||0;
ts[t].eh=parseInt(ev[0])||0;ts[t].em=parseInt(ev[1])||0;
var days=0;for(var d=0;d<7;d++){if(document.getElementById('d_'+t+'_'+d).checked)days|=(1<<d);}
ts[t].days=days;ts[t].en=document.getElementById('en_'+t).checked?1:0;}
}

function addTS(){gather();var ts=cfg.light.ts||[];if(ts.length>=4){alert('Max 4 schedules');return;}
ts.push({sh:21,sm:0,eh:7,em:0,days:127,en:1});renderTS(ts);}
function delTS(i){gather();cfg.light.ts.splice(i,1);renderTS(cfg.light.ts);}

function save(){
gather();
var b='twi_on='+cfg.light.twi_on+'&twi_off='+cfg.light.twi_off;
var ts=cfg.light.ts||[];b+='&tsc='+ts.length;
for(var t=0;t<ts.length;t++){
b+='&t'+t+'_sh='+ts[t].sh+'&t'+t+'_sm='+ts[t].sm+'&t'+t+'_eh='+ts[t].eh+'&t'+t+'_em='+ts[t].em+'&t'+t+'_d='+ts[t].days+'&t'+t+'_en='+ts[t].en;}
fetch('/api/light',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b})
.then(function(r){return r.json();}).then(function(d){
document.getElementById('msg').textContent=d.ok?'Saved!':'Error';
setTimeout(function(){document.getElementById('msg').textContent='';},3000);
}).catch(function(){document.getElementById('msg').textContent='Error';});}

init();
</script></body></html>)rawliteral";

// ─── Page 3: Irrigation Settings ───────────────────────────────────────────
static const char GH_HTML_IRR[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Irrigation Settings</title>
<style>
)rawliteral" GH_CSS R"rawliteral(
</style></head><body>
<a href="/" class="btn btn-sm btn-sec" style="margin-bottom:8px">&#8592; Dashboard</a>
<h1>&#x1F4A7; Irrigation Settings</h1>

<div class="card"><h2>Dry Calibration</h2>
<div class="row">
<span style="font-size:0.85em">Wet (100%): fixed at ADC &le; 100</span>
<span style="font-size:0.85em">Dry (0%): <span class="status-val" id="calDry">-</span> ADC</span>
<button class="btn btn-sm btn-warn" onclick="calibrate()">&#x1F3DC; Calibrate Dry (in air)</button>
<span id="calMsg" style="color:#16c79a;font-size:0.85em"></span>
</div></div>

<div class="card"><h2>Parameters</h2>
<div class="row"><label><input type="checkbox" id="irrEn"> Irrigation Enabled</label></div>
<div class="row">
<label>Dry threshold: <input type="number" min="0" max="100" id="dryTh">%</label>
<label>Wet threshold: <input type="number" min="0" max="100" id="wetTh">%</label>
</div>
<div class="row">
<label>Water ON: <input type="number" min="1" max="180" id="irrOn"> min</label>
<label>Soak OFF: <input type="number" min="1" max="480" id="irrOff"> min</label>
<label>Max cycles: <input type="number" min="1" max="50" id="maxCyc"></label>
</div></div>

<div style="margin:15px 0">
<button class="btn" onclick="save()">&#x1F4BE; Save</button>
<span id="msg" style="color:#16c79a;margin-left:10px"></span>
</div>

<script>
var cfg={};
function init(){fetch('/api/config').then(function(r){return r.json();}).then(function(d){cfg=d;render();}).catch(function(){});}

function render(){
var ir=cfg.irr||{};
document.getElementById('irrEn').checked=!!ir.en;
document.getElementById('dryTh').value=ir.dry_pct!=null?ir.dry_pct:30;
document.getElementById('wetTh').value=ir.wet_pct!=null?ir.wet_pct:60;
document.getElementById('irrOn').value=ir.on_m||3;
document.getElementById('irrOff').value=ir.off_m||20;
document.getElementById('maxCyc').value=ir.max_c||6;
document.getElementById('calDry').textContent=ir.cal_dry||775;
}

function calibrate(){
fetch('/api/calibrate_dry',{method:'POST'}).then(function(r){return r.json();}).then(function(d){
document.getElementById('calDry').textContent=d.val;
document.getElementById('calMsg').textContent='Calibrated dry: '+d.val+' ADC';
setTimeout(function(){document.getElementById('calMsg').textContent='';},4000);
}).catch(function(){});}

function save(){
var b='irr_en='+(document.getElementById('irrEn').checked?1:0);
b+='&irr_dry_pct='+(parseInt(document.getElementById('dryTh').value)||0);
b+='&irr_wet_pct='+(parseInt(document.getElementById('wetTh').value)||0);
b+='&irr_on_m='+(parseInt(document.getElementById('irrOn').value)||0);
b+='&irr_off_m='+(parseInt(document.getElementById('irrOff').value)||0);
b+='&irr_max='+(parseInt(document.getElementById('maxCyc').value)||0);
fetch('/api/irrigation',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b})
.then(function(r){return r.json();}).then(function(d){
document.getElementById('msg').textContent=d.ok?'Saved!':'Error';
setTimeout(function(){document.getElementById('msg').textContent='';},3000);
}).catch(function(){document.getElementById('msg').textContent='Error';});}

init();
</script></body></html>)rawliteral";

// ─── Page 4: Nexa Smart Plugs Settings ────────────────────────────────────
static const char GH_HTML_NEXA[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Nexa Smart Plugs</title>
<style>
)rawliteral" GH_CSS R"rawliteral(
.plug{background:#1b2838;padding:10px;margin:8px 0;border-radius:6px}
.plug h3{color:#e0e0e0}
.found-row{background:#1b2838;padding:6px 10px;border-radius:4px;margin:4px 0}
</style></head><body>
<a href="/" class="btn btn-sm btn-sec" style="margin-bottom:8px">&#8592; Dashboard</a>
<h1>&#x1F50C; Nexa Smart Plugs</h1>
<p style="color:#888;font-size:0.85em;margin:4px 0">Up to 4 WiFi smart plugs (Nexa WPO-01). Controlled by the same LDR sensor &mdash; thresholds set in <a href="/light" style="color:#16c79a">Night Light Settings</a>.</p>

<div class="card"><h2>Discover Devices</h2>
<p style="color:#888;font-size:0.85em">Scan the local network for Nexa smart plugs via mDNS.</p>
<button class="btn" onclick="scan()" id="scanBtn">&#x1F50D; Discover</button>
<span id="scanMsg" style="color:#888;margin-left:10px"></span>
<div id="found"></div></div>

<h2 style="margin-top:12px">Configured Plugs</h2>
<div id="plugs"><span style="color:#888">Loading...</span></div>

<div style="margin:15px 0">
<button class="btn" onclick="save()">&#x1F4BE; Save</button>
<span id="msg" style="color:#16c79a;margin-left:10px"></span>
</div>

<script>
var cfg={},dn=['Mon','Tue','Wed','Thu','Fri','Sat','Sun'],foundDevices=[];
function pad(n){return n<10?'0'+n:''+n;}
function init(){fetch('/api/config').then(function(r){return r.json();}).then(function(d){cfg=d;render();}).catch(function(){});}

function render(){
var nx=cfg.nexa||{};var plugs=nx.plugs||[];var h='';
for(var p=0;p<plugs.length;p++){
var pl=plugs[p];
h+='<div class="plug"><div class="row" style="justify-content:space-between"><h3>'+(pl.name||pl.host||('Plug '+(p+1)))+'</h3>';
h+='<button class="btn btn-sm btn-del" onclick="delPlug('+p+')">Remove</button></div>';
h+='<div class="row">';
h+='<span style="font-size:0.85em;color:#888">Host: '+(pl.host||'?')+'</span>';
h+='<label>Name: <input type="text" maxlength="15" id="pn_'+p+'" value="'+(pl.name||'')+'"></label>';
h+='<label><input type="checkbox" id="pen_'+p+'" '+(pl.en?'checked':'')+' > Enabled</label>';
h+='</div>';
h+='<h3 style="margin-top:6px">Schedules</h3><div id="pts_'+p+'">';
var ts=pl.ts||[];
for(var t=0;t<ts.length;t++){h+=tsHTML(p,t,ts[t]);}
h+='</div><button class="btn btn-sm" onclick="addTS('+p+')">+ Schedule</button>';
h+='</div>';}
if(plugs.length==0) h='<div class="card"><span style="color:#888">No plugs configured. Use Discover to find devices.</span></div>';
document.getElementById('plugs').innerHTML=h;
}

function tsHTML(p,t,s){
var h='<div class="ts"><input type="time" value="'+pad(s.sh)+':'+pad(s.sm)+'" id="ps_'+p+'_'+t+'">';
h+=' &rarr; <input type="time" value="'+pad(s.eh)+':'+pad(s.em)+'" id="pe_'+p+'_'+t+'">';
h+=' <div class="days">';
for(var d=0;d<7;d++){var ck=((s.days>>d)&1)?'checked':'';
h+='<label><input type="checkbox" '+ck+' id="pd_'+p+'_'+t+'_'+d+'"><span>'+dn[d]+'</span></label>';}
h+='</div><label><input type="checkbox" '+(s.en?'checked':'')+' id="pte_'+p+'_'+t+'"> On</label>';
h+='<button class="btn btn-sm btn-del" onclick="delTS('+p+','+t+')">X</button></div>';
return h;
}

function scan(){
document.getElementById('scanBtn').disabled=true;
document.getElementById('scanMsg').textContent='Scanning...';
document.getElementById('found').innerHTML='';
fetch('/api/nexa/discover').then(function(r){return r.json();}).then(function(devs){
document.getElementById('scanBtn').disabled=false;
foundDevices=devs;
document.getElementById('scanMsg').textContent='Found '+devs.length+' device(s)';
renderFound();
}).catch(function(){
document.getElementById('scanBtn').disabled=false;
document.getElementById('scanMsg').textContent='Scan failed';});}

function renderFound(){
var devs=foundDevices;
if(devs.length==0){document.getElementById('found').innerHTML='<p style="color:#888;margin:8px 0">No devices found. Make sure plugs are powered on and on the same network.</p>';return;}
var plugs=(cfg.nexa&&cfg.nexa.plugs)||[];
var existing={};for(var p=0;p<plugs.length;p++){if(plugs[p].host)existing[plugs[p].host.toLowerCase()]=true;}
var h='<div style="margin-top:8px">';
for(var i=0;i<devs.length;i++){
var d=devs[i];var added=!!existing[d.host.toLowerCase()];
h+='<div class="row found-row">';
h+='<span style="flex:1">'+d.host+' <span style="color:#555">('+d.ip+')</span></span>';
if(added){h+='<span class="ind ind-on">Added</span>';}
else if(plugs.length>=4){h+='<span style="color:#888">Max 4 reached</span>';}
else{h+='<button class="btn btn-sm" onclick="addFromScan(\''+d.host+'\')">Add</button>';}
h+='</div>';}
h+='</div>';
document.getElementById('found').innerHTML=h;
}

function addFromScan(host){
gather();
var plugs=(cfg.nexa&&cfg.nexa.plugs)||[];
if(plugs.length>=4){alert('Max 4 plugs');return;}
plugs.push({host:host,name:host,en:1,ts:[{sh:21,sm:0,eh:7,em:0,days:127,en:1}]});
if(!cfg.nexa)cfg.nexa={};
cfg.nexa.plugs=plugs;cfg.nexa.n=plugs.length;
render();renderFound();
}

function gather(){
var nx=cfg.nexa||{};var plugs=nx.plugs||[];
for(var p=0;p<plugs.length;p++){
var el=document.getElementById('pn_'+p);if(!el)continue;
plugs[p].name=document.getElementById('pn_'+p).value;
plugs[p].en=document.getElementById('pen_'+p).checked?1:0;
var ts=plugs[p].ts||[];
for(var t=0;t<ts.length;t++){
var sv=document.getElementById('ps_'+p+'_'+t).value.split(':'),ev=document.getElementById('pe_'+p+'_'+t).value.split(':');
ts[t].sh=parseInt(sv[0])||0;ts[t].sm=parseInt(sv[1])||0;
ts[t].eh=parseInt(ev[0])||0;ts[t].em=parseInt(ev[1])||0;
var days=0;for(var d=0;d<7;d++){if(document.getElementById('pd_'+p+'_'+t+'_'+d).checked)days|=(1<<d);}
ts[t].days=days;ts[t].en=document.getElementById('pte_'+p+'_'+t).checked?1:0;}
}
}

function delPlug(i){gather();cfg.nexa.plugs.splice(i,1);cfg.nexa.n=cfg.nexa.plugs.length;render();renderFound();}

function addTS(p){gather();var ts=cfg.nexa.plugs[p].ts||[];
if(ts.length>=4){alert('Max 4 schedules');return;}
ts.push({sh:21,sm:0,eh:7,em:0,days:127,en:1});cfg.nexa.plugs[p].ts=ts;render();}

function delTS(p,t){gather();cfg.nexa.plugs[p].ts.splice(t,1);render();}

function save(){
gather();var plugs=cfg.nexa?cfg.nexa.plugs:[];
var b='n='+plugs.length;
for(var p=0;p<plugs.length;p++){
var pp='p'+p;b+='&'+pp+'_host='+encodeURIComponent(plugs[p].host||'');
b+='&'+pp+'_name='+encodeURIComponent(plugs[p].name||'');
b+='&'+pp+'_en='+(plugs[p].en?1:0);
var ts=plugs[p].ts||[];b+='&'+pp+'_tsc='+ts.length;
for(var t=0;t<ts.length;t++){
b+='&'+pp+'_t'+t+'_sh='+ts[t].sh+'&'+pp+'_t'+t+'_sm='+ts[t].sm;
b+='&'+pp+'_t'+t+'_eh='+ts[t].eh+'&'+pp+'_t'+t+'_em='+ts[t].em;
b+='&'+pp+'_t'+t+'_d='+ts[t].days+'&'+pp+'_t'+t+'_en='+ts[t].en;}}
fetch('/api/nexa',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b})
.then(function(r){return r.json();}).then(function(d){
document.getElementById('msg').textContent=d.ok?'Saved!':'Error';
setTimeout(function(){document.getElementById('msg').textContent='';},3000);
}).catch(function(){document.getElementById('msg').textContent='Error';});}

init();
</script></body></html>)rawliteral";

// ─── Server class ──────────────────────────────────────────────────────────

class GreenhouseServer {
public:
    GreenhouseServer() : m_server(80) {}

    void begin(GreenhouseCtrl* ctrl)
    {
        m_gh = ctrl;

        // Pages
        m_server.on("/",           HTTP_GET,  [this]() { m_server.send_P(200, PSTR("text/html"), GH_HTML_MAIN); });
        m_server.on("/light",      HTTP_GET,  [this]() { m_server.send_P(200, PSTR("text/html"), GH_HTML_LIGHT); });
        m_server.on("/irrigation", HTTP_GET,  [this]() { m_server.send_P(200, PSTR("text/html"), GH_HTML_IRR); });
        m_server.on("/nexa",       HTTP_GET,  [this]() { m_server.send_P(200, PSTR("text/html"), GH_HTML_NEXA); });

        // API
        m_server.on("/api/status",        HTTP_GET,  [this]() { handleStatus(); });
        m_server.on("/api/config",        HTTP_GET,  [this]() { handleGetConfig(); });
        m_server.on("/api/light",         HTTP_POST, [this]() { handlePostLight(); });
        m_server.on("/api/irrigation",    HTTP_POST, [this]() { handlePostIrrigation(); });
        m_server.on("/api/nexa",          HTTP_POST, [this]() { handlePostNexa(); });
        m_server.on("/api/nexa/discover", HTTP_GET,  [this]() { handleDiscover(); });
        m_server.on("/api/nexa/toggle",   HTTP_POST, [this]() { handleToggle(); });
        m_server.on("/api/calibrate_dry", HTTP_POST, [this]() { handleCalibrateDry(); });
        m_server.on("/api/history",       HTTP_GET,  [this]() { handleHistory(); });

        m_server.begin();
        Log.info("[GH-SRV] Web server started on port 80");
    }

    void handle()
    {
        m_server.handleClient();
    }

private:
    WebServer       m_server;
    GreenhouseCtrl* m_gh = nullptr;

    // ── Status (extended with Nexa) ────────────────────────────────────────
    void handleStatus()
    {
        const NexaCfg& nx = m_gh->config.data.nexa;

        String j = "{";
        j += "\"ldr\":"        + String(m_gh->ldr_reading);
        j += ",\"moist\":"     + String(m_gh->moisture_reading);
        j += ",\"moist_pct\":" + String(m_gh->moisture_pct);
        j += ",\"moist_ok\":"  + String(m_gh->moisture_sensor_ok ? "true" : "false");
        j += ",\"ntp\":"       + String(m_gh->isTimeSynced() ? "true" : "false");
        j += ",\"light\":"     + String(m_gh->light_on  ? "true" : "false");
        j += ",\"dark\":"      + String(m_gh->is_dark   ? "true" : "false");
        j += ",\"valve\":"     + String(m_gh->valve_on()  ? "true" : "false");
        j += ",\"irr_st\":"    + String((int)m_gh->irr_state());
        j += ",\"irr_cc\":"    + String(m_gh->irr_cycle_count());
        j += ",\"btn\":"       + String(g_button_pressed ? "true" : "false");
        j += ",\"conn\":"      + String(g_tank_connected  ? "true" : "false");
        j += ",\"pump\":"      + String((int)g_pump_state);

        // Nexa plug live status
        j += ",\"nexa\":[";
        for (int i = 0; i < nx.num_plugs && i < MAX_NEXA_PLUGS; i++)
        {
            if (i > 0) j += ",";
            j += "{\"name\":\"";
            j += nx.plugs[i].name;
            j += "\",\"on\":";
            j += m_gh->nexa.plug_on[i] ? "true" : "false";
            j += ",\"ok\":";
            j += m_gh->nexa.plug_reachable[i] ? "true" : "false";
            j += "}";
        }
        j += "]}";
        m_server.send(200, "application/json", j);
    }

    // ── Full config (light + irrigation + nexa) ────────────────────────────
    void handleGetConfig()
    {
        const LightCfg&      l  = m_gh->config.data.light;
        const IrrigationCfg& ir = m_gh->config.data.irrigation;
        const NexaCfg&       nx = m_gh->config.data.nexa;

        String j = "{\"light\":{";
        j += "\"twi_on\":"   + String(l.twilight_on);
        j += ",\"twi_off\":" + String(l.twilight_off);
        j += ",\"ts\":[";
        for (int t = 0; t < l.num_time_spans && t < MAX_TIME_SPANS; t++)
        {
            if (t > 0) j += ",";
            const TimeSpanCfg& ts = l.time_spans[t];
            j += "{\"sh\":"   + String(ts.start_hour);
            j += ",\"sm\":"   + String(ts.start_minute);
            j += ",\"eh\":"   + String(ts.end_hour);
            j += ",\"em\":"   + String(ts.end_minute);
            j += ",\"days\":" + String(ts.weekdays);
            j += ",\"en\":"   + String(ts.enabled) + "}";
        }
        j += "]},\"irr\":{";
        j += "\"en\":"        + String(ir.enabled);
        j += ",\"cal_dry\":"  + String(ir.moisture_cal_dry);
        j += ",\"dry_pct\":"  + String(ir.dry_threshold_pct);
        j += ",\"wet_pct\":"  + String(ir.wet_threshold_pct);
        j += ",\"on_m\":"     + String(ir.irrigate_on_min);
        j += ",\"off_m\":"    + String(ir.irrigate_off_min);
        j += ",\"max_c\":"    + String(ir.max_cycles);

        // Nexa config
        j += "},\"nexa\":{\"n\":" + String(nx.num_plugs) + ",\"plugs\":[";
        for (int i = 0; i < nx.num_plugs && i < MAX_NEXA_PLUGS; i++)
        {
            if (i > 0) j += ",";
            const NexaPlugCfg& p = nx.plugs[i];
            j += "{\"host\":\"" + String(p.hostname) + "\"";
            j += ",\"name\":\"" + String(p.name) + "\"";
            j += ",\"en\":" + String(p.enabled);
            j += ",\"ts\":[";
            for (int t = 0; t < p.num_time_spans && t < MAX_TIME_SPANS; t++)
            {
                if (t > 0) j += ",";
                const TimeSpanCfg& ts = p.time_spans[t];
                j += "{\"sh\":"   + String(ts.start_hour);
                j += ",\"sm\":"   + String(ts.start_minute);
                j += ",\"eh\":"   + String(ts.end_hour);
                j += ",\"em\":"   + String(ts.end_minute);
                j += ",\"days\":" + String(ts.weekdays);
                j += ",\"en\":"   + String(ts.enabled) + "}";
            }
            j += "]}";
        }
        j += "]}}";
        m_server.send(200, "application/json", j);
    }

    // ── Save light config ──────────────────────────────────────────────────
    void handlePostLight()
    {
        LightCfg& l = m_gh->config.data.light;

        if (m_server.hasArg("twi_on"))
            l.twilight_on  = constrain(m_server.arg("twi_on").toInt(),  0, 1023);
        if (m_server.hasArg("twi_off"))
            l.twilight_off = constrain(m_server.arg("twi_off").toInt(), 0, 1023);
        if (l.twilight_off <= l.twilight_on)
            l.twilight_off = l.twilight_on + 1;

        if (m_server.hasArg("tsc"))
        {
            int n = constrain(m_server.arg("tsc").toInt(), 0, MAX_TIME_SPANS);
            l.num_time_spans = n;
            for (int t = 0; t < n; t++)
            {
                String tp = "t" + String(t);
                TimeSpanCfg& ts = l.time_spans[t];
                if (m_server.hasArg(tp + "_sh"))  ts.start_hour   = constrain(m_server.arg(tp + "_sh").toInt(),  0, 23);
                if (m_server.hasArg(tp + "_sm"))  ts.start_minute = constrain(m_server.arg(tp + "_sm").toInt(),  0, 59);
                if (m_server.hasArg(tp + "_eh"))  ts.end_hour     = constrain(m_server.arg(tp + "_eh").toInt(),  0, 23);
                if (m_server.hasArg(tp + "_em"))  ts.end_minute   = constrain(m_server.arg(tp + "_em").toInt(),  0, 59);
                if (m_server.hasArg(tp + "_d"))   ts.weekdays     = m_server.arg(tp + "_d").toInt() & 0x7F;
                if (m_server.hasArg(tp + "_en"))  ts.enabled      = m_server.arg(tp + "_en").toInt() ? 1 : 0;
            }
        }

        m_gh->config.save();
        Log.info("[GH-SRV] Light config saved");
        m_server.send(200, "application/json", "{\"ok\":true}");
    }

    // ── Save irrigation config ─────────────────────────────────────────────
    void handlePostIrrigation()
    {
        IrrigationCfg& ir = m_gh->config.data.irrigation;

        if (m_server.hasArg("irr_en"))
            ir.enabled = m_server.arg("irr_en").toInt() ? 1 : 0;
        if (m_server.hasArg("irr_dry_pct"))
            ir.dry_threshold_pct = constrain(m_server.arg("irr_dry_pct").toInt(), 0, 100);
        if (m_server.hasArg("irr_wet_pct"))
            ir.wet_threshold_pct = constrain(m_server.arg("irr_wet_pct").toInt(), 0, 100);
        if (m_server.hasArg("irr_on_m"))
            ir.irrigate_on_min  = constrain(m_server.arg("irr_on_m").toInt(),  1, 180);
        if (m_server.hasArg("irr_off_m"))
            ir.irrigate_off_min = constrain(m_server.arg("irr_off_m").toInt(), 1, 480);
        if (m_server.hasArg("irr_max"))
            ir.max_cycles = constrain(m_server.arg("irr_max").toInt(), 1, 50);

        m_gh->config.save();
        Log.info("[GH-SRV] Irrigation config saved");
        m_server.send(200, "application/json", "{\"ok\":true}");
    }

    // ── Save nexa config ───────────────────────────────────────────────────
    void handlePostNexa()
    {
        NexaCfg& nx = m_gh->config.data.nexa;

        if (m_server.hasArg("n"))
        {
            int n = constrain(m_server.arg("n").toInt(), 0, MAX_NEXA_PLUGS);
            nx.num_plugs = n;

            for (int i = 0; i < n; i++)
            {
                String pp = "p" + String(i);
                NexaPlugCfg& p = nx.plugs[i];

                if (m_server.hasArg(pp + "_host"))
                {
                    String host = m_server.arg(pp + "_host");
                    memset(p.hostname, 0, NEXA_HOST_LEN);
                    host.toCharArray(p.hostname, NEXA_HOST_LEN);
                }
                if (m_server.hasArg(pp + "_name"))
                {
                    String name = m_server.arg(pp + "_name");
                    memset(p.name, 0, NEXA_NAME_LEN);
                    name.toCharArray(p.name, NEXA_NAME_LEN);
                }
                if (m_server.hasArg(pp + "_en"))
                    p.enabled = m_server.arg(pp + "_en").toInt() ? 1 : 0;

                if (m_server.hasArg(pp + "_tsc"))
                {
                    int tsc = constrain(m_server.arg(pp + "_tsc").toInt(), 0, MAX_TIME_SPANS);
                    p.num_time_spans = tsc;
                    for (int t = 0; t < tsc; t++)
                    {
                        String tp = pp + "_t" + String(t);
                        TimeSpanCfg& ts = p.time_spans[t];
                        if (m_server.hasArg(tp + "_sh"))  ts.start_hour   = constrain(m_server.arg(tp + "_sh").toInt(),  0, 23);
                        if (m_server.hasArg(tp + "_sm"))  ts.start_minute = constrain(m_server.arg(tp + "_sm").toInt(),  0, 59);
                        if (m_server.hasArg(tp + "_eh"))  ts.end_hour     = constrain(m_server.arg(tp + "_eh").toInt(),  0, 23);
                        if (m_server.hasArg(tp + "_em"))  ts.end_minute   = constrain(m_server.arg(tp + "_em").toInt(),  0, 59);
                        if (m_server.hasArg(tp + "_d"))   ts.weekdays     = m_server.arg(tp + "_d").toInt() & 0x7F;
                        if (m_server.hasArg(tp + "_en"))  ts.enabled      = m_server.arg(tp + "_en").toInt() ? 1 : 0;
                    }
                }
            }
        }

        m_gh->config.save();
        Log.info("[GH-SRV] Nexa config saved");
        m_server.send(200, "application/json", "{\"ok\":true}");
    }

    // ── Discover Nexa plugs via mDNS ───────────────────────────────────────
    void handleDiscover()
    {
        NexaDiscoveredPlug found[8];
        int n = NexaController::discover(found, 8);
        String j = "[";
        for (int i = 0; i < n; i++)
        {
            if (i > 0) j += ",";
            j += "{\"host\":\"" + String(found[i].hostname) + "\"";
            j += ",\"ip\":\"" + found[i].ip.toString() + "\"}";
        }
        j += "]";
        m_server.send(200, "application/json", j);
    }

    // ── Manual toggle a Nexa plug from dashboard ───────────────────────────
    void handleToggle()
    {
        int idx = m_server.hasArg("idx") ? m_server.arg("idx").toInt() : -1;
        int on  = m_server.hasArg("on")  ? m_server.arg("on").toInt()  : -1;
        if (idx < 0 || on < 0)
        {
            m_server.send(400, "application/json", "{\"ok\":false}");
            return;
        }
        bool ok = m_gh->nexa.forceToggle(m_gh->config.data.nexa, idx, on != 0);
        m_server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    }

    // ── Calibrate dry ──────────────────────────────────────────────────────
    void handleCalibrateDry()
    {
        m_gh->calibrateDry();
        String j = "{\"val\":" + String(m_gh->config.data.irrigation.moisture_cal_dry) + "}";
        m_server.send(200, "application/json", j);
    }

    // ── History ────────────────────────────────────────────────────────────
    void handleHistory()
    {
        m_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        m_server.send(200, "application/json", "");

        String chunk = "{\"ldr\":[";
        sendHistoryArray(chunk, m_gh->ldr_history, m_gh->ldr_history_count,
                         m_gh->ldr_history_write_idx);
        chunk += "],\"moist\":[";
        m_server.sendContent(chunk);
        chunk = "";
        sendHistoryArray(chunk, m_gh->moisture_history, m_gh->moisture_history_count,
                         m_gh->moisture_history_write_idx);
        chunk += "]}";
        m_server.sendContent(chunk);
        m_server.sendContent("");
    }

    void sendHistoryArray(String& chunk, const uint16_t* buf, uint16_t count, uint16_t write_idx)
    {
        uint16_t start_idx = (count < HISTORY_SIZE) ? 0 : write_idx;
        for (uint16_t i = 0; i < count; i++)
        {
            uint16_t idx = (start_idx + i) % HISTORY_SIZE;
            if (i > 0) chunk += ",";
            chunk += String(buf[idx]);
            if (chunk.length() > 256)
            {
                m_server.sendContent(chunk);
                chunk = "";
            }
        }
    }
};
