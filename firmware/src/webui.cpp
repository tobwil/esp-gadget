#include "webui.h"
#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>
#include "config.h"
#include "favs.h"
#include "hw.h"
#include "keys.h"
#include "assist.h"
#include "kids.h"
#include "n8n.h"
#include "power.h"
#include "music.h"
#include "net.h"
#include "radio.h"
#include "settings.h"
#include "ui/ui.h"
#include "voice.h"

extern const char TF_BOARD_TAG[];  // main.cpp

namespace webui {

static WebServer* srv = nullptr;

// Für das Handy gebaut: eine Spalte, große Schalter, unten die Reiter. Hell und dunkel nach Systemeinstellung.
static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0d0d0f">
<title>Taschenfunk</title>
<style>
:root{--bg:#f4f2ee;--card:#fff;--text:#1d1c1a;--dim:#77736c;--line:#e6e2da;--acc:#c98a2b;--kid:#e0562e;--ok:#1f9d6b;--bad:#d64541}
@media (prefers-color-scheme:dark){:root{--bg:#0d0d0f;--card:#18181c;--text:#f4f1ea;--dim:#8e8a82;--line:#2a2a30;--acc:#e0b25e;--kid:#ff8a5c;--ok:#3ddc97;--bad:#ff6b5f}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 -apple-system,system-ui,Inter,sans-serif}
header{position:sticky;top:0;z-index:2;background:var(--bg);padding:calc(10px + env(safe-area-inset-top)) 16px 10px;border-bottom:1px solid var(--line)}
header h1{font-size:20px;margin:0}header p{margin:0;color:var(--dim);font-size:14px}
.badge[hidden]{display:none}h2+.row{border-top:0}.badge{display:inline-block;font-size:12px;font-weight:600;padding:2px 8px;border-radius:99px;background:var(--kid);color:#fff;margin-left:6px;vertical-align:middle}
main{max-width:560px;margin:0 auto;padding:14px 12px calc(90px + env(safe-area-inset-bottom))}
section{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:14px 16px;margin-bottom:12px}
h2{font-size:15px;margin:0 0 6px;color:var(--acc);font-weight:650}.kidc h2{color:var(--kid)}
p.note{color:var(--dim);font-size:14px;margin:6px 0}
label.f{display:block;font-size:13px;color:var(--dim);margin:12px 0 4px}
input,select{width:100%;min-height:46px;padding:10px 12px;border-radius:12px;border:1px solid var(--line);background:var(--bg);color:var(--text);font:inherit;font-size:16px}
button{font:inherit;font-weight:600;border:0;border-radius:12px;min-height:46px;padding:10px 16px;background:var(--acc);color:#000;cursor:pointer}
button.wide{width:100%;margin-top:12px}button.ghost{background:transparent;color:var(--text);border:1px solid var(--line);min-height:40px;padding:6px 12px;font-weight:500}
button.on{background:var(--ok);color:#fff;border-color:var(--ok)}button.kid{background:var(--kid);color:#fff}
.row{display:flex;align-items:center;gap:10px;min-height:56px;padding:6px 0;border-top:1px solid var(--line)}
.row:first-child{border-top:0}.row .t{flex:1;min-width:0}.row .t b{display:block;font-weight:500;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.row small{color:var(--dim);display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sw{position:relative;width:52px;height:32px;flex:none}.sw input{opacity:0;position:absolute;inset:0;margin:0;min-height:0;z-index:1;cursor:pointer}
.sw span{position:absolute;inset:0;border-radius:99px;background:var(--line);transition:.2s}.sw span:after{content:"";position:absolute;left:3px;top:3px;width:26px;height:26px;border-radius:50%;background:#fff;transition:.2s;box-shadow:0 1px 3px #0004}
.sw input:checked+span{background:var(--kid)}.angry .sw input:checked+span{background:#d63a2e}.sw input:checked+span:after{transform:translateX(20px)}
.two{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.seg{display:flex;background:var(--bg);border:1px solid var(--line);border-radius:12px;padding:3px;margin-top:10px}
.seg button{flex:1;background:transparent;color:var(--dim);min-height:38px;padding:4px}.seg button.sel{background:var(--card);color:var(--text);box-shadow:0 1px 3px #0002}
.kv{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;font-size:15px}.kv span:nth-child(odd){color:var(--dim)}
.ok{color:var(--ok)}.bad{color:var(--bad)}
input[type=range]{padding:0;border:0;background:none;accent-color:var(--kid)}
nav{position:fixed;left:0;right:0;bottom:0;z-index:3;display:flex;background:var(--card);border-top:1px solid var(--line);padding-bottom:env(safe-area-inset-bottom)}
nav button{flex:1;background:none;color:var(--dim);border-radius:0;min-height:62px;font-size:12px;font-weight:500;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px}
nav button i{font-style:normal;font-size:22px;line-height:1}nav button.sel{color:var(--acc)}nav button.sel.k{color:var(--kid)}
.tab{display:none}.tab.sel{display:block}
#msg{position:fixed;left:50%;bottom:calc(76px + env(safe-area-inset-bottom));transform:translateX(-50%);background:var(--text);color:var(--bg);padding:10px 16px;border-radius:12px;display:none;z-index:4;max-width:90vw}
#lock{position:fixed;inset:0;background:var(--bg);z-index:9;display:none;flex-direction:column;justify-content:center;padding:24px;max-width:420px;margin:0 auto}
@media (min-width:760px){nav{position:sticky;top:0;max-width:560px;margin:0 auto;border:1px solid var(--line);border-radius:16px}main{padding-bottom:40px}}
</style></head><body>
<header><h1><span id="name">Taschenfunk</span><span class="badge" id="kidb" hidden>Kindermodus</span></h1><p id="stat">verbinde …</p></header>
<main>
<div class="tab sel" id="t-home">
 <section><h2>Uhr</h2><div class="kv" id="info"></div></section>
 <section><h2>Akku</h2><div class="kv" id="batkv"></div><svg id="batchart" viewBox="0 0 360 90" style="width:100%;height:90px;margin-top:10px"></svg>
  <label class="f" for="pmode">Sparmodus</label><select id="pmode"><option value="1">Automatisch ab 20 %</option><option value="2">Immer an</option><option value="0">Aus</option></select>
  <p class="note">Sparmodus: Display höchstens 40 % und nach 10 s aus, kein Funk im Hintergrund, ohne Musik schläft das WLAN.</p></section>
 <section class="kidc"><div class="row"><div class="t"><b>Kindermodus</b><small id="kidsum"></small></div><label class="sw"><input type="checkbox" id="kon2"><span></span></label></div>
  <div class="row"><div class="t"><b>Wutmodus</b><small>Nur das Wut-Glas</small></div><label class="sw"><input type="checkbox" id="kangry2"><span></span></label></div></section>
</div>

<div class="tab" id="t-kids">
 <section class="kidc"><div class="row"><div class="t"><b>Kindermodus</b><small>Auf der Uhr keine freie Auswahl mehr</small></div><label class="sw"><input type="checkbox" id="kon"><span></span></label></div></section>
 <section class="kidc angry"><div class="row"><div class="t"><b>Wutmodus</b><small id="angst"></small></div><label class="sw"><input type="checkbox" id="kangry"><span></span></label></div>
  <p class="note">Auf der Uhr gibt es dann nur noch das Wut-Glas: ganz fest schütteln, bis alle roten Funken rausgeflogen sind, danach sinkt ruhiger Glitzer und ein Kreis atmet vor. Wischen und Tasten tun nichts, Musik und Funk sind aus.</p></section>
 <section class="kidc"><h2>Tagesablauf</h2><p class="note">Das Zifferblatt zeigt den Tag als Pixel-Geschichte.</p>
  <div class="two"><div><label class="f" for="kw">Aufstehen</label><input type="time" id="kw"></div><div><label class="f" for="kb">Ins Bett</label><input type="time" id="kb"></div></div>
  <div class="seg" id="kplace"><button data-v="0">Kita</button><button data-v="1">Schule</button></div>
  <div class="row" style="margin-top:6px"><div class="t"><b>Mittagsruhe</b><small>12:45 bis 14:00</small></div><label class="sw"><input type="checkbox" id="knap"><span></span></label></div>
 </section>
 <section class="kidc"><h2>Schlafenszeit</h2>
  <div class="row"><div class="t"><b>Nachts sperren</b><small>Ab der Bettzeit bis zum Aufstehen: keine Apps, kein Funk, gedimmt</small></div><label class="sw"><input type="checkbox" id="knight"><span></span></label></div>
  <label class="f" for="kstory">Einschlafmusik nach der Bettzeit</label>
  <select id="kstory"><option value="0">keine</option><option value="15">15 Minuten</option><option value="30">30 Minuten</option><option value="45">45 Minuten</option><option value="60">60 Minuten</option></select></section>
 <section class="kidc"><h2>Lautstärke</h2><div class="row"><div class="t"><b>Höchstens <span id="kvv"></span> %</b></div></div><input type="range" id="kv" min="10" max="100" step="5"></section>
 <section class="kidc"><h2>Erlaubte Apps</h2><p class="note">Musik ist immer dabei. Einstellungen und Zifferblätter sind gesperrt.</p><div id="kapps"></div></section>
 <section class="kidc"><h2>Freigegeben</h2><div id="krel"></div></section>
 <section class="kidc"><h2>Freigeben</h2><p class="note">Radiosender, Audiothek-Sendungen oder Podcasts.</p>
  <div class="seg" id="ssrc"><button data-v="radio" class="sel">Radio</button><button data-v="ard">Audiothek</button><button data-v="podcast">Podcasts</button></div>
  <label class="f" for="sq">Suche</label><input id="sq" type="search" placeholder="z. B. Radio Teddy, Ohrenbär, Figarino" enterkeyhint="search">
  <button class="wide kid" id="sgo">Suchen</button><div id="sres"></div>
 </section>
 <section class="kidc"><h2>Eltern-PIN</h2><p class="note" id="pinst"></p><label class="f" for="kpin">Neue PIN (4–8 Ziffern, leer = keine)</label><input id="kpin" inputmode="numeric" maxlength="8" autocomplete="off"><button class="wide ghost" id="kpingo">PIN speichern</button></section>
</div>

<div class="tab" id="t-music">
 <section><h2>Favoriten</h2><div id="favs"></div></section>
</div>

<div class="tab" id="t-ai">
 <section><h2 id="ainame2">Assistent</h2><p class="note">Gemini 3.8 Live mit Stimme. Auf der Uhr: App öffnen, halten und sprechen. Nicht im Kindermodus.</p>
  <label class="f" for="ainame">Name</label><input id="ainame" maxlength="20"><button class="wide" id="ainamego">Speichern</button>
  <div class="row" style="margin-top:8px"><div class="t"><b>Google-Suche</b><small>Aktuelles, Nachrichten, Sport, Wetter</small></div><label class="sw"><input type="checkbox" id="aisearch"><span></span></label></div></section>
 <section><h2>Modelle</h2><p class="note">Die Liste kommt direkt von Google – neue Modelle tauchen von selbst auf.</p>
  <label class="f" for="aimodel">KIra (Live, mit Stimme)</label><select id="aimodel"></select>
  <label class="f" for="aivoice">Stimme</label><select id="aivoice"></select>
  <label class="f" for="aistt">Spracheingabe bei der Suche</label><select id="aistt"></select>
  <button class="wide" id="aimodgo">Speichern</button></section>
 <section><h2>n8n</h2><p class="note" id="n8nst"></p>
  <label class="f" for="n8nurl">MCP-Adresse</label><input id="n8nurl" placeholder="https://…/mcp-server/http" autocapitalize="off" autocomplete="off">
  <label class="f" for="n8ntok">Token</label><input id="n8ntok" type="password" autocomplete="off" placeholder="unverändert lassen">
  <button class="wide" id="n8ngo">Speichern</button></section>
 <section><h2>Freigegeben für den Assistenten</h2><p class="note">Nur diese Workflows und Agenten darf er starten. Vor allem, was etwas verschickt oder ändert, fragt er nach.</p>
  <div id="n8nallow"></div><button class="wide ghost" id="n8nload">Workflows und Agenten laden</button><div id="n8nall"></div>
  <button class="wide" id="n8nsave" hidden>Freigabe speichern</button></section>
</div>

<div class="tab" id="t-dev">
 <section><h2>Name</h2><input id="nm" maxlength="16" aria-label="Name"><button class="wide" id="nmgo">Speichern</button></section>
 <section><h2>WLAN</h2><div id="wifi"></div>
  <label class="f" for="ss">Netz hinzufügen</label><input id="ss" placeholder="Name (SSID)" autocapitalize="off">
  <label class="f" for="pw">Passwort</label><input id="pw" type="password" placeholder="leer = offenes Netz">
  <button class="wide" id="wgo">Hinzufügen und verbinden</button></section>
 <section><h2>Spracheingabe</h2><p id="gst" class="note"></p><label class="f" for="gem">Gemini-API-Schlüssel</label><input id="gem" type="password" autocomplete="off" placeholder="unverändert lassen"><button class="wide" id="gemgo">Speichern</button></section>
 <section><h2>Firmware</h2><p class="note" id="fwst"></p>
  <label class="f" for="fwfile">Neue Firmware (firmware.bin aus dem Build)</label><input id="fwfile" type="file" accept=".bin">
  <button class="wide" id="fwgo">Aktualisieren</button><p class="note" id="fwprog"></p></section>
 <section><button class="wide ghost" id="rst">Uhr neu starten</button></section>
</div>
</main>
<nav id="nav"><button data-t="home" class="sel"><i>⌚</i>Übersicht</button><button data-t="kids" class="k"><i>🧸</i>Kinder</button><button data-t="music"><i>♪</i>Musik</button><button data-t="ai"><i>✦</i>KI</button><button data-t="dev"><i>⚙</i>Gerät</button></nav>
<div id="msg"></div>
<div id="lock"><h1>Eltern-PIN</h1><p class="note">Diese Uhr ist geschützt.</p><input id="lpin" type="password" inputmode="numeric" autocomplete="off"><button class="wide" id="lgo">Entsperren</button></div>
<script>
const $=id=>document.getElementById(id),val=id=>$(id).value.trim();
const APPS=[[1,'Funk','Walkie-Talkie mit anderen Uhren'],[2,'Timer',''],[4,'Stoppuhr',''],[8,'Balance','Kugel-Spiel'],[16,'Wecker',''],[32,'Wetter','']];
let S=null,pin='',src='radio';
try{pin=localStorage.getItem('pin')||''}catch(e){}
function esc(s){return String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function toast(t){const m=$('msg');m.textContent=t;m.style.display='block';clearTimeout(toast.h);toast.h=setTimeout(()=>m.style.display='none',2500)}
async function api(p,b){const o={headers:{'X-Pin':pin}};if(b!==undefined){o.method='POST';o.body=JSON.stringify(b)}
 const r=await fetch('/api/'+p,o);if(r.status==401){$('lock').style.display='flex';throw new Error('pin')}
 $('lock').style.display='none';return r.json().catch(()=>({}))}
async function post(p,b,quiet){try{const j=await api(p,b);if(!quiet)toast(j.msg||'Gespeichert');await load()}catch(e){if(e.message!='pin')toast('Uhr nicht erreichbar')}}
const hm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0'),mins=v=>{const[a,b]=v.split(':');return+a*60+ +b};
function row(t,s,right){return`<div class="row"><div class="t"><b>${esc(t)}</b>${s?`<small>${esc(s)}</small>`:''}</div>${right||''}</div>`}
function btn(label,fn,cls){return`<button class="ghost ${cls||''}" onclick='${fn}'>${label}</button>`}
const J=o=>esc(JSON.stringify(o));
function released(p){return S.kids.released.some(r=>r.path==p)}
function relBtn(it){const on=released(it.path);return btn(on?'✓ Frei':'Freigeben',`rel(${J(it)},${!on})`,on?'on':'')}
function rel(it,on){post('kids/release',{path:it.path,title:it.title,sub:it.sub,on},true).then(()=>{toast(on?'Freigegeben':'Entfernt');if(lastRes)showRes(lastRes)})}
function kids(p){post('kids',p,true)}
function render(){
 const s=S,k=s.kids;
 $('name').textContent=s.name;$('kidb').hidden=!k.on;
 $('stat').textContent=`${s.battery<0?'USB':s.battery+' %'}${s.charging?' ⚡':''} · ${s.ssid||'offline'} · ${s.host}.local`;
 $('info').innerHTML=`<span>Akku</span><span>${s.battery<0?'kein Akku':s.battery+' %'}${s.charging?' · lädt':''}</span><span>WLAN</span><span>${esc(s.ssid||'–')}${s.rssi?' ('+s.rssi+' dBm)':''}</span>
  <span>Adresse</span><span>${esc(s.ip)}</span><span>Funk-Nachbarn</span><span>${s.peers}</span><span>Version</span><span>${esc(s.version)}</span>`;
 $('kon').checked=$('kon2').checked=k.on;$('kangry').checked=$('kangry2').checked=k.angry&&k.on;
 $('angst').textContent=(k.angryToday?`Heute ${k.angryToday}× Wut rausgeschüttelt`:'Heute noch nicht benutzt')+(k.angryLast?' · zuletzt '+new Date(k.angryLast*1000).toLocaleTimeString('de',{hour:'2-digit',minute:'2-digit'}):'');
 $('kidb').textContent=k.angry&&k.on?'Wutmodus':'Kindermodus';
 $('kidsum').textContent=k.on?`An · ${k.released.length} Freigaben · bis ${k.maxVol} %`:'Aus';
 if(document.activeElement!==$('kw'))$('kw').value=hm(k.wake);if(document.activeElement!==$('kb'))$('kb').value=hm(k.bed);
 [...$('kplace').children].forEach(b=>b.classList.toggle('sel',+b.dataset.v==+k.school));
 $('knap').checked=k.nap;$('knight').checked=k.nightLock;if(document.activeElement!==$('kstory'))$('kstory').value=k.storyMin;$('kv').value=k.maxVol;$('kvv').textContent=k.maxVol;
 $('kapps').innerHTML=APPS.map(([b,n,d])=>row(n,d,`<label class="sw"><input type="checkbox" ${k.apps&b?'checked':''} onchange="kids({apps:this.checked?S.kids.apps|${b}:S.kids.apps&~${b}})"><span></span></label>`)).join('');
 $('krel').innerHTML=k.released.map(r=>row(r.title,r.sub,btn('Entfernen',`rel(${J(r)},false)`))).join('')||'<p class="note">Noch nichts – unten suchen oder einen Favoriten freigeben.</p>';
 $('pinst').innerHTML=k.pin?'<span class="ok">PIN ist gesetzt</span> – die Weboberfläche fragt danach.':'Keine PIN – jeder im WLAN kann die Uhr einstellen.';
 $('favs').innerHTML=s.favs.map(f=>row(f.title,f.sub,relBtn(f)+btn('✕',`post("fav/remove",{path:${J(f.path)}})`))).join('')||'<p class="note">Noch keine – auf der Uhr in Alben, Playlists und Podcasts merken.</p>';
 if(document.activeElement!==$('nm'))$('nm').value=s.name;
 $('wifi').innerHTML=s.known.map(n=>row(n,n===s.ssid?'verbunden':'',btn('Vergessen',`post("wifi/forget",{ssid:${J(n)}})`))).join('')||'<p class="note">Keine Netze gespeichert</p>';
 const ai=s.assist;
 if(document.activeElement!==$('ainame'))$('ainame').value=ai.name;$('ainame2').textContent=ai.name;$('aisearch').checked=ai.search;
 if(document.activeElement!==$('n8nurl'))$('n8nurl').value=ai.n8nUrl;
 $('n8nst').innerHTML=ai.n8nSet?'<span class="ok">Eingerichtet</span>':'Noch nicht eingerichtet – Adresse und Token aus n8n (Einstellungen → MCP).';
 $('n8nallow').innerHTML=ai.allowed.map(a=>row(a.name,a.kind=='agent'?'Agent':'Workflow',btn('✕',`unallow(${J(a.id)})`))).join('')||'<p class="note">Noch nichts freigegeben.</p>';
 if($('t-ai').classList.contains('sel'))loadModels();
 const pw=s.power;
 $('batkv').innerHTML=`<span>Stand</span><span>${s.battery<0?'kein Akku':s.battery+' %'}${pw.mv?' · '+(pw.mv/1000).toFixed(2)+' V':''}${s.charging?' · lädt':''}</span>
  <span>Restlaufzeit</span><span>${pw.remainMin>=0?Math.floor(pw.remainMin/60)+' h '+pw.remainMin%60+' min':'wird gemessen …'}</span>
  <span>Verbrauch</span><span>${pw.drain>0?pw.drain.toFixed(1)+' %/h':'–'}</span><span>Sparmodus</span><span>${pw.saving?'an':'aus'}</span>`;
 if(document.activeElement!==$('pmode'))$('pmode').value=pw.mode;
 const h=pw.history;if(h.length>1){const mx=Math.max(...h.map(p=>p[0]))||1;
  const pts=h.map(p=>`${(360-p[0]/mx*360).toFixed(1)},${(86-p[1]*0.82).toFixed(1)}`).join(' ');
  $('batchart').innerHTML=`<polyline points="${pts}" fill="none" stroke="var(--acc)" stroke-width="2"/><text x="0" y="10" font-size="10" fill="var(--dim)">-${mx} min</text><text x="330" y="10" font-size="10" fill="var(--dim)">jetzt</text>`}
 else $('batchart').innerHTML='<text x="0" y="50" font-size="12" fill="var(--dim)">Verlauf: alle 2 Minuten ein Messpunkt</text>';
 $('fwst').textContent=`Version ${s.version} · ${s.board} · gebaut ${s.built}`;
 $('gst').innerHTML=s.gemini?'<span class="ok">Schlüssel gesetzt</span>':'<span class="bad">Kein Schlüssel – Spracheingabe aus</span>';
}
async function load(){try{S=await api('state');render()}catch(e){if(e.message!='pin')$('stat').textContent='Uhr nicht erreichbar'}}
let lastRes=null;
function showRes(items){lastRes=items;$('sres').innerHTML=items.map(it=>row(it.title,it.sub,relBtn(it))).join('')||'<p class="note">Nichts gefunden</p>'}
async function search(){const q=val('sq');if(!q)return;$('sres').innerHTML='<p class="note">Suche …</p>';
 try{const j=await api('search?src='+src+'&q='+encodeURIComponent(q));if(j.err)toast(j.err);showRes(j.items||[])}catch(e){$('sres').innerHTML=''}}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('sel',x==b));
 document.querySelectorAll('.tab').forEach(t=>t.classList.toggle('sel',t.id=='t-'+b.dataset.t));scrollTo(0,0)});
$('kon').onchange=$('kon2').onchange=e=>{kids({on:e.target.checked});toast(e.target.checked?'Kindermodus an':'Kindermodus aus')};
$('kangry').onchange=$('kangry2').onchange=e=>{kids(e.target.checked?{angry:true,on:true}:{angry:false});toast(e.target.checked?'Wutmodus an – nur noch das Wut-Glas':'Wutmodus aus')};
$('kw').onchange=()=>kids({wake:mins($('kw').value)});$('kb').onchange=()=>kids({bed:mins($('kb').value)});
$('kplace').onclick=e=>{if(e.target.dataset.v)kids({school:e.target.dataset.v=='1'})};
$('knap').onchange=e=>kids({nap:e.target.checked});
$('knight').onchange=e=>kids({nightLock:e.target.checked});$('kstory').onchange=e=>kids({storyMin:+e.target.value});
$('kv').oninput=e=>$('kvv').textContent=e.target.value;$('kv').onchange=e=>kids({maxVol:+e.target.value});
$('ssrc').onclick=e=>{if(!e.target.dataset.v)return;src=e.target.dataset.v;[...$('ssrc').children].forEach(b=>b.classList.toggle('sel',b==e.target));if(val('sq'))search()};
$('sgo').onclick=search;$('sq').onkeydown=e=>{if(e.key=='Enter')search()};
$('kpingo').onclick=()=>{const p=val('kpin');if(p&&!/^\d{4,8}$/.test(p))return toast('4–8 Ziffern');
 api('kids/pin',{pin:p}).then(j=>{pin=p;try{localStorage.setItem('pin',p)}catch(e){}$('kpin').value='';toast(j.msg);load()})};
$('lgo').onclick=()=>{pin=val('lpin');try{localStorage.setItem('pin',pin)}catch(e){}load()};
$('nmgo').onclick=()=>post('name',{name:val('nm')});
$('wgo').onclick=()=>post('wifi',{ssid:val('ss'),pass:val('pw')});
$('gemgo').onclick=()=>post('keys',{gemini:val('gem')});
const VOICES=['Kore','Leda','Aoede','Zephyr','Callirrhoe','Autonoe','Despina','Erinome','Laomedeia','Achernar','Gacrux','Pulcherrima','Sulafat','Vindemiatrix','Puck','Charon','Fenrir','Orus','Enceladus','Iapetus','Umbriel','Algieba','Algenib','Rasalgethi','Alnilam','Schedar','Achird','Zubenelgenubi','Sadachbia','Sadaltager'];
function opts(sel,list,cur){$(sel).innerHTML=[...new Set([cur,...list])].filter(Boolean).map(m=>`<option ${m==cur?'selected':''}>${esc(m)}</option>`).join('')}
let modelsLoaded=false;
async function loadModels(){if(modelsLoaded||!S)return;modelsLoaded=true;const a=S.assist;
 opts('aimodel',[],a.model);opts('aistt',[],a.stt);opts('aivoice',VOICES,a.voice);
 try{const j=await api('models');if(j.err)return toast(j.err);opts('aimodel',j.live,a.model);opts('aistt',j.text,a.stt)}catch(e){}}
$('aimodgo').onclick=()=>post('assist',{model:$('aimodel').value,voice:$('aivoice').value,stt:$('aistt').value});
document.querySelector('nav button[data-t=ai]').addEventListener('click',()=>setTimeout(loadModels,50));
$('ainamego').onclick=()=>post('assist',{name:val('ainame')});
$('aisearch').onchange=e=>post('assist',{search:e.target.checked},true);
$('n8ngo').onclick=()=>{post('assist',{n8nUrl:val('n8nurl'),n8nToken:val('n8ntok')});$('n8ntok').value=''};
let n8nList=[];
$('n8nload').onclick=async()=>{$('n8nall').innerHTML='<p class="note">Lade …</p>';
 try{const j=await api('n8n/list');if(j.err){$('n8nall').innerHTML='<p class="note bad">'+esc(j.err)+'</p>';return}
  n8nList=j.items||[];const on=new Set(S.assist.allowed.map(a=>a.id));
  $('n8nall').innerHTML=n8nList.map((w,i)=>row(w.name,(w.kind=='agent'?'Agent':'Workflow')+(w.active?' · aktiv':''),`<label class="sw"><input type="checkbox" data-i="${i}" ${on.has(w.id)?'checked':''}><span></span></label>`)).join('')||'<p class="note">Nichts gefunden</p>';
  $('n8nsave').hidden=!n8nList.length}catch(e){$('n8nall').innerHTML=''}};
$('n8nsave').onclick=()=>{const sel=[...document.querySelectorAll('#n8nall input:checked')].map(c=>n8nList[+c.dataset.i]).map(w=>({id:w.id,name:w.name,kind:w.kind,desc:w.desc||''}));
 post('n8n/allow',{items:sel})};
function unallow(id){post('n8n/allow',{items:S.assist.allowed.filter(a=>a.id!=id)})}
$('pmode').onchange=e=>post('power',{mode:+e.target.value});
$('fwgo').onclick=()=>{const f=$('fwfile').files[0];if(!f)return toast('Erst eine Datei wählen');
 const fd=new FormData();fd.append('file',f,f.name);const x=new XMLHttpRequest();x.open('POST','/api/update');x.setRequestHeader('X-Pin',pin);
 x.upload.onprogress=e=>{if(e.lengthComputable)$('fwprog').textContent='Übertrage … '+Math.round(e.loaded*100/e.total)+' %'};
 x.onload=()=>{let m='';try{m=JSON.parse(x.responseText).msg}catch(e){}$('fwprog').textContent=m||('Fehler '+x.status);if(x.status==200)setTimeout(load,15000)};
 x.onerror=()=>{$('fwprog').textContent='Verbindung abgebrochen'};x.send(fd)};
$('rst').onclick=()=>confirm('Uhr neu starten?')&&post('restart',{});
// Reiter direkt öffnen: …/#kinder, #musik, #ki, #geraet
const HASH={kinder:'kids',musik:'music',ki:'ai',geraet:'dev'};
function fromHash(){const t=HASH[location.hash.slice(1)];const b=t&&document.querySelector(`nav button[data-t=${t}]`);if(b)b.click()}
window.addEventListener('hashchange',fromHash);fromHash();
load();setInterval(()=>{if(!document.hidden)load()},10000);
</script></body></html>)HTML";

static void json(int code, JsonDocument& d) {
  String out;
  serializeJson(d, out);
  srv->send(code, "application/json", out);
}

static void msg(const char* m, int code = 200) {
  JsonDocument d;
  d["msg"] = m;
  json(code, d);
}

static bool body(JsonDocument& d) { return !deserializeJson(d, srv->arg("plain")); }

// Eltern-PIN: ist eine gesetzt, braucht jede Anfrage an /api/ den Kopf „X-Pin“
static bool pinOk() {
  const char* pin = kids::cfg().pin;
  return !pin[0] || srv->header("X-Pin") == pin;
}

static bool authed() {
  if (pinOk()) return true;
  delay(600);  // Raten bremsen
  msg("PIN nötig", 401);
  return false;
}

// ------------------------------------------------------------------ Update per WLAN
// Die hochgeladene Firmware muss die Kennung dieses Boards enthalten (TF_BOARD_TAG, siehe main.cpp) –
// so landet die Firmware der runden Uhr nicht versehentlich auf der eckigen und umgekehrt.
static String otaErr;
static bool otaTag = false;
static char otaTail[48];
static size_t otaTailLen = 0;

static void otaScan(const uint8_t* p, size_t n) {
  size_t tl = strlen(TF_BOARD_TAG);
  if (otaTag || !n) return;
  // über die Grenze zweier Stücke hinweg: Ende des letzten + Anfang des neuen
  uint8_t join[96];
  size_t k = min(n, tl - 1);
  memcpy(join, otaTail, otaTailLen);
  memcpy(join + otaTailLen, p, k);
  if (memmem(join, otaTailLen + k, TF_BOARD_TAG, tl) || memmem(p, n, TF_BOARD_TAG, tl)) { otaTag = true; return; }
  otaTailLen = min(n, tl - 1);
  memcpy(otaTail, p + n - otaTailLen, otaTailLen);
}

static void otaUpload() {
  HTTPUpload& u = srv->upload();
  if (u.status == UPLOAD_FILE_START) {
    otaErr = "";
    otaTag = false;
    otaTailLen = 0;
    if (!pinOk()) { otaErr = "PIN nötig"; return; }
    Serial.printf("Update: %s wird empfangen\n", u.filename.c_str());
    music::stop();
    assist::close();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) otaErr = Update.errorString();
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (otaErr.length()) return;
    otaScan(u.buf, u.currentSize);
    if (Update.write(u.buf, u.currentSize) != u.currentSize) otaErr = Update.errorString();
  } else if (u.status == UPLOAD_FILE_END) {
    if (otaErr.length()) { Update.abort(); return; }
    if (!otaTag) { Update.abort(); otaErr = "Diese Firmware ist für ein anderes Board"; return; }
    if (!Update.end(true)) otaErr = Update.errorString();
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaErr = "Übertragung abgebrochen";
  }
}

static void otaDone() {
  if (otaErr.length()) {
    Serial.printf("Update: abgelehnt (%s)\n", otaErr.c_str());
    return msg(otaErr.c_str(), otaErr == "PIN nötig" ? 401 : 400);
  }
  Serial.println("Update: installiert, starte neu");
  msg("Update installiert – die Uhr startet neu");
  delay(600);
  ESP.restart();
}

static void addItems(JsonArray a, const music::Item* it, int n) {
  for (int i = 0; i < n; i++) {
    JsonObject f = a.add<JsonObject>();
    f["path"] = it[i].id;
    f["title"] = it[i].title;
    f["sub"] = it[i].sub;
  }
}

static void state() {
  if (!authed()) return;
  JsonDocument d;
  d["name"] = cfg.name;
  d["host"] = netHost();
  d["ip"] = netIp();
  d["version"] = FW_VERSION;
  d["board"] = TF_BOARD_TAG + 8;  // „TFBOARD=…;“ → Boardname
  d["built"] = __DATE__ " " __TIME__;
  d["battery"] = hwBattery();
  d["charging"] = hwCharging();
  d["ssid"] = netOnline() ? netSsid() : "";
  d["rssi"] = netOnline() ? netRssi() : 0;
  radio::Peer ps[12];
  d["peers"] = radio::peers(ps, 12);
  JsonArray k = d["known"].to<JsonArray>();
  for (int i = 0; i < knownCount; i++) k.add(known[i].ssid);
  d["gemini"] = voice::configured();
  JsonObject pw = d["power"].to<JsonObject>();
  pw["mode"] = (int)pwr::mode();
  pw["saving"] = pwr::saving();
  pw["mv"] = hwBatteryMv();
  pw["remainMin"] = pwr::remainingMin();
  pw["drain"] = pwr::drainPerHour();
  JsonDocument hd;
  deserializeJson(hd, pwr::historyJson());
  pw["history"] = hd;
  JsonObject ai = d["assist"].to<JsonObject>();
  ai["name"] = assist::name();
  ai["search"] = assist::search();
  ai["model"] = assist::model();
  ai["voice"] = assist::voice();
  ai["stt"] = assist::sttModel();
  ai["n8nUrl"] = n8n::url();
  ai["n8nSet"] = n8n::configured();  // nie den Token selbst
  JsonDocument al;
  deserializeJson(al, n8n::allowedJson());
  ai["allowed"] = al;
  static music::Item* fv = (music::Item*)heap_caps_malloc(sizeof(music::Item) * favs::MAX, MALLOC_CAP_SPIRAM);
  addItems(d["favs"].to<JsonArray>(), fv, favs::list(fv, favs::MAX));
  const kids::Cfg& kc = kids::cfg();
  JsonObject ko = d["kids"].to<JsonObject>();
  ko["on"] = kc.on;
  ko["wake"] = kc.wake;
  ko["bed"] = kc.bed;
  ko["maxVol"] = kc.maxVol;
  ko["apps"] = kc.apps;
  ko["school"] = kc.school;
  ko["nap"] = kc.nap;
  ko["nightLock"] = kc.nightLock;
  ko["storyMin"] = kc.storyMin;
  ko["night"] = kids::night();
  ko["pin"] = kc.pin[0] != 0;  // nie die PIN selbst
  ko["angry"] = kc.angry;
  ko["angryToday"] = kids::angryToday();
  ko["angryLast"] = kids::angryLast();
  addItems(ko["released"].to<JsonArray>(), fv, kids::list(fv, favs::MAX));
  json(200, d);
}

static void setKids() {
  if (!authed()) return;
  JsonDocument d;
  if (!body(d)) return msg("Ungültige Anfrage", 400);
  kids::Cfg c = kids::cfg();
  bool was = c.on, wasAngry = kids::angry();
  c.on = d["on"] | c.on;
  c.wake = d["wake"] | c.wake;
  c.bed = d["bed"] | c.bed;
  c.maxVol = d["maxVol"] | c.maxVol;
  c.apps = d["apps"] | c.apps;
  c.school = d["school"] | c.school;
  c.nap = d["nap"] | c.nap;
  c.nightLock = d["nightLock"] | c.nightLock;
  c.storyMin = d["storyMin"] | c.storyMin;
  c.angry = d["angry"] | c.angry;
  kids::set(c);
  if (kids::angry() && !wasAngry) music::stop();  // im Wutmodus läuft nichts anderes
  if (c.on) ui::volumeSet(cfg.volume);  // Obergrenze sofort anwenden
  if (c.on != was || kids::angry() != wasAngry) ui::home();  // Zifferblatt und Apps neu aufbauen
  msg("Gespeichert");
}

// Suche für Freigaben: nur Listen, die man freigeben kann
static void search() {
  if (!authed()) return;
  static music::Item* it = nullptr;
  if (!it) it = (music::Item*)heap_caps_calloc(40, sizeof(music::Item), MALLOC_CAP_SPIRAM);
  String q = srv->arg("q"), src = srv->arg("src");
  String path = (src == "podcast" ? "pcsearch:" : src == "ard" ? "ardsearch:" : "rlsearch:") + q;
  char title[72], err[64];
  int n = music::query(path.c_str(), title, sizeof(title), it, 40, err, sizeof(err));
  JsonDocument d;
  JsonArray a = d["items"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    const char* id = it[i].id;
    // freigeben lassen sich Podcasts, Audiothek-Sendungen und Radiosender
    bool station = !strncmp(id, "radio:", 6);
    if (!station && strncmp(id, "pc:feed:", 8) && strncmp(id, "ard:set:", 8)) continue;
    addItems(a, &it[i], 1);
  }
  if (err[0]) d["err"] = err;
  json(200, d);
}


void begin() {
  srv = new WebServer(80);
  const char* hdrs[] = {"X-Pin"};
  srv->collectHeaders(hdrs, 1);
  srv->on("/", HTTP_GET, []() { srv->send_P(200, "text/html; charset=utf-8", PAGE); });
  srv->on("/api/state", HTTP_GET, state);
  srv->on("/api/update", HTTP_POST, otaDone, otaUpload);
  srv->on("/api/search", HTTP_GET, search);
  srv->on("/api/kids", HTTP_POST, setKids);
  srv->on("/api/kids/release", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d) || !(d["path"] | "")[0]) return msg("Ungültige Anfrage", 400);
    kids::release(d["path"], d["title"] | "", d["sub"] | "", d["on"] | true);
    msg("Gespeichert");
  });
  srv->on("/api/kids/pin", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    const char* p = d["pin"] | "";
    size_t n = strlen(p);
    for (size_t i = 0; i < n; i++) if (!isdigit((unsigned char)p[i])) n = 99;
    if (n && (n < 4 || n > 8)) return msg("4–8 Ziffern", 400);
    kids::Cfg c = kids::cfg();
    snprintf(c.pin, sizeof(c.pin), "%s", p);
    kids::set(c);
    msg(n ? "PIN gespeichert" : "PIN entfernt");
  });
  srv->on("/api/assist", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    assist::configure(d["name"] | (const char*)nullptr, d["search"].is<bool>() ? (int)d["search"].as<bool>() : -1,
                      d["model"] | (const char*)nullptr, d["voice"] | (const char*)nullptr, d["stt"] | (const char*)nullptr);
    const char* u = d["n8nUrl"] | (const char*)nullptr;
    const char* t = d["n8nToken"] | "";
    if (u || t[0]) n8n::set(u, t[0] ? t : nullptr);
    msg("Gespeichert");
  });
  srv->on("/api/power", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    pwr::setMode((pwr::Mode)(d["mode"] | 1));
    msg("Gespeichert");
  });
  srv->on("/api/models", HTTP_GET, []() {
    if (!authed()) return;
    String err, m = assist::models(err);
    if (!m.length()) { JsonDocument d; d["err"] = err; return json(200, d); }
    srv->send(200, "application/json", m);
  });
  srv->on("/api/n8n/list", HTTP_GET, []() {
    if (!authed()) return;
    String err, list = n8n::listAll(err);
    if (!list.length()) { JsonDocument d; d["err"] = err; return json(200, d); }
    srv->send(200, "application/json", "{\"items\":" + list + "}");
  });
  srv->on("/api/n8n/allow", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    String s;
    serializeJson(d["items"], s);
    n8n::setAllowed(s);
    msg("Freigabe gespeichert");
  });
  srv->on("/api/name", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    const char* n = body(d) ? (d["name"] | "") : "";
    if (!n[0]) return msg("Name fehlt", 400);
    snprintf(cfg.name, sizeof(cfg.name), "%s", n);
    settingsSave();
    radio::nameChanged();
    msg("Name gespeichert");
  });
  srv->on("/api/wifi", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d) || !(d["ssid"] | "")[0]) return msg("SSID fehlt", 400);
    knownAdd(d["ssid"], d["pass"] | "");
    settingsSave();
    msg("Netz gespeichert");
    netConnect(d["ssid"], d["pass"] | "");
  });
  srv->on("/api/wifi/forget", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (body(d)) { knownForget(d["ssid"] | ""); settingsSave(); }
    msg("Netz vergessen");
  });
  srv->on("/api/keys", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    const char* g = d["gemini"] | "";
    if (g[0]) keys::set(g);
    msg("Schlüssel gespeichert");
  });
  srv->on("/api/fav/remove", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (body(d) && favs::has(d["path"] | "")) favs::toggle(d["path"] | "", "", "");
    msg("Favorit entfernt");
  });
  srv->on("/api/restart", HTTP_POST, []() {
    if (!authed()) return;
    msg("Startet neu …");
    delay(300);
    ESP.restart();
  });
  srv->begin();
}

void poll() {
  if (srv) srv->handleClient();
}

}  // namespace webui
