#pragma once

#ifdef FEATURE_NIGHTLIGHT

#include <ESP8266WebServer.h>
#include "nightlight.h"

// ─── Embedded Web UI ───────────────────────────────────────────────────────
static const char NIGHTLIGHT_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Night Light</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;max-width:900px;margin:0 auto;padding:10px;background:#1a1a2e;color:#e0e0e0}
h1{color:#16c79a;margin:10px 0;font-size:1.5em}
h2{color:#16c79a;margin:8px 0;font-size:1.2em}
h3{color:#aaa;margin:6px 0;font-size:1em}
.card{background:#162447;border-radius:8px;padding:15px;margin:10px 0}
canvas{width:100%;background:#0d1b2a;border-radius:4px;margin-top:8px}
input[type=range]{width:200px;vertical-align:middle}
input[type=number]{width:70px;background:#1b2838;color:#e0e0e0;border:1px solid #555;padding:4px;border-radius:4px}
input[type=time]{background:#1b2838;color:#e0e0e0;border:1px solid #555;padding:4px;border-radius:4px}
label{display:inline-block;margin:4px 2px}
.btn{background:#16c79a;color:#1a1a2e;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;margin:4px;font-weight:bold}
.btn:hover{background:#1df0b0}
.btn-sm{padding:4px 10px;font-size:0.85em}
.btn-del{background:#c73e1d;color:#fff}
.btn-del:hover{background:#e8421e}
.ts{background:#1b2838;padding:8px;margin:4px 0;border-radius:4px;display:flex;flex-wrap:wrap;align-items:center;gap:6px}
.days{display:flex;gap:2px;flex-wrap:wrap}
.days label{background:#0d1b2a;padding:2px 6px;border-radius:3px;font-size:0.85em;cursor:pointer}
.days input{display:none}
.days input:checked+span{color:#16c79a;font-weight:bold}
.status{font-size:1.5em;font-weight:bold;color:#16c79a}
.bar-bg{background:#0d1b2a;border-radius:4px;overflow:hidden;height:20px;margin:6px 0}
.bar{height:20px;background:linear-gradient(90deg,#16c79a,#e2f76e);border-radius:4px;transition:width 0.5s}
.row{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin:6px 0}
.ch-status{padding:2px 8px;border-radius:4px;font-size:0.85em}
.ch-on{background:#16c79a;color:#000}
.ch-off{background:#555;color:#ccc}
.legend{display:flex;gap:12px;margin:4px 0;font-size:0.8em}
.legend i{display:inline-block;width:20px;height:3px;vertical-align:middle;margin-right:3px}
</style></head><body>
<h1>&#x1F319; Night Light Configuration</h1>

<div class="card">
<h2>Light Sensor</h2>
<div class="row">
<span>Current: <span class="status" id="rdg">-</span> / 1023</span>
<span id="ch0s" class="ch-status ch-off">CH1: OFF</span>
<span id="ch1s" class="ch-status ch-off">CH2: OFF</span>
<span id="ntp" style="font-size:0.8em;color:#888">NTP: ?</span>
</div>
<div class="bar-bg"><div class="bar" id="bar" style="width:0%"></div></div>
<div class="legend">
<span><i style="background:#16c79a"></i>Sensor</span>
<span><i style="background:#ff6b6b;border-top:2px dashed #ff6b6b"></i>CH1 thresholds</span>
<span><i style="background:#ffd93d;border-top:2px dashed #ffd93d"></i>CH2 thresholds</span>
</div>
<canvas id="chart" height="180"></canvas>
</div>

<div id="channels"></div>

<div style="margin:15px 0">
<button class="btn" onclick="saveConfig()">&#x1F4BE; Save Configuration</button>
<span id="saveMsg" style="color:#16c79a;margin-left:10px"></span>
</div>

<script>
var cfg={channels:[
{pwm:512,twi_on:300,twi_off:400,ts:[{sh:21,sm:0,eh:7,em:0,days:127,en:1}]},
{pwm:512,twi_on:300,twi_off:400,ts:[{sh:21,sm:0,eh:7,em:0,days:127,en:1}]}
]};
var hist=[];
var dn=['Mon','Tue','Wed','Thu','Fri','Sat','Sun'];
var curReading=0;
var pwmDebounce=[null,null];

function init(){
fetchConfig();
fetchHistory();
setInterval(fetchStatus,2000);
setInterval(fetchHistory,60000);
}

function fetchStatus(){
fetch('/api/status').then(function(r){return r.json();}).then(function(d){
curReading=d.r;
document.getElementById('rdg').textContent=d.r;
document.getElementById('bar').style.width=(d.r/1023*100)+'%';
document.getElementById('ntp').textContent='NTP: '+(d.ntp?'synced':'waiting...');
for(var i=0;i<2;i++){
var el=document.getElementById('ch'+i+'s');
if(d['c'+i]){el.className='ch-status ch-on';el.textContent='CH'+(i+1)+': ON';}
else{el.className='ch-status ch-off';el.textContent='CH'+(i+1)+': OFF';}
}
}).catch(function(){});
}

function fetchConfig(){
fetch('/api/config').then(function(r){return r.json();}).then(function(d){
cfg=d;renderChannels();
}).catch(function(){renderChannels();});
}

function fetchHistory(){
fetch('/api/history').then(function(r){return r.json();}).then(function(d){
hist=d;drawChart();
}).catch(function(){});
}

function renderChannels(){
var html='';
for(var ch=0;ch<2;ch++){
var c=cfg.channels[ch];
html+='<div class="card"><h2>Channel '+(ch+1)+'</h2>';
html+='<div class="row"><label>PWM Level: <input type="range" min="0" max="1023" value="'+c.pwm+'" id="pwm'+ch+'" oninput="pwmSlide('+ch+')"> ';
html+='<span id="pwmV'+ch+'">'+(c.pwm/1023*100).toFixed(0)+'</span>%</label></div>';
html+='<h3>Twilight Thresholds</h3>';
html+='<div class="row"><label>Night (on below): <input type="number" min="0" max="1023" value="'+c.twi_on+'" id="ton'+ch+'"></label>';
html+='<label>Day (off above): <input type="number" min="0" max="1023" value="'+c.twi_off+'" id="toff'+ch+'"></label></div>';
html+='<h3>Time Schedules</h3><div id="ts'+ch+'">';
for(var t=0;t<c.ts.length;t++){html+=renderTS(ch,t,c.ts[t]);}
html+='</div><button class="btn btn-sm" onclick="addTS('+ch+')">+ Add Schedule</button>';
html+='</div>';
}
document.getElementById('channels').innerHTML=html;
}

function renderTS(ch,idx,ts){
var h='<div class="ts" id="ts_'+ch+'_'+idx+'">';
h+='<input type="time" value="'+pad(ts.sh)+':'+pad(ts.sm)+'" id="tss_'+ch+'_'+idx+'">';
h+=' &rarr; <input type="time" value="'+pad(ts.eh)+':'+pad(ts.em)+'" id="tse_'+ch+'_'+idx+'">';
h+=' <div class="days">';
for(var d=0;d<7;d++){
var ck=((ts.days>>d)&1)?'checked':'';
h+='<label><input type="checkbox" '+ck+' id="tsd_'+ch+'_'+idx+'_'+d+'"><span>'+dn[d]+'</span></label>';
}
h+='</div>';
h+='<label><input type="checkbox" '+(ts.en?'checked':'')+' id="tsen_'+ch+'_'+idx+'"> On</label>';
h+='<button class="btn btn-sm btn-del" onclick="delTS('+ch+','+idx+')">X</button>';
h+='</div>';
return h;
}

function pad(n){return n<10?'0'+n:''+n;}

function pwmSlide(ch){
var v=document.getElementById('pwm'+ch).value;
document.getElementById('pwmV'+ch).textContent=(v/1023*100).toFixed(0);
if(pwmDebounce[ch])clearTimeout(pwmDebounce[ch]);
pwmDebounce[ch]=setTimeout(function(){preview(ch);},300);
}

function preview(ch){
var v=document.getElementById('pwm'+ch).value;
fetch('/api/preview',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'ch='+ch+'&pwm='+v});
}

function addTS(ch){
gatherConfig();
if(cfg.channels[ch].ts.length>=4){alert('Max 4 schedules per channel');return;}
cfg.channels[ch].ts.push({sh:21,sm:0,eh:7,em:0,days:127,en:1});
renderChannels();
}

function delTS(ch,idx){
gatherConfig();
cfg.channels[ch].ts.splice(idx,1);
renderChannels();
}

function gatherConfig(){
for(var ch=0;ch<2;ch++){
var c=cfg.channels[ch];
c.pwm=parseInt(document.getElementById('pwm'+ch).value)||0;
c.twi_on=parseInt(document.getElementById('ton'+ch).value)||0;
c.twi_off=parseInt(document.getElementById('toff'+ch).value)||0;
for(var t=0;t<c.ts.length;t++){
var sv=document.getElementById('tss_'+ch+'_'+t).value.split(':');
var ev=document.getElementById('tse_'+ch+'_'+t).value.split(':');
c.ts[t].sh=parseInt(sv[0])||0;
c.ts[t].sm=parseInt(sv[1])||0;
c.ts[t].eh=parseInt(ev[0])||0;
c.ts[t].em=parseInt(ev[1])||0;
var days=0;
for(var d=0;d<7;d++){if(document.getElementById('tsd_'+ch+'_'+t+'_'+d).checked)days|=(1<<d);}
c.ts[t].days=days;
c.ts[t].en=document.getElementById('tsen_'+ch+'_'+t).checked?1:0;
}
}
}

function saveConfig(){
gatherConfig();
var body='';
for(var ch=0;ch<2;ch++){
var c=cfg.channels[ch];
var p='c'+ch;
if(ch>0)body+='&';
body+=p+'_pwm='+c.pwm+'&'+p+'_ton='+c.twi_on+'&'+p+'_toff='+c.twi_off+'&'+p+'_tsc='+c.ts.length;
for(var t=0;t<c.ts.length;t++){
var tp=p+'_t'+t;
body+='&'+tp+'_sh='+c.ts[t].sh+'&'+tp+'_sm='+c.ts[t].sm+'&'+tp+'_eh='+c.ts[t].eh+'&'+tp+'_em='+c.ts[t].em+'&'+tp+'_d='+c.ts[t].days+'&'+tp+'_en='+c.ts[t].en;
}
}
fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
.then(function(r){return r.json();}).then(function(d){
document.getElementById('saveMsg').textContent=d.ok?'Saved!':'Error';
setTimeout(function(){document.getElementById('saveMsg').textContent='';},3000);
}).catch(function(){document.getElementById('saveMsg').textContent='Error';});
}

function drawChart(){
var c=document.getElementById('chart');
var ctx=c.getContext('2d');
var dpr=window.devicePixelRatio||1;
var rect=c.getBoundingClientRect();
c.width=rect.width*dpr;
c.height=180*dpr;
ctx.scale(dpr,dpr);
var W=rect.width,H=180;
ctx.clearRect(0,0,W,H);
ctx.fillStyle='#0d1b2a';ctx.fillRect(0,0,W,H);

// Grid
ctx.strokeStyle='#222';ctx.lineWidth=0.5;
for(var i=0;i<=4;i++){
var y=H*i/4;
ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(W,y);ctx.stroke();
ctx.fillStyle='#555';ctx.font='10px Arial';
ctx.fillText(Math.round(1023*(1-i/4)),2,y+10);
}

// Time labels
ctx.fillStyle='#555';
var n=hist.length;
for(var i=0;i<=6;i++){
var x=W*i/6;
var hrs=Math.round((n-n*i/6)/60);
if(hrs>0)ctx.fillText('-'+hrs+'h',x+2,H-3);
else ctx.fillText('now',x-18,H-3);
}

// Threshold lines
var thColors=['#ff6b6b','#ffd93d'];
for(var ch=0;ch<2;ch++){
ctx.strokeStyle=thColors[ch];ctx.lineWidth=1;
var yOn=H-(cfg.channels[ch].twi_on/1023*H);
ctx.setLineDash([5,3]);
ctx.beginPath();ctx.moveTo(0,yOn);ctx.lineTo(W,yOn);ctx.stroke();
var yOff=H-(cfg.channels[ch].twi_off/1023*H);
ctx.setLineDash([2,3]);
ctx.beginPath();ctx.moveTo(0,yOff);ctx.lineTo(W,yOff);ctx.stroke();
ctx.setLineDash([]);
}

// Data line
if(n>0){
ctx.strokeStyle='#16c79a';ctx.lineWidth=1.5;
ctx.beginPath();
for(var i=0;i<n;i++){
var x=n>1?i/(n-1)*W:W/2;
var y=H-(hist[i]/1023*H);
if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
}
ctx.stroke();
}

// Current reading marker
if(n>0){
var cy=H-(curReading/1023*H);
ctx.beginPath();ctx.arc(W-2,cy,3,0,Math.PI*2);ctx.fillStyle='#16c79a';ctx.fill();
}
}

window.addEventListener('resize',drawChart);
init();
</script></body></html>)rawliteral";

// ─── Server class ──────────────────────────────────────────────────────────

class NightlightServer {
public:
    NightlightServer() : m_server(80) {}

    void begin(Nightlight* nightlight)
    {
        m_nl = nightlight;

        m_server.on("/", HTTP_GET, [this]() { handleRoot(); });
        m_server.on("/api/status",  HTTP_GET,  [this]() { handleStatus(); });
        m_server.on("/api/config",  HTTP_GET,  [this]() { handleGetConfig(); });
        m_server.on("/api/config",  HTTP_POST, [this]() { handlePostConfig(); });
        m_server.on("/api/preview", HTTP_POST, [this]() { handlePreview(); });
        m_server.on("/api/history", HTTP_GET,  [this]() { handleHistory(); });

        m_server.begin();
        Log.info("[NL-SRV] Web server started on port 80");
    }

    void handle()
    {
        m_server.handleClient();
    }

private:
    ESP8266WebServer m_server;
    Nightlight*      m_nl = nullptr;

    // --- Handlers ---

    void handleRoot()
    {
        m_server.send_P(200, PSTR("text/html"), NIGHTLIGHT_HTML);
    }

    void handleStatus()
    {
        String json = "{\"r\":" + String(m_nl->current_reading);
        for (int i = 0; i < NUM_CHANNELS; i++)
        {
            json += ",\"c" + String(i) + "\":" + String(m_nl->channel_on[i] ? "true" : "false");
        }
        json += ",\"ntp\":" + String(m_nl->isTimeSynced() ? "true" : "false");
        json += "}";
        m_server.send(200, "application/json", json);
    }

    void handleGetConfig()
    {
        String json = "{\"channels\":[";
        for (int ch = 0; ch < NUM_CHANNELS; ch++)
        {
            if (ch > 0) json += ",";
            const ChannelCfg& c = m_nl->config.data.channels[ch];
            json += "{\"pwm\":" + String(c.pwm_value);
            json += ",\"twi_on\":" + String(c.twilight_on);
            json += ",\"twi_off\":" + String(c.twilight_off);
            json += ",\"ts\":[";
            for (int t = 0; t < c.num_time_spans && t < MAX_TIME_SPANS; t++)
            {
                if (t > 0) json += ",";
                const TimeSpanCfg& ts = c.time_spans[t];
                json += "{\"sh\":" + String(ts.start_hour);
                json += ",\"sm\":" + String(ts.start_minute);
                json += ",\"eh\":" + String(ts.end_hour);
                json += ",\"em\":" + String(ts.end_minute);
                json += ",\"days\":" + String(ts.weekdays);
                json += ",\"en\":" + String(ts.enabled);
                json += "}";
            }
            json += "]}";
        }
        json += "]}";
        m_server.send(200, "application/json", json);
    }

    void handlePostConfig()
    {
        for (int ch = 0; ch < NUM_CHANNELS; ch++)
        {
            String p = "c" + String(ch);
            ChannelCfg& c = m_nl->config.data.channels[ch];

            if (m_server.hasArg(p + "_pwm"))
            {
                int v = m_server.arg(p + "_pwm").toInt();
                c.pwm_value = constrain(v, 0, 1023);
            }
            if (m_server.hasArg(p + "_ton"))
            {
                int v = m_server.arg(p + "_ton").toInt();
                c.twilight_on = constrain(v, 0, 1023);
            }
            if (m_server.hasArg(p + "_toff"))
            {
                int v = m_server.arg(p + "_toff").toInt();
                c.twilight_off = constrain(v, 0, 1023);
            }

            // Ensure hysteresis is valid (off > on)
            if (c.twilight_off <= c.twilight_on)
                c.twilight_off = c.twilight_on + 1;

            if (m_server.hasArg(p + "_tsc"))
            {
                int n = m_server.arg(p + "_tsc").toInt();
                c.num_time_spans = constrain(n, 0, MAX_TIME_SPANS);
            }

            for (int t = 0; t < c.num_time_spans && t < MAX_TIME_SPANS; t++)
            {
                String tp = p + "_t" + String(t);
                TimeSpanCfg& ts = c.time_spans[t];

                if (m_server.hasArg(tp + "_sh")) ts.start_hour   = constrain(m_server.arg(tp + "_sh").toInt(), 0, 23);
                if (m_server.hasArg(tp + "_sm")) ts.start_minute  = constrain(m_server.arg(tp + "_sm").toInt(), 0, 59);
                if (m_server.hasArg(tp + "_eh")) ts.end_hour      = constrain(m_server.arg(tp + "_eh").toInt(), 0, 23);
                if (m_server.hasArg(tp + "_em")) ts.end_minute    = constrain(m_server.arg(tp + "_em").toInt(), 0, 59);
                if (m_server.hasArg(tp + "_d"))  ts.weekdays      = m_server.arg(tp + "_d").toInt() & 0x7F;
                if (m_server.hasArg(tp + "_en")) ts.enabled       = m_server.arg(tp + "_en").toInt() ? 1 : 0;
            }
        }

        m_nl->config.save();
        m_nl->onConfigSaved();

        Log.info("[NL-SRV] Config saved");
        m_server.send(200, "application/json", "{\"ok\":true}");
    }

    void handlePreview()
    {
        int ch  = m_server.arg("ch").toInt();
        int pwm = m_server.arg("pwm").toInt();
        pwm = constrain(pwm, 0, 1023);
        m_nl->startPreview(ch, pwm);
        m_server.send(200, "application/json", "{\"ok\":true}");
    }

    void handleHistory()
    {
        // Stream history as JSON array, chunked to save RAM
        m_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        m_server.send(200, "application/json", "");

        String chunk = "[";
        uint16_t count = m_nl->history_count;
        uint16_t start_idx;

        if (count < HISTORY_SIZE)
            start_idx = 0;
        else
            start_idx = m_nl->history_write_idx; // Oldest sample

        for (uint16_t i = 0; i < count; i++)
        {
            uint16_t idx = (start_idx + i) % HISTORY_SIZE;
            if (i > 0) chunk += ",";
            chunk += String(m_nl->history[idx]);

            // Flush in chunks to avoid large String allocations
            if (chunk.length() > 256)
            {
                m_server.sendContent(chunk);
                chunk = "";
            }
        }
        chunk += "]";
        m_server.sendContent(chunk);
        m_server.sendContent(""); // End chunked transfer
    }
};

#endif // FEATURE_NIGHTLIGHT
