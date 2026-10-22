#include "webui.h"
#include <ArduinoJson.h>
#include <WebServer.h>
#include "config.h"
#include "favs.h"
#include "hw.h"
#include "keys.h"
#include "kids.h"
#include "music.h"
#include "net.h"
#include "radio.h"
#include "settings.h"
#include "spotify.h"
#include "ui/ui.h"
#include "voice.h"

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
 <section class="kidc"><h2>Lautstärke</h2><div class="row"><div class="t"><b>Höchstens <span id="kvv"></span> %</b></div></div><input type="range" id="kv" min="10" max="100" step="5"></section>
 <section class="kidc"><h2>Erlaubte Apps</h2><p class="note">Musik ist immer dabei. Einstellungen und Zifferblätter sind gesperrt.</p><div id="kapps"></div></section>
 <section class="kidc"><h2>Freigegeben</h2><div id="krel"></div></section>
 <section class="kidc"><h2>Freigeben</h2>
  <div class="seg" id="ssrc"><button data-v="spotify" class="sel">Spotify</button><button data-v="podcast">Podcasts</button></div>
  <label class="f" for="sq">Suche</label><input id="sq" type="search" placeholder="z. B. Bibi Blocksberg" enterkeyhint="search">
  <button class="wide kid" id="sgo">Suchen</button><div id="sres"></div>
  <label class="f" for="slink">… oder Spotify-Link einfügen (Playlist oder Album)</label><input id="slink" placeholder="https://open.spotify.com/playlist/…">
  <button class="wide ghost" id="slinkgo">Link freigeben</button>
  <p class="note">Eigene Playlists müssen in Spotify öffentlich sein, damit die Uhr sie lesen kann.</p>
 </section>
 <section class="kidc"><h2>Eltern-PIN</h2><p class="note" id="pinst"></p><label class="f" for="kpin">Neue PIN (4–8 Ziffern, leer = keine)</label><input id="kpin" inputmode="numeric" maxlength="8" autocomplete="off"><button class="wide ghost" id="kpingo">PIN speichern</button></section>
</div>

<div class="tab" id="t-music">
 <section><h2>Spotify</h2><p id="spst" class="note"></p><div id="acc"></div><p class="note">Weiteres Konto: in dessen Spotify-App die Uhr als Lautsprecher wählen.</p></section>
 <section><h2>Favoriten</h2><div id="favs"></div></section>
</div>

<div class="tab" id="t-dev">
 <section><h2>Name</h2><input id="nm" maxlength="16" aria-label="Name"><button class="wide" id="nmgo">Speichern</button></section>
 <section><h2>WLAN</h2><div id="wifi"></div>
  <label class="f" for="ss">Netz hinzufügen</label><input id="ss" placeholder="Name (SSID)" autocapitalize="off">
  <label class="f" for="pw">Passwort</label><input id="pw" type="password" placeholder="leer = offenes Netz">
  <button class="wide" id="wgo">Hinzufügen und verbinden</button></section>
 <section><h2>Spotify-Schlüssel</h2><label class="f" for="sid">Client-ID (developer.spotify.com)</label><input id="sid" autocomplete="off" autocapitalize="off">
  <label class="f" for="ssec">Client-Secret</label><input id="ssec" type="password" autocomplete="off" placeholder="unverändert lassen">
  <button class="wide" id="spgo">Speichern</button></section>
 <section><h2>Spracheingabe</h2><p id="gst" class="note"></p><label class="f" for="gem">Gemini-API-Schlüssel</label><input id="gem" type="password" autocomplete="off" placeholder="unverändert lassen"><button class="wide" id="gemgo">Speichern</button></section>
 <section><button class="wide ghost" id="rst">Uhr neu starten</button></section>
