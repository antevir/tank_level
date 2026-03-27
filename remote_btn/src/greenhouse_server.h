#pragma once

#ifdef FEATURE_GREENHOUSE

#include <WebServer.h>
#include "greenhouse.h"
#include "pump_state.h"

// Forward declarations — set by main.cpp each tick
extern volatile bool      g_button_pressed;
extern volatile PumpState g_pump_state;
extern volatile bool      g_tank_connected;

// ─── Embedded Web UI ───────────────────────────────────────────────────────
static const char GH_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Greenhouse Controller</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;max-width:960px;margin:0 auto;padding:10px;background:#1a1a2e;color:#e0e0e0}
h1{color:#16c79a;margin:10px 0;font-size:1.5em}
h2{color:#16c79a;margin:8px 0;font-size:1.2em}
h3{color:#aaa;margin:6px 0;font-size:1em}
.card{background:#162447;border-radius:8px;padding:15px;margin:10px 0}
canvas{width:100%;background:#0d1b2a;border-radius:4px;margin-top:8px}
input[type=number]{width:70px;background:#1b2838;color:#e0e0e0;border:1px solid #555;padding:4px;border-radius:4px}
input[type=time]{background:#1b2838;color:#e0e0e0;border:1px solid #555;padding:4px;border-radius:4px}
label{display:inline-block;margin:4px 2px}
.btn{background:#16c79a;color:#1a1a2e;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;margin:4px;font-weight:bold}
.btn:hover{background:#1df0b0}
.btn-sm{padding:4px 10px;font-size:0.85em}
.btn-del{background:#c73e1d;color:#fff}
.btn-del:hover{background:#e8421e}
.btn-warn{background:#e6a117;color:#000}
.ts{background:#1b2838;padding:8px;margin:4px 0;border-radius:4px;display:flex;flex-wrap:wrap;align-items:center;gap:6px}
.days{display:flex;gap:2px;flex-wrap:wrap}
.days label{background:#0d1b2a;padding:2px 6px;border-radius:3px;font-size:0.85em;cursor:pointer}
.days input{display:none}
.days input:checked+span{color:#16c79a;font-weight:bold}
.ind{display:inline-block;padding:3px 10px;border-radius:4px;font-size:0.85em;font-weight:bold;margin:2px 4px}
.ind-on{background:#16c79a;color:#000}
.ind-off{background:#555;color:#ccc}
.ind-warn{background:#e6a117;color:#000}
.ind-err{background:#c73e1d;color:#fff}
.status-val{font-size:1.3em;font-weight:bold;color:#16c79a}
.bar-bg{background:#0d1b2a;border-radius:4px;overflow:hidden;height:16px;margin:4px 0}
.bar{height:16px;border-radius:4px;transition:width 0.5s}
.row{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin:6px 0}
.legend{display:flex;gap:12px;margin:4px 0;font-size:0.8em}
.legend i{display:inline-block;width:20px;height:3px;vertical-align:middle;margin-right:3px}
#connBanner{display:none;background:#c73e1d;color:#fff;padding:12px 16px;border-radius:8px;margin:8px 0;font-weight:bold;font-size:1.05em;text-align:center}
</style></head><body>
<h1>&#127793; Greenhouse Controller</h1>

<!-- Disconnected banner — shown whenever TCP to pump server is down -->
<div id="connBanner">&#9888; DISCONNECTED FROM PUMP SERVER — button presses will not reach the pump</div>

<!-- System Status -->
<div class="card">
<h2>System Status</h2>
<div class="row">
<span id="ntp" style="font-size:0.8em;color:#888">NTP: ?</span>
<span id="connInd" class="ind ind-off">Server: -</span>
<span id="btnInd" class="ind ind-off">Button: -</span>
<span id="pumpInd" class="ind ind-off">Pump: -</span>
</div>
</div>

<!-- Light Sensor & Lamp -->
<div class="card">
<h2>&#x1F319; Light Sensor &amp; Lamp</h2>
<div class="row">
<span>LDR: <span class="status-val" id="ldr">-</span> / 1023</span>
<span id="lightInd" class="ind ind-off">Lamp: OFF</span>
<span id="darkInd" class="ind ind-off">Dark: No</span>
</div>
<div class="bar-bg"><div class="bar" id="ldrBar" style="width:0%;background:linear-gradient(90deg,#16c79a,#e2f76e)"></div></div>
<div class="legend">
<span><i style="background:#16c79a"></i>LDR (0-1023)</span>
<span><i style="background:#ff6b6b"></i>Thresholds</span>
</div>
<canvas id="ldrChart" height="150"></canvas>
<h3>Twilight Thresholds</h3>
<div class="row">
<label>Night (on below): <input type="number" min="0" max="1023" id="twi_on"></label>
<label>Day (off above): <input type="number" min="0" max="1023" id="twi_off"></label>
</div>
<h3>Time Schedules</h3>
<div id="tsContainer"></div>
<button class="btn btn-sm" onclick="addTS()">+ Add Schedule</button>
</div>

<!-- Moisture Sensor & Irrigation -->
<div class="card">
<h2>&#x1F4A7; Soil Moisture &amp; Irrigation</h2>
<div class="row">
<span>Moisture: <span class="status-val" id="moistPct">-</span>% (<span id="moistV">-</span> V)</span>
<span id="moistSensorInd" class="ind ind-off">Sensor: -</span>
<span id="valveInd" class="ind ind-off">Valve: OFF</span>
<span id="irrInd" class="ind ind-off">State: Idle</span>
<span id="cycleInd" style="font-size:0.8em;color:#888">Cycles: 0</span>
</div>
<div class="bar-bg"><div class="bar" id="moistBar" style="width:0%;background:linear-gradient(90deg,#0d47a1,#4fc3f7)"></div></div>
<div class="legend">
<span><i style="background:#4fc3f7"></i>Moisture (0-100%)</span>
<span><i style="background:#e6a117"></i>Dry threshold</span>
<span><i style="background:#16c79a"></i>Wet threshold</span>
</div>
<canvas id="moistChart" height="150"></canvas>
<h3>Dry Calibration</h3>
<div class="row">
<span style="font-size:0.85em">Wet (100%): fixed at ADC ≤ 100 (no calibration needed)</span>
<span style="font-size:0.85em">Dry (0%): <span class="status-val" id="calDry">-</span> ADC</span>
<button class="btn btn-sm btn-warn" onclick="calibrate()">&#x1F3DC; Calibrate Dry (in air)</button>
<span id="calMsg" style="color:#16c79a;font-size:0.85em"></span>
</div>
<h3>Irrigation Settings</h3>
<div class="row">
<label><input type="checkbox" id="irrEn"> Irrigation Enabled</label>
</div>
<div class="row">
<label>Dry threshold: <input type="number" min="0" max="100" id="dryTh">%</label>
<label>Wet threshold: <input type="number" min="0" max="100" id="wetTh">%</label>
</div>
<div class="row">
<label>Water ON: <input type="number" min="1" max="180" id="irrOn"> min</label>
<label>Soak OFF: <input type="number" min="1" max="480" id="irrOff"> min</label>
<label>Max cycles: <input type="number" min="1" max="50" id="maxCyc"></label>
</div>
</div>

<!-- Save -->
<div style="margin:15px 0">
<button class="btn" onclick="saveConfig()">&#x1F4BE; Save Configuration</button>
<span id="saveMsg" style="color:#16c79a;margin-left:10px"></span>
</div>

<script>
var cfg={};
var ldrHist=[],moistHist=[];
var dn=['Mon','Tue','Wed','Thu','Fri','Sat','Sun'];
var curLdr=0,curMoistPct=0,curMoistAdc=0;

function init(){fetchConfig();fetchHistory();setInterval(fetchStatus,2000);setInterval(fetchHistory,60000);}

function adcToPct(adc,calW,calD){
if(calD<=calW)return 50;
var p=100-(adc-calW)*100/(calD-calW);
return Math.max(0,Math.min(100,Math.round(p)));
}

function fetchStatus(){
fetch('/api/status').then(function(r){return r.json();}).then(function(d){
curLdr=d.ldr;curMoistPct=d.moist_ok?d.moist_pct:0;curMoistAdc=d.moist;
document.getElementById('ldr').textContent=d.ldr;
document.getElementById('ldrBar').style.width=(d.ldr/1023*100)+'%';
document.getElementById('moistV').textContent=(d.moist/1023*3.3).toFixed(2);
var ms=document.getElementById('moistSensorInd');
if(d.moist_ok){
ms.className='ind ind-on';ms.textContent='Sensor: OK';
document.getElementById('moistPct').textContent=d.moist_pct;
document.getElementById('moistBar').style.width=d.moist_pct+'%';
}else{
ms.className='ind ind-err';ms.textContent='Sensor: DISCONNECTED';
document.getElementById('moistPct').textContent='--';
document.getElementById('moistBar').style.width='0%';
}
document.getElementById('ntp').textContent='NTP: '+(d.ntp?'synced':'waiting...');

// Connection state — most prominent indicator
var banner=document.getElementById('connBanner');
var ci=document.getElementById('connInd');
if(d.conn){
banner.style.display='none';
ci.className='ind ind-on';ci.textContent='Server: Connected';
}else{
banner.style.display='block';
ci.className='ind ind-err';ci.textContent='Server: DISCONNECTED';
}

// Light
var le=document.getElementById('lightInd');
if(d.light){le.className='ind ind-on';le.textContent='Lamp: ON';}
else{le.className='ind ind-off';le.textContent='Lamp: OFF';}
var de=document.getElementById('darkInd');
if(d.dark){de.className='ind ind-on';de.textContent='Dark: Yes';}
else{de.className='ind ind-off';de.textContent='Dark: No';}

// Valve & irrigation state
var ve=document.getElementById('valveInd');
if(!d.moist_ok){ve.className='ind ind-err';ve.textContent='Valve: Fault';}
else if(d.valve){ve.className='ind ind-on';ve.textContent='Valve: ON';}
else{ve.className='ind ind-off';ve.textContent='Valve: OFF';}
var ie=document.getElementById('irrInd');
if(!d.moist_ok){ie.className='ind ind-err';ie.textContent='State: No Sensor';}
else{
var iStates=['Idle','Watering','Soaking','Paused'];
var iColors=['ind ind-off','ind ind-on','ind ind-warn','ind ind-err'];
ie.className=iColors[d.irr_st]||'ind ind-off';
ie.textContent='State: '+(iStates[d.irr_st]||'?');
}
document.getElementById('cycleInd').textContent='Cycles: '+(d.irr_cc||0);

// Button
var be=document.getElementById('btnInd');
if(d.btn){be.className='ind ind-on';be.textContent='Button: PRESSED';}
else{be.className='ind ind-off';be.textContent='Button: -';}

// Pump — grey out with "Unknown" when TCP is not connected
var pe=document.getElementById('pumpInd');
if(!d.conn){
pe.className='ind ind-err';pe.textContent='Pump: Unknown';
}else{
var pLabels={'-2':'DryRun','-1':'Off','0':'Idle','1':'Running','2':'Warning'};
var pColors={'-2':'ind ind-err','-1':'ind ind-off','0':'ind ind-off','1':'ind ind-on','2':'ind ind-warn'};
var ps=String(d.pump);
pe.className=pColors[ps]||'ind ind-off';
pe.textContent='Pump: '+(pLabels[ps]||'?');
}
}).catch(function(){
// Fetch itself failed — device unreachable
document.getElementById('connBanner').style.display='block';
var ci=document.getElementById('connInd');
ci.className='ind ind-err';ci.textContent='Server: DISCONNECTED';
});}

function fetchConfig(){
fetch('/api/config').then(function(r){return r.json();}).then(function(d){
cfg=d;renderConfig();
}).catch(function(){});
}

function fetchHistory(){
fetch('/api/history').then(function(r){return r.json();}).then(function(d){
ldrHist=d.ldr||[];moistHist=d.moist||[];drawCharts();
}).catch(function(){});
}

function renderConfig(){
var l=cfg.light||{};
document.getElementById('twi_on').value=l.twi_on||300;
document.getElementById('twi_off').value=l.twi_off||400;
renderTS(l.ts||[]);
var ir=cfg.irr||{};
document.getElementById('irrEn').checked=!!ir.en;
document.getElementById('dryTh').value=ir.dry_pct!=null?ir.dry_pct:30;
document.getElementById('wetTh').value=ir.wet_pct!=null?ir.wet_pct:60;
document.getElementById('irrOn').value=ir.on_m||3;
document.getElementById('irrOff').value=ir.off_m||20;
document.getElementById('maxCyc').value=ir.max_c||6;
document.getElementById('calDry').textContent=ir.cal_dry||775;
}

function renderTS(tsList){
if(!cfg.light)cfg.light={};
cfg.light.ts=tsList;
var html='';
for(var t=0;t<tsList.length;t++){
var ts=tsList[t];
var h='<div class="ts">';
h+='<input type="time" value="'+pad(ts.sh)+':'+pad(ts.sm)+'" id="tss_'+t+'">';
h+=' &rarr; <input type="time" value="'+pad(ts.eh)+':'+pad(ts.em)+'" id="tse_'+t+'">';
h+=' <div class="days">';
for(var d=0;d<7;d++){
var ck=((ts.days>>d)&1)?'checked':'';
h+='<label><input type="checkbox" '+ck+' id="tsd_'+t+'_'+d+'"><span>'+dn[d]+'</span></label>';
}
h+='</div>';
h+='<label><input type="checkbox" '+(ts.en?'checked':'')+' id="tsen_'+t+'"> On</label>';
h+='<button class="btn btn-sm btn-del" onclick="delTS('+t+')">X</button>';
h+='</div>';
html+=h;
}
document.getElementById('tsContainer').innerHTML=html;
}

function pad(n){return n<10?'0'+n:''+n;}

function addTS(){
gatherConfig();
var ts=cfg.light.ts||[];
if(ts.length>=4){alert('Max 4 schedules');return;}
ts.push({sh:21,sm:0,eh:7,em:0,days:127,en:1});
renderTS(ts);
}

function delTS(idx){gatherConfig();cfg.light.ts.splice(idx,1);renderTS(cfg.light.ts);}

function gatherConfig(){
if(!cfg.light)cfg.light={};
if(!cfg.irr)cfg.irr={};
cfg.light.twi_on=parseInt(document.getElementById('twi_on').value)||0;
cfg.light.twi_off=parseInt(document.getElementById('twi_off').value)||0;
var ts=cfg.light.ts||[];
for(var t=0;t<ts.length;t++){
var sv=document.getElementById('tss_'+t).value.split(':');
var ev=document.getElementById('tse_'+t).value.split(':');
ts[t].sh=parseInt(sv[0])||0;ts[t].sm=parseInt(sv[1])||0;
ts[t].eh=parseInt(ev[0])||0;ts[t].em=parseInt(ev[1])||0;
var days=0;
for(var d=0;d<7;d++){if(document.getElementById('tsd_'+t+'_'+d).checked)days|=(1<<d);}
ts[t].days=days;ts[t].en=document.getElementById('tsen_'+t).checked?1:0;
}
cfg.irr.en=document.getElementById('irrEn').checked?1:0;
cfg.irr.dry_pct=parseInt(document.getElementById('dryTh').value)||0;
cfg.irr.wet_pct=parseInt(document.getElementById('wetTh').value)||0;
cfg.irr.on_m=parseInt(document.getElementById('irrOn').value)||0;
cfg.irr.off_m=parseInt(document.getElementById('irrOff').value)||0;
cfg.irr.max_c=parseInt(document.getElementById('maxCyc').value)||0;
}

function saveConfig(){
gatherConfig();
var b='twi_on='+cfg.light.twi_on+'&twi_off='+cfg.light.twi_off;
var ts=cfg.light.ts||[];
b+='&tsc='+ts.length;
for(var t=0;t<ts.length;t++){
b+='&t'+t+'_sh='+ts[t].sh+'&t'+t+'_sm='+ts[t].sm+'&t'+t+'_eh='+ts[t].eh+'&t'+t+'_em='+ts[t].em+'&t'+t+'_d='+ts[t].days+'&t'+t+'_en='+ts[t].en;
}
b+='&irr_en='+cfg.irr.en+'&irr_dry_pct='+cfg.irr.dry_pct+'&irr_wet_pct='+cfg.irr.wet_pct;
b+='&irr_on_m='+cfg.irr.on_m+'&irr_off_m='+cfg.irr.off_m+'&irr_max='+cfg.irr.max_c;
fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b})
.then(function(r){return r.json();}).then(function(d){
document.getElementById('saveMsg').textContent=d.ok?'Saved!':'Error';
setTimeout(function(){document.getElementById('saveMsg').textContent='';},3000);
}).catch(function(){document.getElementById('saveMsg').textContent='Error';});
}

function calibrate(){
fetch('/api/calibrate_dry',{method:'POST'}).then(function(r){return r.json();}).then(function(d){
document.getElementById('calDry').textContent=d.val;cfg.irr=cfg.irr||{};cfg.irr.cal_dry=d.val;
document.getElementById('calMsg').textContent='Calibrated dry: '+d.val+' ADC';
setTimeout(function(){document.getElementById('calMsg').textContent='';},4000);
drawCharts();
}).catch(function(){});
}

function drawCharts(){
drawChart('ldrChart',ldrHist,curLdr,1023,'#16c79a',
[{v:cfg.light?cfg.light.twi_on:300,c:'#ff6b6b',d:[5,3]},
 {v:cfg.light?cfg.light.twi_off:400,c:'#ff6b6b',d:[2,3]}]);
var calW=100;  // Fixed: ADC <= 100 = 100% moisture
var calD=(cfg.irr&&cfg.irr.cal_dry!=null)?cfg.irr.cal_dry:775;
var moistPctHist=moistHist.map(function(v){return adcToPct(v,calW,calD);});
drawChart('moistChart',moistPctHist,curMoistPct,100,'#4fc3f7',
[{v:(cfg.irr&&cfg.irr.dry_pct!=null)?cfg.irr.dry_pct:30,c:'#e6a117',d:[5,3]},
 {v:(cfg.irr&&cfg.irr.wet_pct!=null)?cfg.irr.wet_pct:60,c:'#16c79a',d:[2,3]}]);
}

function drawChart(canvasId,hist,cur,yMax,lineColor,thresholds){
var c=document.getElementById(canvasId);if(!c)return;
var ctx=c.getContext('2d');
var dpr=window.devicePixelRatio||1;
var rect=c.getBoundingClientRect();
c.width=rect.width*dpr;c.height=150*dpr;
ctx.scale(dpr,dpr);
var W=rect.width,H=150;
ctx.clearRect(0,0,W,H);ctx.fillStyle='#0d1b2a';ctx.fillRect(0,0,W,H);
ctx.strokeStyle='#222';ctx.lineWidth=0.5;
for(var i=0;i<=4;i++){
var y=H*i/4;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(W,y);ctx.stroke();
ctx.fillStyle='#555';ctx.font='10px Arial';
ctx.fillText(Math.round(yMax*(1-i/4)),2,y+10);
}
var n=hist.length;
ctx.fillStyle='#555';
for(var i=0;i<=6;i++){
var x=W*i/6;
var mins=Math.round(n-n*i/6);
var lbl;
if(mins>=120)lbl='-'+Math.round(mins/60)+'h';
else if(mins>0)lbl='-'+mins+'m';
else lbl='now';
ctx.fillText(lbl,i<6?x+2:x-22,H-3);
}
for(var ti=0;ti<thresholds.length;ti++){
var th=thresholds[ti];
ctx.strokeStyle=th.c;ctx.lineWidth=1;ctx.setLineDash(th.d||[4,3]);
var ty=H-(th.v/yMax*H);
ctx.beginPath();ctx.moveTo(0,ty);ctx.lineTo(W,ty);ctx.stroke();
ctx.setLineDash([]);
}
if(n>0){
ctx.strokeStyle=lineColor;ctx.lineWidth=1.5;ctx.beginPath();
for(var i=0;i<n;i++){
var x=n>1?i/(n-1)*W:W/2;
var y=H-(hist[i]/yMax*H);
if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
}
ctx.stroke();
var cy=H-(cur/yMax*H);
ctx.beginPath();ctx.arc(W-2,cy,3,0,Math.PI*2);ctx.fillStyle=lineColor;ctx.fill();
}
}

window.addEventListener('resize',drawCharts);
init();
</script></body></html>)rawliteral";

// ─── Server class ──────────────────────────────────────────────────────────

class GreenhouseServer {
public:
    GreenhouseServer() : m_server(80) {}

    void begin(GreenhouseCtrl* ctrl)
    {
        m_gh = ctrl;

        m_server.on("/", HTTP_GET, [this]() { handleRoot(); });
        m_server.on("/api/status",        HTTP_GET,  [this]() { handleStatus(); });
        m_server.on("/api/config",        HTTP_GET,  [this]() { handleGetConfig(); });
        m_server.on("/api/config",        HTTP_POST, [this]() { handlePostConfig(); });
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
    WebServer      m_server;
    GreenhouseCtrl* m_gh = nullptr;

    void handleRoot()
    {
        m_server.send_P(200, PSTR("text/html"), GH_HTML);
    }

    void handleStatus()
    {
        String j = "{";
        j += "\"ldr\":"        + String(m_gh->ldr_reading);
        j += ",\"moist\":"     + String(m_gh->moisture_reading);
        j += ",\"moist_pct\":" + String(m_gh->moisture_pct);
        j += ",\"moist_ok\":"  + String(m_gh->moisture_sensor_ok ? "true" : "false");
        j += ",\"ntp\":"       + String(m_gh->isTimeSynced() ? "true" : "false");
        j += ",\"light\":"     + String(m_gh->light_on  ? "true" : "false");
        j += ",\"dark\":"      + String(m_gh->is_dark   ? "true" : "false");
        j += ",\"valve\":"     + String(m_gh->valve_on  ? "true" : "false");
        j += ",\"irr_st\":"    + String((int)m_gh->irr_state);
        j += ",\"irr_cc\":"    + String(m_gh->irr_cycle_count);
        j += ",\"btn\":"       + String(g_button_pressed ? "true" : "false");
        j += ",\"conn\":"      + String(g_tank_connected  ? "true" : "false");
        j += ",\"pump\":"      + String((int)g_pump_state);
        j += "}";
        m_server.send(200, "application/json", j);
    }

    void handleGetConfig()
    {
        const LightCfg&      l  = m_gh->config.data.light;
        const IrrigationCfg& ir = m_gh->config.data.irrigation;

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
        j += "\"en\":"         + String(ir.enabled);
        j += ",\"cal_dry\":"  + String(ir.moisture_cal_dry);
        j += ",\"dry_pct\":"  + String(ir.dry_threshold_pct);
        j += ",\"wet_pct\":"  + String(ir.wet_threshold_pct);
        j += ",\"on_m\":"     + String(ir.irrigate_on_min);
        j += ",\"off_m\":"    + String(ir.irrigate_off_min);
        j += ",\"max_c\":"    + String(ir.max_cycles);
        j += "}}";
        m_server.send(200, "application/json", j);
    }

    void handlePostConfig()
    {
        LightCfg&      l  = m_gh->config.data.light;
        IrrigationCfg& ir = m_gh->config.data.irrigation;

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
        Log.info("[GH-SRV] Config saved");
        m_server.send(200, "application/json", "{\"ok\":true}");
    }

    void handleCalibrateDry()
    {
        m_gh->calibrateDry();
        String j = "{\"val\":" + String(m_gh->config.data.irrigation.moisture_cal_dry) + "}";
        m_server.send(200, "application/json", j);
    }

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

#endif // FEATURE_GREENHOUSE
