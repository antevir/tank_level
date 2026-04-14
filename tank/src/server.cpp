#include <Arduino.h>
#include "common.h"
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>

#include "Log.h"
#include "server.h"
#include "tank.h"
#include "pump.h"
#include "tcp_server.h"

static WebServer server(80);

// ─── Embedded Dashboard HTML ───────────────────────────────────────────────
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tank Dashboard</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;max-width:960px;margin:0 auto;padding:10px;background:#1a1a2e;color:#e0e0e0}
h1{color:#16c79a;margin:10px 0;font-size:1.5em}
h2{color:#16c79a;margin:8px 0;font-size:1.2em}
.card{background:#162447;border-radius:8px;padding:15px;margin:10px 0}
.btn{background:#16c79a;color:#1a1a2e;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;margin:4px;font-weight:bold;text-decoration:none;display:inline-block}
.btn:hover{background:#1df0b0}
.btn-sec{background:#555;color:#fff}
.btn-sec:hover{background:#777}
.btn-del{background:#c73e1d;color:#fff}
.btn-del:hover{background:#e8421e}
.ind{display:inline-block;padding:3px 10px;border-radius:4px;font-size:0.85em;font-weight:bold;margin:2px 4px}
.ind-on{background:#16c79a;color:#000}
.ind-off{background:#555;color:#ccc}
.ind-warn{background:#e6a117;color:#000}
.ind-err{background:#c73e1d;color:#fff}
.status-val{font-size:1.3em;font-weight:bold;color:#16c79a}
.row{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin:6px 0}
.hdr{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap}
.tank-bar-wrap{width:100%;max-width:300px;height:260px;background:#0d1b2a;border-radius:8px;position:relative;overflow:hidden;border:2px solid #16c79a}
.tank-bar{position:absolute;bottom:0;width:100%;background:linear-gradient(0deg,#0d47a1,#4fc3f7);transition:height 0.8s;display:flex;align-items:center;justify-content:center;font-size:1.8em;font-weight:bold;color:#fff;text-shadow:0 1px 4px rgba(0,0,0,0.5)}
.stat-table{width:100%}
.stat-table td,.stat-table th{padding:6px 10px;border-bottom:1px solid #1b2838;text-align:left}
.stat-table th{color:#888;font-weight:normal;width:45%}
canvas{width:100%;background:#0d1b2a;border-radius:4px;margin-top:8px}
.legend{display:flex;gap:12px;margin:4px 0;font-size:0.8em}
.legend i{display:inline-block;width:20px;height:3px;vertical-align:middle;margin-right:3px}
.tab-bar{display:flex;gap:4px;margin:8px 0}
.tab-bar button{background:#1b2838;color:#888;border:none;padding:6px 14px;border-radius:4px 4px 0 0;cursor:pointer;font-weight:bold}
.tab-bar button.active{background:#16c79a;color:#1a1a2e}
</style></head><body>
<h1><h1>&#x1F4A7; Tank Dashboard <a href="/log" class="btn btn-sec" style="font-size:0.6em;vertical-align:middle">Log</a></h1></h1>

<div class="card"><h2>System Status</h2>
<div class="row">
<span id="pumpInd" class="ind ind-off">Pump: -</span>
<span id="connInd" class="ind ind-off">Clients: 0</span>
<span id="sdInd" class="ind ind-off">SD Card: ?</span>
<span id="sensorInd" class="ind ind-off">Sensor: ?</span>
</div></div>

<div class="card">
<div class="row" style="align-items:flex-start;gap:20px">
<div class="tank-bar-wrap"><div class="tank-bar" id="tankBar">-</div></div>
<div style="flex:1;min-width:200px">
<table class="stat-table">
<tr><th>Remaining Water</th><td id="water">-</td></tr>
<tr><th>24h Harvest</th><td id="harvest">-</td></tr>
<tr><th>24h Consumption</th><td id="consumed">-</td></tr>
<tr><th>Pump State</th><td id="pstate">-</td></tr>
<tr><th>Pump Current</th><td id="pcurrent">-</td></tr>
<tr><th>Air Temperature</th><td id="airtemp">-</td></tr>
</table>
<div class="row" style="margin-top:12px">
<button class="btn" onclick="pumpCmd('enable')">Enable Pump</button>
<button class="btn btn-del" onclick="pumpCmd('disable')">Disable Pump</button>
</div>
</div></div></div>

<div class="card">
<h2>History</h2>
<div class="tab-bar">
<button class="active" onclick="loadHist('24h',this)">24 h</button>
<button onclick="loadHist('7d',this)">7 days</button>
<button onclick="loadHist('30d',this)">30 days</button>
</div>
<div class="legend">
<span><i style="background:#4fc3f7"></i>Tank Level (%)</span>
<span><i style="background:#e6a117"></i>Consumption</span>
</div>
<canvas id="chart" height="200"></canvas>
</div>

<script>
function fetchStats(){
fetch('/stats.json').then(function(r){return r.json();}).then(function(d){
var pct=d.TANK.LVL/10;
document.getElementById('tankBar').style.height=pct+'%';
document.getElementById('tankBar').textContent=Math.round(pct)+'%';
document.getElementById('water').textContent=d.TANK.LVL+' L';
var sign=d.TANK.HARV>0?'+':'';
document.getElementById('harvest').textContent=sign+d.TANK.HARV+' L';
document.getElementById('consumed').textContent=d.TANK.CONS+' L';
document.getElementById('pstate').textContent=d.PUMP.STATETEXT;
document.getElementById('pcurrent').textContent=d.PUMP.CUR+' mA';
var at=document.getElementById('airtemp');
if(d.TANK.TEMP!==undefined)at.textContent=d.TANK.TEMP.toFixed(1)+' \u00b0C';
var pi=document.getElementById('pumpInd');
var ps=d.PUMP.STATE;
var pL={'-2':'Pump: DryRun','-1':'Pump: Off','0':'Pump: Idle','1':'Pump: Running','2':'Pump: Warning'};
var pC={'-2':'ind ind-err','-1':'ind ind-off','0':'ind ind-off','1':'ind ind-on','2':'ind ind-warn'};
pi.className=pC[String(ps)]||'ind ind-off';pi.textContent=pL[String(ps)]||'Pump: ?';
var ci=document.getElementById('connInd');
var nc=d.TCP_CLIENTS||0;
ci.className=nc>0?'ind ind-on':'ind ind-off';
ci.textContent='Clients: '+nc;
var h=d.HEALTH||{};
var si=document.getElementById('sdInd');
si.className=h.SD?'ind ind-on':'ind ind-err';
si.textContent=h.SD?'SD Card: OK':'SD Card: FAIL';
var se=document.getElementById('sensorInd');
se.className=h.SENSOR?'ind ind-on':'ind ind-err';
se.textContent=h.SENSOR?'Sensor: OK':'Sensor: FAIL';
}).catch(function(){});}

function pumpCmd(action){
fetch('/'+action+'_pump',{method:'POST'}).then(function(){setTimeout(fetchStats,500);}).catch(function(){});}

var histData=[],histMode='24h';
function loadHist(mode,btn){
histMode=mode;
document.querySelectorAll('.tab-bar button').forEach(function(b){b.className='';});
if(btn)btn.className='active';
var url=mode=='24h'?'/24h_history.json':'/last30days.json';
fetch(url).then(function(r){return r.json();}).then(function(data){
if(mode=='7d'){var d=new Date();d.setDate(d.getDate()-7);
data=data.filter(function(s){return new Date((s.TS-7200)*1000)>=d;});}
histData=data;drawHist();
}).catch(function(){});}

function drawHist(){
var c=document.getElementById('chart');if(!c)return;var ctx=c.getContext('2d');
var dpr=window.devicePixelRatio||1,rect=c.getBoundingClientRect();
c.width=rect.width*dpr;c.height=200*dpr;ctx.scale(dpr,dpr);
var W=rect.width,H=200,data=histData,n=data.length;
ctx.clearRect(0,0,W,H);ctx.fillStyle='#0d1b2a';ctx.fillRect(0,0,W,H);
ctx.strokeStyle='#222';ctx.lineWidth=0.5;
for(var i=0;i<=4;i++){var y=H*i/4;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(W,y);ctx.stroke();
ctx.fillStyle='#555';ctx.font='10px Arial';ctx.fillText(Math.round(100*(1-i/4))+'%',2,y+10);}
if(n<1)return;
// Time labels
var t0=data[0].TS,t1=data[n-1].TS;
ctx.fillStyle='#555';
for(var i=0;i<=6;i++){var x=W*i/6;
var ts=new Date((t0+(t1-t0)*i/6-7200)*1000);
var lbl;
if(histMode=='24h')lbl=('0'+ts.getHours()).slice(-2)+':'+('0'+ts.getMinutes()).slice(-2);
else lbl=(ts.getMonth()+1)+'/'+ts.getDate();
ctx.fillText(lbl,i<6?x+2:x-30,H-3);}
// Level line
ctx.strokeStyle='#4fc3f7';ctx.lineWidth=1.5;ctx.beginPath();
for(var i=0;i<n;i++){var x=n>1?i/(n-1)*W:W/2;var y=H-(data[i].LVL/1000*H);
if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);}ctx.stroke();
// Consumption bars
var maxCons=0;for(var i=0;i<n;i++){if(data[i].CONS>maxCons)maxCons=data[i].CONS;}
if(maxCons>0){ctx.fillStyle='rgba(230,161,23,0.4)';
var bw=Math.max(2,W/n*0.6);
for(var i=0;i<n;i++){var x=n>1?i/(n-1)*W:W/2;var bh=data[i].CONS/maxCons*(H*0.3);
ctx.fillRect(x-bw/2,H-bh,bw,bh);}}
}

window.addEventListener('resize',drawHist);
fetchStats();setInterval(fetchStats,3000);loadHist('24h',document.querySelector('.tab-bar button'));
</script></body></html>)rawliteral";

static const char LOG_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tank Log</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:monospace;max-width:960px;margin:0 auto;padding:10px;background:#1a1a2e;color:#e0e0e0;font-size:13px}
h1{color:#16c79a;margin:10px 0;font-size:1.5em;font-family:Arial,sans-serif}
.btn{background:#16c79a;color:#1a1a2e;border:none;padding:6px 14px;border-radius:4px;cursor:pointer;margin:4px;font-weight:bold;text-decoration:none;display:inline-block;font-family:Arial,sans-serif;font-size:0.8em}
.btn:hover{background:#1df0b0}
.btn-sec{background:#555;color:#fff}
.btn-sec:hover{background:#777}
#log{background:#0d1b2a;border-radius:6px;padding:10px;margin:10px 0;white-space:pre-wrap;word-wrap:break-word;max-height:80vh;overflow-y:auto}
.log-info{color:#4fc3f7}
.log-warn{color:#e6a117}
.log-err{color:#c73e1d}
</style></head><body>
<h1><a href="/" class="btn btn-sec" style="font-size:0.8em">&#8592; Dashboard</a> Tank Log</h1>
<div><label><input type="checkbox" id="autoRefresh" checked> Auto-refresh (3s)</label>
<button class="btn" onclick="fetchLog()">Refresh Now</button></div>
<div id="log">Loading...</div>
<script>
function fetchLog(){
fetch('/log.json').then(function(r){return r.json();}).then(function(lines){
var el=document.getElementById('log');
var html='';
for(var i=0;i<lines.length;i++){
var c='log-info';
if(lines[i].indexOf('[Error]')>=0)c='log-err';
else if(lines[i].indexOf('[Warning]')>=0)c='log-warn';
html+='<div class="'+c+'">'+lines[i].replace(/</g,'&lt;')+'</div>';
}
el.innerHTML=html||'<em>No log entries</em>';
el.scrollTop=el.scrollHeight;
}).catch(function(){});}
fetchLog();
setInterval(function(){if(document.getElementById('autoRefresh').checked)fetchLog();},3000);
</script></body></html>)rawliteral";

// ─── SD card JSON helpers (unchanged) ──────────────────────────────────────

static bool sendHistoryJson(String path)
{
    if (path.endsWith(".json") && SD.exists(path))
    {
        File file = SD.open(path, FILE_READ);
        if (file)
        {
            String header =
                String("HTTP/1.1 200 OK\r\n") +
                "Content-Type: text/json\r\n" +
                "Connection: close\r\n" +
                "\r\n";
            WiFiClient client = server.client();
            client.print(header + "[");
            uint8_t buf[256];
            while (file.available())
            {
                size_t len = file.read(buf, sizeof(buf));
                client.write(buf, len);
            }
            file.close();
            client.print("]");
            client.flush();
            client.stop();
            return true;
        }
    }
    return false;
}

static bool sendLast30daysJson(String path)
{
    bool ret = false;
    String filename;
    int data_offset;

    if (!path.endsWith("last30days.json"))
    {
        return false;
    }

    if (!tank_get_last_30days_file_and_offset(filename, data_offset))
    {
        Log.error("Failed to get 30 days offset");
        return false;
    }

    File file = SD.open(filename, FILE_READ);
    if (file)
    {
        if (file.seek(data_offset))
        {
            String header =
                String("HTTP/1.1 200 OK\r\n") +
                "Content-Type: text/json\r\n" +
                "Connection: close\r\n" +
                "\r\n";
            WiFiClient client = server.client();
            client.print(header + "[");
            uint8_t buf[256];
            while (file.available())
            {
                size_t len = file.read(buf, sizeof(buf));
                client.write(buf, len);
            }
            client.print("]");
            client.flush();
            client.stop();
            ret = true;
        }
        else
        {
            Log.error("Failed to set 30 days offset");
        }

        file.close();
    }
    else
    {
        Log.error("Failed to open 30 days file");
    }

    return ret;
}

void server_init()
{
    server.on("/", HTTP_GET, []() {
        server.send_P(200, PSTR("text/html"), INDEX_HTML);
    });

    server.on("/stats.json", HTTP_GET, []() {
        String json = "{\"TANK\":" + tank_get_stats_json();
        json += ",\"PUMP\":" + pump_get_stats_json();
        json += ",\"TCP_CLIENTS\":" + String(tcp_server_connected_count());
        json += ",\"HEALTH\":" + tank_get_health_json() + "}";
        server.send(200, "text/json", json);
    });

    server.on("/24h_history.json", HTTP_GET, []() {
        server.send(200, "text/json", tank_get_last_24h_json());
    });

    server.on("/enable_pump", HTTP_POST, []() {
        pump_enable();
        server.send(200, "text/plain", "OK");
    });

    server.on("/disable_pump", HTTP_POST, []() {
        pump_disable();
        server.send(200, "text/plain", "OK");
    });

    server.on("/log", HTTP_GET, []() {
        server.send_P(200, PSTR("text/html"), LOG_HTML);
    });

    server.on("/log.json", HTTP_GET, []() {
        String json = "[";
        int n = Log.ringCount();
        for (int i = 0; i < n; i++)
        {
            const char *entry = Log.ringEntry(i);
            if (!entry) continue;
            if (i > 0) json += ",";
            // JSON-escape the string
            String escaped = String(entry);
            escaped.replace("\\", "\\\\");
            escaped.replace("\"", "\\\"");
            json += "\"" + escaped + "\"";
        }
        json += "]";
        server.send(200, "application/json", json);
    });

    // SD card history files (last30days, monthly, yearly)
    server.onNotFound([]() {
        String uri = server.uri();
        if (sendLast30daysJson(uri))
            return;
        if (sendHistoryJson(uri))
            return;
        server.send(404, "text/plain", "404: Not Found");
    });

    server.begin();
    Log.info("Server started");
}

void server_handle()
{
    server.handleClient();
}