</div>
</main>
<nav id="nav"><button data-t="home" class="sel"><i>⌚</i>Übersicht</button><button data-t="kids" class="k"><i>🧸</i>Kinder</button><button data-t="music"><i>♪</i>Musik</button><button data-t="dev"><i>⚙</i>Gerät</button></nav>
<div id="msg"></div>
<div id="lock"><h1>Eltern-PIN</h1><p class="note">Diese Uhr ist geschützt.</p><input id="lpin" type="password" inputmode="numeric" autocomplete="off"><button class="wide" id="lgo">Entsperren</button></div>
<script>
const $=id=>document.getElementById(id),val=id=>$(id).value.trim();
const APPS=[[1,'Funk','Walkie-Talkie mit anderen Uhren'],[2,'Timer',''],[4,'Stoppuhr',''],[8,'Balance','Kugel-Spiel'],[16,'Wecker',''],[32,'Wetter','']];
let S=null,pin='',src='spotify';
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
  <span>Adresse</span><span>${esc(s.ip)}</span><span>Spotify</span><span>${s.spotify.connected?esc(s.spotify.user):'nicht verbunden'}</span><span>Funk-Nachbarn</span><span>${s.peers}</span><span>Version</span><span>${esc(s.version)}</span>`;
 $('kon').checked=$('kon2').checked=k.on;$('kangry').checked=$('kangry2').checked=k.angry&&k.on;
 $('angst').textContent=(k.angryToday?`Heute ${k.angryToday}× Wut rausgeschüttelt`:'Heute noch nicht benutzt')+(k.angryLast?' · zuletzt '+new Date(k.angryLast*1000).toLocaleTimeString('de',{hour:'2-digit',minute:'2-digit'}):'');
 $('kidb').textContent=k.angry&&k.on?'Wutmodus':'Kindermodus';
 $('kidsum').textContent=k.on?`An · ${k.released.length} Freigaben · bis ${k.maxVol} %`:'Aus';
 if(document.activeElement!==$('kw'))$('kw').value=hm(k.wake);if(document.activeElement!==$('kb'))$('kb').value=hm(k.bed);
 [...$('kplace').children].forEach(b=>b.classList.toggle('sel',+b.dataset.v==+k.school));
 $('knap').checked=k.nap;$('kv').value=k.maxVol;$('kvv').textContent=k.maxVol;
 $('kapps').innerHTML=APPS.map(([b,n,d])=>row(n,d,`<label class="sw"><input type="checkbox" ${k.apps&b?'checked':''} onchange="kids({apps:this.checked?S.kids.apps|${b}:S.kids.apps&~${b}})"><span></span></label>`)).join('');
 $('krel').innerHTML=k.released.map(r=>row(r.title,r.sub,btn('Entfernen',`rel(${J(r)},false)`))).join('')||'<p class="note">Noch nichts – unten suchen oder einen Favoriten freigeben.</p>';
 $('pinst').innerHTML=k.pin?'<span class="ok">PIN ist gesetzt</span> – die Weboberfläche fragt danach.':'Keine PIN – jeder im WLAN kann die Uhr einstellen.';
 const sp=s.spotify;
 $('spst').innerHTML=!sp.configured?'<span class="bad">Client-ID und Secret fehlen (Reiter „Gerät“).</span>':sp.connected?`<span class="ok">Verbunden als ${esc(sp.user)}</span>`:sp.accounts.length?'Verbindet …':'Noch nicht gekoppelt: in der Spotify-App die Uhr als Lautsprecher wählen.';
 $('acc').innerHTML=sp.accounts.map((a,i)=>row(a.name,a.current?'aktiv':'',(a.current?'':btn('Wechseln',`post("spotify/use",{i:${i}})`))+btn('✕',`confirm("Konto entfernen?")&&post("spotify/remove",{i:${i}})`))).join('');
 $('favs').innerHTML=s.favs.map(f=>row(f.title,f.sub,relBtn(f)+btn('✕',`post("fav/remove",{path:${J(f.path)}})`))).join('')||'<p class="note">Noch keine – auf der Uhr in Alben, Playlists und Podcasts merken.</p>';
 if(document.activeElement!==$('nm'))$('nm').value=s.name;
 $('wifi').innerHTML=s.known.map(n=>row(n,n===s.ssid?'verbunden':'',btn('Vergessen',`post("wifi/forget",{ssid:${J(n)}})`))).join('')||'<p class="note">Keine Netze gespeichert</p>';
 $('sid').placeholder=sp.clientId||'nicht gesetzt';
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
$('kv').oninput=e=>$('kvv').textContent=e.target.value;$('kv').onchange=e=>kids({maxVol:+e.target.value});
$('ssrc').onclick=e=>{if(!e.target.dataset.v)return;src=e.target.dataset.v;[...$('ssrc').children].forEach(b=>b.classList.toggle('sel',b==e.target));if(val('sq'))search()};
$('sgo').onclick=search;$('sq').onkeydown=e=>{if(e.key=='Enter')search()};
$('slinkgo').onclick=()=>{const u=val('slink');if(u)post('kids/link',{url:u}).then(()=>$('slink').value='')};
$('kpingo').onclick=()=>{const p=val('kpin');if(p&&!/^\d{4,8}$/.test(p))return toast('4–8 Ziffern');
 api('kids/pin',{pin:p}).then(j=>{pin=p;try{localStorage.setItem('pin',p)}catch(e){}$('kpin').value='';toast(j.msg);load()})};
$('lgo').onclick=()=>{pin=val('lpin');try{localStorage.setItem('pin',pin)}catch(e){}load()};
$('nmgo').onclick=()=>post('name',{name:val('nm')});
$('wgo').onclick=()=>post('wifi',{ssid:val('ss'),pass:val('pw')});
$('spgo').onclick=()=>post('keys',{spotifyId:val('sid'),spotifySecret:val('ssec')});
$('gemgo').onclick=()=>post('keys',{gemini:val('gem')});
$('rst').onclick=()=>confirm('Uhr neu starten?')&&post('restart',{});
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
static bool authed() {
  const char* pin = kids::cfg().pin;
  if (!pin[0] || srv->header("X-Pin") == pin) return true;
  delay(600);  // Raten bremsen
  msg("PIN nötig", 401);
  return false;
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
  d["battery"] = hwBattery();
  d["charging"] = hwCharging();
  d["ssid"] = netOnline() ? netSsid() : "";
  d["rssi"] = netOnline() ? netRssi() : 0;
  radio::Peer ps[12];
  d["peers"] = radio::peers(ps, 12);
  JsonArray k = d["known"].to<JsonArray>();
  for (int i = 0; i < knownCount; i++) k.add(known[i].ssid);
  JsonObject sp = d["spotify"].to<JsonObject>();
  sp["configured"] = spotify::configured();
  sp["connected"] = spotify::connected();
  sp["user"] = spotify::user();
  String id = keys::spotifyId();  // nur gekürzt anzeigen
  sp["clientId"] = id.length() > 8 ? id.substring(0, 4) + "…" + id.substring(id.length() - 4) : id;
  static char names[spotify::ACCOUNTS][64];
  int cur = -1, n = spotify::accounts(names, spotify::ACCOUNTS, &cur);
  JsonArray acc = sp["accounts"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject a = acc.add<JsonObject>();
    a["name"] = names[i];
    a["current"] = i == cur;
  }
  d["gemini"] = voice::configured();
  static music::Item fv[favs::MAX];
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
  String q = srv->arg("q");
  bool pod = srv->arg("src") == "podcast";
  String path = (pod ? "pcsearch:" : "search:") + q;
  char title[72], err[64];
  int n = music::query(path.c_str(), title, sizeof(title), it, 40, err, sizeof(err));
  JsonDocument d;
  JsonArray a = d["items"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    const char* id = it[i].id;
    if (it[i].kind != music::K_LIST) continue;
    if (strncmp(id, "sp:album:", 9) && strncmp(id, "sp:playlist:", 12) && strncmp(id, "pc:feed:", 8)) continue;
    addItems(a, &it[i], 1);
  }
  if (err[0]) d["err"] = err;
  json(200, d);
}

// Spotify-Link (open.spotify.com/playlist/… oder /album/…) freigeben
static void link() {
  if (!authed()) return;
  JsonDocument d;
  String url = body(d) ? String(d["url"] | "") : "";
  const char* kinds[] = {"playlist", "album"};
  String path;
  for (const char* k : kinds) {
    int i = url.indexOf(String("/") + k + "/");
    if (i < 0) i = url.indexOf(String(":") + k + ":");
    if (i < 0) continue;
    String id = url.substring(i + strlen(k) + 2);
    int e = 0;
    while (e < (int)id.length() && isalnum((unsigned char)id[e])) e++;
    id = id.substring(0, e);
    if (id.length() > 10) path = String("sp:") + k + ":" + id;
    break;
  }
  if (!path.length()) return msg("Kein Spotify-Playlist- oder Album-Link", 400);
  static music::Item* it = nullptr;
  if (!it) it = (music::Item*)heap_caps_calloc(40, sizeof(music::Item), MALLOC_CAP_SPIRAM);
  char title[72] = "", err[64] = "";
  int n = music::query(path.c_str(), title, sizeof(title), it, 40, err, sizeof(err));
  if (!n || !title[0]) return msg(err[0] ? err : "Nicht gefunden – ist die Playlist öffentlich?", 400);
  kids::release(path.c_str(), title, path.startsWith("sp:album:") ? "Album" : "Playlist", true);
  msg("Freigegeben");
}

void begin() {
  srv = new WebServer(80);
  const char* hdrs[] = {"X-Pin"};
  srv->collectHeaders(hdrs, 1);
  srv->on("/", HTTP_GET, []() { srv->send_P(200, "text/html; charset=utf-8", PAGE); });
  srv->on("/api/state", HTTP_GET, state);
  srv->on("/api/search", HTTP_GET, search);
  srv->on("/api/kids", HTTP_POST, setKids);
  srv->on("/api/kids/link", HTTP_POST, link);
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
  srv->on("/api/name", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    const char* n = body(d) ? (d["name"] | "") : "";
    if (!n[0]) return msg("Name fehlt", 400);
    snprintf(cfg.name, sizeof(cfg.name), "%s", n);
    settingsSave();
    radio::nameChanged();
    msg("Name gespeichert (Spotify übernimmt ihn nach dem Neustart)");
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
    const char* id = d["spotifyId"] | "";
    const char* sec = d["spotifySecret"] | "";
    const char* g = d["gemini"] | "";
    keys::set(id[0] ? id : nullptr, sec[0] ? sec : nullptr, g[0] ? g : nullptr);
    if (id[0] || sec[0]) spotify::keysChanged();
    msg("Schlüssel gespeichert");
  });
  srv->on("/api/spotify/use", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (body(d)) spotify::useAccount(d["i"] | -1);
    msg("Konto gewechselt");
  });
  srv->on("/api/spotify/remove", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (body(d)) spotify::removeAccount(d["i"] | -1);
    msg("Konto entfernt");
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
