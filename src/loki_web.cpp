// =============================================================================
// Loki CYD — Web UI Server
// Single-page dashboard for monitoring and control
// =============================================================================

#include "loki_web.h"
#include "loki_config.h"
#include "loki_types.h"
#include "loki_recon.h"
#include "loki_score.h"
#include "loki_pet.h"
#include "loki_storage.h"
#include "loki_sprites.h"
#include <SD.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <Preferences.h>

extern TFT_eSPI tft;

namespace LokiWeb {

static WebServer* server = nullptr;
static bool webRunning = false;
static String scanTarget = "";  // Custom target IP/subnet

// =============================================================================
// SPA HTML (PROGMEM)
// =============================================================================

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LOKI CYD</title>
<style>
:root{--bg:#0a0f0a;--sf:#141e14;--el:#1e2e1e;--tx:#c8e0c8;--td:#5a7a5a;--ac:#4ebd35;--ab:#7ed860;--hi:#ffc800;--al:#ff40a0;--er:#ff3030;--ok:#40d840;--cr:#ff60b0}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--tx);font:13px/1.4 'Courier New',monospace;overflow-x:hidden}
nav{display:flex;background:var(--sf);border-bottom:1px solid var(--el);position:sticky;top:0;z-index:9}
nav a{flex:1;text-align:center;padding:10px 4px;color:var(--td);text-decoration:none;font-size:11px;border-bottom:2px solid transparent;transition:.2s}
nav a.on{color:var(--ac);border-color:var(--ac)}
nav a:hover{color:var(--ab)}
.tab{display:none;padding:12px}
.tab.on{display:block}
h2{color:var(--ac);font-size:14px;margin-bottom:10px;text-transform:uppercase;letter-spacing:1px}
h3{color:var(--ab);font-size:12px;margin:10px 0 6px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(90px,1fr));gap:6px;margin-bottom:14px}
.st{background:var(--sf);border:1px solid var(--el);border-radius:4px;padding:8px 6px;text-align:center}
.st .v{font-size:18px;color:var(--ab);font-weight:bold}
.st .l{font-size:9px;color:var(--td);margin-top:2px}
.btn{background:var(--el);color:var(--ac);border:1px solid var(--ac);padding:8px 16px;font:12px monospace;cursor:pointer;border-radius:3px}
.btn:hover{background:var(--ac);color:var(--bg)}
.btn.r{border-color:var(--er);color:var(--er)}
.btn.r:hover{background:var(--er);color:#fff}
.btn.sm{padding:4px 10px;font-size:11px}
#log{background:var(--sf);border:1px solid var(--el);border-radius:4px;padding:8px;height:260px;overflow-y:auto;font-size:11px;line-height:1.5;white-space:pre-wrap;word-break:break-all}
.hl{display:flex;gap:8px;margin-bottom:14px;flex-wrap:wrap;align-items:center}
table{width:100%;border-collapse:collapse;font-size:11px}
th{background:var(--sf);color:var(--ac);text-align:left;padding:6px;border-bottom:1px solid var(--el);position:sticky;top:0}
td{padding:5px 6px;border-bottom:1px solid var(--el)}
tr:hover{background:var(--sf)}
.dev{background:var(--sf);border:1px solid var(--el);border-radius:4px;margin-bottom:6px;cursor:pointer}
.dh{padding:8px;display:flex;justify-content:space-between;align-items:center}
.dh .ip{color:var(--ab);font-weight:bold}
.dh .vn{color:var(--td);font-size:11px}
.dh .tp{font-size:10px;padding:2px 6px;border-radius:2px;background:var(--el);color:var(--ac)}
.dd{display:none;padding:0 8px 8px;font-size:11px;border-top:1px solid var(--el)}
.dd.on{display:block}
.dd p{margin:3px 0}
.dd .k{color:var(--td)}
.dd .cv{color:var(--cr)}
.tag{display:inline-block;padding:1px 5px;margin:1px;border-radius:2px;background:var(--el);font-size:10px}
.tag.cr{background:#3a1020;color:var(--cr);border:1px solid var(--cr)}
.tag.op{border:1px solid var(--ac)}
.tag.sc{background:#1a2a10;color:var(--ok)}
.tag.lo{background:#2a1a10;color:var(--hi)}
.si{text-align:center;margin:10px 0}
.si img{max-width:100%;border:1px solid var(--el);border-radius:4px}
.inf{background:var(--sf);border:1px solid var(--el);border-radius:4px;padding:10px;margin-bottom:10px}
.inf p{margin:4px 0}
.inf .k{color:var(--td);min-width:80px;display:inline-block}
input[type=text],input[type=password],select{background:var(--el);color:var(--tx);border:1px solid var(--ac);padding:6px 8px;font:12px monospace;border-radius:3px;width:100%}
select{cursor:pointer}.row{display:flex;gap:6px;align-items:center;margin-bottom:6px}
.row input{flex:1}.wnet{background:var(--el);border:1px solid var(--sf);padding:6px 8px;margin:2px 0;border-radius:3px;cursor:pointer;font-size:11px;display:flex;justify-content:space-between}
.wnet:hover{border-color:var(--ac)}.wnet .rs{color:var(--td);font-size:10px}
.bgrp{display:flex;gap:4px}.bgrp .btn{flex:1;padding:6px 4px;text-align:center}
.bgrp .btn.act{background:var(--ac);color:var(--bg)}.dl-btns{display:flex;gap:8px;margin:10px 0;flex-wrap:wrap}.dl-btns a{text-decoration:none}
.kf-cr{color:var(--cr)}.kf-ok{color:var(--ok)}.kf-at{color:var(--al)}.kf-xp{color:var(--hi)}.kf-er{color:var(--er)}.kf-fn{color:var(--ab)}.kf-dm{color:var(--td)}.stt{display:inline-block;padding:2px 6px;border-radius:2px;font-size:10px}
.s-found{background:#1a2a10;color:var(--ok)}.s-scanning{background:#1a1a30;color:#60a0ff}.s-testing{background:#2a1a10;color:var(--hi)}.s-cracked{background:#3a1020;color:var(--cr)}.s-open{background:#10302a;color:#40e0d0}.s-locked{background:#1a1a1a;color:#888}
@media(max-width:400px){.grid{grid-template-columns:repeat(3,1fr)}.st .v{font-size:15px}nav a{font-size:10px;padding:8px 2px}}
</style>
</head><body>
<nav id="nv">
<a href="#" data-t="dash" class="on">Dashboard</a>
<a href="#" data-t="hosts">Hosts</a>
<a href="#" data-t="loot">Loot</a>
<a href="#" data-t="settings">Settings</a>
<a href="#" data-t="display">Display</a>
</nav>

<div id="dash" class="tab on">
<div class="grid" id="sg"></div>
<div class="hl">
<input type="text" id="tgtIn" placeholder="Target IP/subnet (optional)" style="width:200px">
<button class="btn" id="scnBtn" onclick="toggleScan()">Start Scan</button>
<span id="scnSt" style="color:var(--td);font-size:11px"></span>
</div>
<div id="tgtInfo" style="font-size:11px;color:var(--td);margin-bottom:8px"></div>
<h2>Attack Log</h2>
<div id="log"></div>
</div>

<div id="hosts" class="tab">
<h2>Discovered Hosts</h2>
<div id="hl"></div>
</div>

<div id="loot" class="tab">
<h2>Credentials</h2>
<table><thead><tr><th>IP</th><th>Port</th><th>User</th><th>Pass</th></tr></thead><tbody id="ct"></tbody></table>
<h3>Downloads</h3>
<div class="dl-btns">
<a href="/creds" download="creds.json"><button class="btn sm">Credentials</button></a>
<a href="/hosts" download="hosts.json"><button class="btn sm">Hosts</button></a>
<a href="/log" download="attack_log.txt"><button class="btn sm">Attack Log</button></a>
</div>
</div>

<div id="settings" class="tab">
<h2>Settings</h2>
<div class="inf">
<h3>WiFi</h3>
<div id="wifiInfo"></div>
<button class="btn sm" onclick="scanWifi()" id="wscBtn">Scan Networks</button>
<div id="wlist" style="margin-top:6px"></div>
<div id="wconn" style="display:none;margin-top:6px">
<div class="row"><span style="color:var(--td);font-size:11px" id="wsel"></span></div>
<div class="row"><input type="password" id="wpass" placeholder="Password"></div>
<button class="btn sm" onclick="connWifi()">Connect</button>
<span id="wcst" style="font-size:11px;margin-left:6px"></span>
</div>
</div>
<div class="inf">
<h3>Theme</h3>
<div class="row"><select id="thSel" onchange="setTheme()"><option value="">Loading...</option></select></div>
<span id="thSt" style="font-size:11px;color:var(--td)"></span>
</div>
<div class="inf">
<h3>Brightness</h3>
<div class="bgrp">
<button class="btn sm" onclick="setBrt(25)">25%</button>
<button class="btn sm" onclick="setBrt(50)">50%</button>
<button class="btn sm" onclick="setBrt(75)">75%</button>
<button class="btn sm" onclick="setBrt(100)">100%</button>
</div>
</div>
<div class="inf" id="sysinfo"></div>
</div>

<div id="display" class="tab">
<h2>Live Display</h2>
<div class="hl"><button class="btn" onclick="refScr()">Refresh</button></div>
<div class="si"><img id="scr" alt="Display"></div>
</div>

<script>
var at='dash',iv={},run=false,curTgt='';
var sn=['Targets','Ports','Vulns','Creds','Zombies','Data','NetKB','Level','Gold','Attacks'];

function $(s){return document.getElementById(s)}
function qa(s){return document.querySelectorAll(s)}

qa('#nv a').forEach(function(a){a.onclick=function(e){
e.preventDefault();
qa('#nv a').forEach(function(x){x.classList.remove('on')});
qa('.tab').forEach(function(x){x.classList.remove('on')});
a.classList.add('on');at=a.dataset.t;$(at).classList.add('on');
stopPolls();startPolls();
}});

function stopPolls(){for(var k in iv){clearInterval(iv[k]);delete iv[k]}}

function startPolls(){
if(at==='dash'){updStats();updLog();iv.s=setInterval(updStats,3000);iv.l=setInterval(updLog,2000)}
else if(at==='hosts'){fetchHosts();iv.h=setInterval(fetchHosts,10000)}
else if(at==='loot'){fetchCreds()}
else if(at==='settings'){fetchSettings();fetchThemes()}
else if(at==='display'){refScr()}
}

function updStats(){
fetch('/stats').then(function(r){return r.json()}).then(function(d){
run=d.running;curTgt=d.target||'';
$('scnBtn').textContent=run?'Stop Scan':'Start Scan';
$('scnBtn').className=run?'btn r':'btn';
$('scnSt').textContent=run?'Scanning...':'Idle';
$('tgtInfo').textContent=curTgt?'Target: '+curTgt:'';
var z=d.cracked,nk=d.hosts,lv=Math.floor(nk*0.1+d.cracked*0.2+d.files*0.1+z*0.5+d.scans+d.vulns*0.01);
var vals=[d.hosts,d.ports,d.vulns,d.cracked,z,d.files,nk,lv,d.xp,d.scans];
var h='';for(var i=0;i<10;i++)h+='<div class="st"><div class="v">'+vals[i]+'</div><div class="l">'+sn[i]+'</div></div>';
$('sg').innerHTML=h;
}).catch(function(){})
}

var lastLog='';
function updLog(){
fetch('/log').then(function(r){return r.text()}).then(function(t){
if(t===lastLog)return;lastLog=t;
var el=$('log'),lines=t.trim().split('\n'),h='';
for(var i=0;i<lines.length;i++){
var l=lines[i],c='';
if(l.indexOf('CRACKED')>-1||l.indexOf('!!!')>-1)c='kf-cr';
else if(l.indexOf('XP')>-1||l.indexOf('xp')>-1||l.indexOf('Score')>-1)c='kf-xp';
else if(l.indexOf('[+]')>-1)c='kf-fn';
else if(l.indexOf('[>]')>-1)c='kf-at';
else if(l.indexOf('[!]')>-1&&(l.indexOf('fail')>-1||l.indexOf('Fail')>-1||l.indexOf('ERROR')>-1))c='kf-er';
else if(l.indexOf('[*]')>-1)c='kf-ok';
else if(l.indexOf('blocked')>-1||l.indexOf('locked')>-1)c='kf-dm';
h+='<div class="'+c+'">'+esc(l)+'</div>';
}
el.innerHTML=h;el.scrollTop=el.scrollHeight;
}).catch(function(){})
}

var stNames=['Found','Scanning','Testing','Cracked','Open','Locked'];
var stCls=['s-found','s-scanning','s-testing','s-cracked','s-open','s-locked'];
var dtNames=['?','Phone','Laptop','Router','Camera','NAS','Printer','TV','IoT','Speaker','Gaming','Server','VM','Other'];

function fetchHosts(){
fetch('/hosts').then(function(r){return r.json()}).then(function(devs){
var h='';
for(var i=0;i<devs.length;i++){var d=devs[i];
var sc=d.status<stCls.length?stCls[d.status]:'';
var sl=d.status<stNames.length?stNames[d.status]:'?';
h+='<div class="dev" onclick="this.querySelector(\'.dd\').classList.toggle(\'on\')">';
h+='<div class="dh"><span class="ip">'+esc(d.ip)+'</span>';
h+='<span class="vn">'+esc(d.vendor||'?')+'</span>';
h+='<span class="tp">'+dtNames[d.type||0]+'</span>';
h+='<span class="stt '+sc+'">'+sl+'</span></div>';
h+='<div class="dd">';
h+='<p><span class="k">MAC:</span> '+esc(d.mac)+'</p>';
if(d.banner)h+='<p><span class="k">Banner:</span> '+esc(d.banner)+'</p>';
if(d.openPorts&&d.openPorts.length){h+='<p><span class="k">Ports:</span> ';for(var j=0;j<d.openPorts.length;j++)h+='<span class="tag op">'+d.openPorts[j]+'</span>';h+='</p>'}
if(d.crackedUser)h+='<p><span class="k">Creds:</span> <span class="cv">'+esc(d.crackedUser)+':'+esc(d.crackedPass)+'</span></p>';
if(d.crackedPorts&&d.crackedPorts.length){h+='<p><span class="k">Cracked on:</span> ';for(var j=0;j<d.crackedPorts.length;j++)h+='<span class="tag cr">'+d.crackedPorts[j]+'</span>';h+='</p>'}
h+='</div></div>';
}
if(!devs.length)h='<p style="color:var(--td)">No devices found yet.</p>';
$('hl').innerHTML=h;
}).catch(function(){})
}

function fetchCreds(){
fetch('/creds').then(function(r){return r.json()}).then(function(creds){
var h='';for(var i=0;i<creds.length;i++){var c=creds[i];
h+='<tr><td>'+esc(c.ip)+'</td><td>'+c.port+'</td><td class="cv">'+esc(c.user)+'</td><td class="cv">'+esc(c.pass)+'</td></tr>';}
if(!creds.length)h='<tr><td colspan=4 style="color:var(--td)">No credentials yet</td></tr>';
$('ct').innerHTML=h;
}).catch(function(){})
}

function fetchSettings(){
fetch('/stats').then(function(r){return r.json()}).then(function(d){
$('wifiInfo').innerHTML='<p><span class="k">SSID:</span> '+(d.ssid||'N/A')+'</p><p><span class="k">IP:</span> '+(d.ip||'N/A')+'</p><p><span class="k">Signal:</span> '+(d.rssi||'?')+' dBm</p>';
$('sysinfo').innerHTML='<h3>System</h3><p><span class="k">Version:</span> '+(d.version||'?')+'</p><p><span class="k">Free Heap:</span> '+(d.freeHeap||'?')+' bytes</p><p><span class="k">Status:</span> '+(d.running?'<span style="color:var(--ok)">Scanning</span>':'<span style="color:var(--td)">Idle</span>')+'</p>';
}).catch(function(){})
}

function fetchThemes(){
fetch('/themes').then(function(r){return r.json()}).then(function(themes){
var sel=$('thSel'),h='';
for(var i=0;i<themes.length;i++){
h+='<option value="'+esc(themes[i].name)+'"'+(themes[i].active?' selected':'')+'>'+esc(themes[i].display)+'</option>';
}
sel.innerHTML=h;
}).catch(function(){})
}

function setTheme(){
var v=$('thSel').value;if(!v)return;
$('thSt').textContent='Switching...';
fetch('/theme/set?name='+encodeURIComponent(v),{method:'POST'}).then(function(r){return r.text()}).then(function(t){
$('thSt').textContent=t;setTimeout(function(){$('thSt').textContent=''},3000);
}).catch(function(){$('thSt').textContent='Error'})
}

function setBrt(l){
fetch('/brightness?level='+l,{method:'POST'}).then(function(){
qa('.bgrp .btn').forEach(function(b){b.classList.remove('act')});
event.target.classList.add('act');
}).catch(function(){})
}

function scanWifi(){
$('wscBtn').textContent='Scanning...';$('wscBtn').disabled=true;
fetch('/wifi/scan').then(function(r){return r.json()}).then(function(nets){
var h='';
for(var i=0;i<nets.length;i++){
var n=nets[i],lock=n.encryption>0?'*':'';
h+='<div class="wnet" onclick="pickWifi(\''+esc(n.ssid).replace(/'/g,"\\'")+'\')">';
h+='<span>'+lock+' '+esc(n.ssid)+'</span>';
h+='<span class="rs">'+n.rssi+'dBm ch'+n.channel+'</span></div>';
}
if(!nets.length)h='<p style="color:var(--td)">No networks found</p>';
$('wlist').innerHTML=h;
$('wscBtn').textContent='Scan Networks';$('wscBtn').disabled=false;
}).catch(function(){$('wscBtn').textContent='Scan Networks';$('wscBtn').disabled=false})
}

function pickWifi(ssid){
$('wsel').textContent='Network: '+ssid;
$('wconn').style.display='block';$('wconn').dataset.ssid=ssid;$('wpass').value='';$('wcst').textContent='';
}

function connWifi(){
var ssid=$('wconn').dataset.ssid,pass=$('wpass').value;
$('wcst').textContent='Connecting...';
fetch('/wifi/connect?ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass),{method:'POST'}).then(function(r){return r.text()}).then(function(t){
$('wcst').textContent=t;if(t.indexOf('OK')>-1)setTimeout(fetchSettings,3000);
}).catch(function(){$('wcst').textContent='Error'})
}

function toggleScan(){
var tgt=$('tgtIn').value.trim();
var url=run?'/stop':'/start';
if(!run&&tgt)url='/start?target='+encodeURIComponent(tgt);
fetch(url).then(function(){setTimeout(updStats,500)})
}

function refScr(){$('scr').src='/screenshot?t='+Date.now()}

function esc(s){if(!s)return'';var d=document.createElement('div');d.textContent=s;return d.innerHTML}

startPolls();
</script>
</body></html>)rawliteral";

// =============================================================================
// ROUTES
// =============================================================================

static void handleRoot() {
    server->send_P(200, "text/html", INDEX_HTML);
}

static void handleStart() {
    if (!LokiRecon::isRunning()) {
        if (server->hasArg("target")) {
            scanTarget = server->arg("target");
            Serial.printf("[WEB] Scan target set: %s\n", scanTarget.c_str());
        }
        LokiRecon::start();
        LokiPet::setMood(MOOD_SCANNING);
    }
    server->send(200, "text/plain", "OK");
}

static void handleStop() {
    LokiRecon::stop();
    server->send(200, "text/plain", "OK");
}

static void handleStats() {
    LokiScore score = LokiScoreManager::get();
    String json = "{";
    json += "\"hosts\":" + String(score.hostsFound) + ",";
    json += "\"ports\":" + String(score.portsFound) + ",";
    json += "\"cracked\":" + String(score.servicesCracked) + ",";
    json += "\"files\":" + String(score.filesStolen) + ",";
    json += "\"vulns\":" + String(score.vulnsFound) + ",";
    json += "\"scans\":" + String(score.totalScans) + ",";
    json += "\"xp\":" + String(score.xp) + ",";
    json += "\"running\":" + String(LokiRecon::isRunning() ? "true" : "false") + ",";
    // WiFi and theme info for settings tab
    json += "\"ssid\":\"" + String(WiFi.SSID()) + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"theme\":\"" + String(LokiSprites::getThemeConfig().name) + "\",";
    json += "\"version\":\"" LOKI_VERSION "\",";
    json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"target\":\"" + scanTarget + "\"";
    json += "}";
    server->send(200, "application/json", json);
}

static void handleCreds() {
    int count = LokiRecon::getCredLogCount();
    LokiCredEntry* creds = LokiRecon::getCredLog();

    String json = "[";
    for (int i = 0; i < count; i++) {
        if (i > 0) json += ",";
        char ip[16];
        snprintf(ip, sizeof(ip), "%d.%d.%d.%d", creds[i].ip[0], creds[i].ip[1], creds[i].ip[2], creds[i].ip[3]);
        json += "{\"ip\":\"" + String(ip) + "\",\"port\":" + String(creds[i].port) +
                ",\"user\":\"" + String(creds[i].user) + "\",\"pass\":\"" + String(creds[i].pass) + "\"}";
    }
    json += "]";
    server->send(200, "application/json", json);
}

static void handleLog() {
    int count = LokiPet::getKillFeedCount();
    String text = "";
    for (int i = 0; i < count; i++) {
        char line[52];
        uint16_t color;
        LokiPet::getKillFeedLine(i, line, sizeof(line), &color);
        text += String(line) + "\n";
    }
    server->send(200, "text/plain", text);
}

static void handleHosts() {
    static const uint16_t scanPorts[] = LOKI_SCAN_PORTS;
    static const int numPorts = sizeof(scanPorts) / sizeof(scanPorts[0]);

    int count = LokiRecon::getDeviceCount();
    LokiDevice* devs = LokiRecon::getDevices();

    String json = "[";
    for (int i = 0; i < count; i++) {
        if (i > 0) json += ",";
        LokiDevice& d = devs[i];

        char ip[16];
        snprintf(ip, sizeof(ip), "%d.%d.%d.%d", d.ip[0], d.ip[1], d.ip[2], d.ip[3]);

        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);

        json += "{\"ip\":\"" + String(ip) + "\"";
        json += ",\"mac\":\"" + String(mac) + "\"";
        json += ",\"vendor\":\"" + String(d.vendor) + "\"";
        json += ",\"type\":" + String(d.type);
        json += ",\"status\":" + String(d.status);
        json += ",\"banner\":\"" + String(d.banner) + "\"";

        // Open ports as array of port numbers
        json += ",\"openPorts\":[";
        bool first = true;
        for (int p = 0; p < numPorts; p++) {
            if (d.openPorts & (1 << p)) {
                if (!first) json += ",";
                json += String(scanPorts[p]);
                first = false;
            }
        }
        json += "]";

        // Cracked creds
        if (d.credUser[0]) {
            json += ",\"crackedUser\":\"" + String(d.credUser) + "\"";
            json += ",\"crackedPass\":\"" + String(d.credPass) + "\"";
        }

        // Cracked ports as array
        json += ",\"crackedPorts\":[";
        first = true;
        for (int p = 0; p < numPorts; p++) {
            if (d.crackedPorts & (1 << p)) {
                if (!first) json += ",";
                json += String(scanPorts[p]);
                first = false;
            }
        }
        json += "]";

        json += "}";
    }
    json += "]";
    server->send(200, "application/json", json);
}

static void handleFiles() {
    server->send(200, "application/json", LokiStorage::listFiles());
}

static void handleDownload() {
    if (!server->hasArg("file")) {
        server->send(400, "text/plain", "Missing ?file= parameter");
        return;
    }
    String filename = server->arg("file");
    if (!filename.startsWith("/")) filename = "/" + filename;

    if (!SPIFFS.exists(filename)) {
        server->send(404, "text/plain", "File not found");
        return;
    }

    File f = SPIFFS.open(filename, FILE_READ);
    if (!f) {
        server->send(500, "text/plain", "Failed to open file");
        return;
    }

    String contentType = "application/octet-stream";
    if (filename.endsWith(".json")) contentType = "application/json";
    else if (filename.endsWith(".txt")) contentType = "text/plain";

    server->streamFile(f, contentType);
    f.close();
}

static void handleScreenshot() {
    int w = SCREEN_WIDTH;
    int h = SCREEN_HEIGHT;

    // BMP header: 14 + 40 + 12 (BI_BITFIELDS) = 66 bytes
    int rowSize = ((w * 2 + 3) / 4) * 4;
    int dataSize = rowSize * h;
    int fileSize = 66 + dataSize;

    // Send BMP header
    uint8_t header[66] = {0};
    header[0] = 'B'; header[1] = 'M';
    *(uint32_t*)&header[2] = fileSize;
    *(uint32_t*)&header[10] = 66;  // data offset
    *(uint32_t*)&header[14] = 40;  // DIB header size
    *(int32_t*)&header[18] = w;
    *(int32_t*)&header[22] = -h;   // top-down
    *(uint16_t*)&header[26] = 1;   // planes
    *(uint16_t*)&header[28] = 16;  // bpp
    *(uint32_t*)&header[30] = 3;   // BI_BITFIELDS
    *(uint32_t*)&header[34] = dataSize;
    // Color masks
    *(uint32_t*)&header[54] = 0xF800; // R
    *(uint32_t*)&header[58] = 0x07E0; // G
    *(uint32_t*)&header[62] = 0x001F; // B

    server->setContentLength(fileSize);
    server->send(200, "image/bmp", "");

    // Send header
    server->client().write(header, 66);

    // Read screen row by row, swap bytes for BMP format, and send
    uint16_t rowBuf[w];
    for (int y = 0; y < h; y++) {
        tft.readRect(0, y, w, 1, rowBuf);
        // Swap bytes — TFT returns big-endian, BMP needs little-endian
        for (int x = 0; x < w; x++) {
            rowBuf[x] = (rowBuf[x] >> 8) | (rowBuf[x] << 8);
        }
        server->client().write((uint8_t*)rowBuf, rowSize);
    }
}

// =============================================================================
// NEW API ENDPOINTS
// =============================================================================

static void handleWifiScan() {
    // Scan without disconnecting from current network
    int n = WiFi.scanNetworks(false, false);
    String json = "[";
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        // Escape SSID quotes
        String ssid = WiFi.SSID(i);
        ssid.replace("\"", "\\\"");
        json += "{\"ssid\":\"" + ssid + "\"";
        json += ",\"rssi\":" + String(WiFi.RSSI(i));
        json += ",\"encryption\":" + String(WiFi.encryptionType(i));
        json += ",\"channel\":" + String(WiFi.channel(i));
        json += "}";
    }
    json += "]";
    WiFi.scanDelete();
    server->send(200, "application/json", json);
}

static void handleWifiConnect() {
    if (!server->hasArg("ssid")) {
        server->send(400, "text/plain", "Missing ssid parameter");
        return;
    }
    String ssid = server->arg("ssid");
    String pass = server->hasArg("pass") ? server->arg("pass") : "";

    Serial.printf("[WEB] WiFi connect: %s\n", ssid.c_str());

    WiFi.begin(ssid.c_str(), pass.c_str());

    // Wait up to 10 seconds for connection
    int timeout = 20;  // 20 * 500ms = 10s
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(500);
        timeout--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        // Save credentials to NVS
        Preferences p;
        p.begin("loki", false);
        p.putString("wifi_ssid", ssid);
        p.putString("wifi_pass", pass);
        p.end();
        server->send(200, "text/plain", "OK - Connected to " + ssid + " (" + WiFi.localIP().toString() + ")");
    } else {
        server->send(200, "text/plain", "FAIL - Could not connect to " + ssid);
    }
}

static void handleThemes() {
    int count = LokiSprites::getThemeCount();
    String currentTheme = String(LokiSprites::getThemeConfig().name);
    String json = "[";
    for (int i = 0; i < count; i++) {
        if (i > 0) json += ",";
        String fname = String(LokiSprites::getThemeName(i));
        String dname = String(LokiSprites::getThemeDisplayName(i));
        dname.replace("\"", "\\\"");
        json += "{\"name\":\"" + fname + "\",\"display\":\"" + dname + "\"";
        // Mark active theme
        if (dname == currentTheme || fname == currentTheme) {
            json += ",\"active\":true";
        }
        json += "}";
    }
    json += "]";
    server->send(200, "application/json", json);
}

static void handleThemeSet() {
    if (!server->hasArg("name")) {
        server->send(400, "text/plain", "Missing name parameter");
        return;
    }
    String name = server->arg("name");
    Serial.printf("[WEB] Theme set: %s\n", name.c_str());

    if (LokiSprites::loadTheme(name.c_str())) {
        // Save to NVS
        Preferences p;
        p.begin("loki", false);
        p.putString("theme", name);
        p.end();
        server->send(200, "text/plain", "OK - Theme: " + name);
    } else {
        server->send(200, "text/plain", "FAIL - Theme not found: " + name);
    }
}

static void handleBrightness() {
    if (!server->hasArg("level")) {
        server->send(400, "text/plain", "Missing level parameter");
        return;
    }
    int level = server->arg("level").toInt();
    if (level < 0) level = 0;
    if (level > 100) level = 100;

    int duty = level * 255 / 100;
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL_PIN, 0);
    ledcWrite(0, duty);

    Serial.printf("[WEB] Brightness: %d%% (duty=%d)\n", level, duty);
    server->send(200, "text/plain", "OK");
}

// =============================================================================
// LIFECYCLE
// =============================================================================

void setup() {
    if (server) return;

    server = new WebServer(80);
    server->on("/", handleRoot);
    server->on("/start", handleStart);
    server->on("/stop", handleStop);
    server->on("/stats", handleStats);
    server->on("/screenshot", handleScreenshot);
    server->on("/creds", handleCreds);
    server->on("/log", handleLog);
    server->on("/hosts", handleHosts);
    server->on("/files", handleFiles);
    server->on("/download", handleDownload);
    // New endpoints
    server->on("/wifi/scan", HTTP_GET, handleWifiScan);
    server->on("/wifi/connect", HTTP_POST, handleWifiConnect);
    server->on("/themes", HTTP_GET, handleThemes);
    server->on("/theme/set", HTTP_POST, handleThemeSet);
    server->on("/brightness", HTTP_POST, handleBrightness);
    server->begin();
    webRunning = true;

    Serial.printf("[WEB] Server started on http://%s/\n", WiFi.localIP().toString().c_str());
}

void loop() {
    if (server && webRunning) {
        server->handleClient();
    }
}

void stop() {
    if (server) {
        server->stop();
        delete server;
        server = nullptr;
    }
    webRunning = false;
}

bool isRunning() { return webRunning; }

}  // namespace LokiWeb
