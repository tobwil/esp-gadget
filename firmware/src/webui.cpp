#include "webui.h"
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <WebServer.h>
#include "config.h"
#include "favs.h"
#include "hw.h"
#include "keys.h"
#include "assist.h"
#include "kids.h"
#include "n8n.h"
#include "power.h"
#include "phone.h"
#include "routines.h"
#include "backup.h"
#include "music.h"
#include "net.h"
#include "radio.h"
#include "settings.h"
#include "ui/ui.h"
#include "voice.h"

extern const char TF_BOARD_TAG[];  // main.cpp

namespace webui {

// ------------------------------------------------------------------ Anfragen
// Die Weboberfläche läuft auf dem HTTPS-Server (phone.cpp). Dessen Task nimmt eine Anfrage an und übergibt sie an die
// Hauptschleife (Oberfläche und Musik sind nicht threadsicher); die Antwort schickt wieder der HTTPS-Task.
// Die Handler sprechen „srv->…“ wie beim Arduino-WebServer. Port 80 leitet nur noch auf https um.
struct Http {
  String uri, query, body, pin;  // Anfrage
  int code = 0;                  // Antwort
  String type, out, disp;
  const char* outP = nullptr;
  void (*fn)() = nullptr;
  volatile int state = 0;        // 0 frei, 1 wartet auf die Hauptschleife, 2 läuft dort
  SemaphoreHandle_t ready = nullptr;

  String arg(const char* name) const;
  String header(const char*) const { return pin; }  // nur X-Pin wird gebraucht
  void sendHeader(const char*, const String& v) { disp = v; }  // nur Content-Disposition
  void send(int c, const char* t, const String& b) { code = c; type = t; out = b; outP = nullptr; xSemaphoreGive(ready); }
  void send_P(int c, const char* t, const char* p) { code = c; type = t; out = ""; outP = p; xSemaphoreGive(ready); }
};

static Http req;
static Http* const srv = &req;
static WebServer* web80 = nullptr;

static String urlDecode(const String& v) {
  String o;
  for (unsigned i = 0; i < v.length(); i++) {
    char c = v[i];
    if (c == '+') c = ' ';
    else if (c == '%' && i + 2 < v.length()) { c = strtol(v.substring(i + 1, i + 3).c_str(), nullptr, 16); i += 2; }
    o += c;
  }
  return o;
}

String Http::arg(const char* name) const {
  if (!strcmp(name, "plain")) return body;
  int n = strlen(name);
  for (int i = 0; i < (int)query.length();) {
    int amp = query.indexOf('&', i);
    if (amp < 0) amp = query.length();
    if (!strncmp(query.c_str() + i, name, n) && (i + n == amp || query[i + n] == '='))
      return i + n == amp ? "" : urlDecode(query.substring(i + n + 1, amp));
    i = amp + 1;
  }
  return "";
}

struct Route { const char* path; bool post; void (*fn)(); };
static Route routes[32];
static int nRoutes = 0;

static void on(const char* path, http_method m, void (*fn)()) {
  if (nRoutes < 32) routes[nRoutes++] = {path, m == HTTP_POST, fn};
}

// Für das Handy gebaut: eine Spalte, große Schalter, unten die Reiter. Hell und dunkel nach Systemeinstellung.
static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0d0d0f">
<title>Taschenfunk</title>
<style>
:root{color-scheme:light dark;--bg:#f2f0eb;--card:#fff;--fill:#f0ede7;--fill2:#e4e0d8;--text:#16151a;--dim:#7a766e;--line:#e8e4dc;
--acc:#c98a2b;--acc2:#e9b862;--kid:#ec6436;--kid2:#ff9a6a;--ok:#159b67;--bad:#e0483e;--warn:#d98c12;--glass:rgba(250,248,244,.78);
--shadow:0 1px 2px rgba(30,24,12,.05),0 12px 32px -14px rgba(30,24,12,.22);--r:24px}
@media (prefers-color-scheme:dark){:root{--bg:#000;--card:#121214;--fill:#1d1d20;--fill2:#2a2a2e;--text:#f6f3ec;--dim:#8f8b84;--line:#242428;
--acc:#e8b660;--acc2:#f6d08a;--kid:#ff7a4a;--kid2:#ffa985;--ok:#34d399;--bad:#ff6b5f;--warn:#fbbf24;--glass:rgba(18,18,20,.74);
--shadow:0 0 0 1px rgba(255,255,255,.06)}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 -apple-system,BlinkMacSystemFont,"SF Pro Text",Inter,system-ui,sans-serif;-webkit-font-smoothing:antialiased}
header{position:sticky;top:0;z-index:5;padding:calc(14px + env(safe-area-inset-top)) 20px 12px;background:var(--glass);-webkit-backdrop-filter:saturate(180%) blur(20px);backdrop-filter:saturate(180%) blur(20px);border-bottom:1px solid var(--line)}
header h1{display:flex;align-items:center;flex-wrap:wrap;gap:8px;margin:0;font-size:28px;line-height:1.15;font-weight:780;letter-spacing:-.025em}
#stat{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 0}
.chip{display:inline-flex;align-items:center;gap:5px;padding:4px 10px;border-radius:99px;background:var(--fill);color:var(--dim);font-size:13px;font-weight:650;white-space:nowrap;max-width:100%;overflow:hidden;text-overflow:ellipsis}
.chip.g{color:var(--ok)}.chip.y{color:var(--warn)}.chip.r{color:var(--bad)}
.badge{padding:4px 11px;border-radius:99px;background:linear-gradient(135deg,var(--kid2),var(--kid));color:#fff;font-size:12px;font-weight:750;letter-spacing:.02em}
.badge[hidden]{display:none}
main{max-width:600px;margin:0 auto;padding:16px 16px calc(112px + env(safe-area-inset-bottom))}
section{margin:0 0 14px;padding:18px;border-radius:var(--r);background:var(--card);box-shadow:var(--shadow)}
h2{display:flex;align-items:center;gap:8px;margin:0 0 10px;color:var(--dim);font-size:12.5px;font-weight:750;letter-spacing:.07em;text-transform:uppercase}
h2:before{content:"";flex:none;width:8px;height:8px;border-radius:50%;background:linear-gradient(135deg,var(--acc2),var(--acc))}
.kidc h2:before{background:linear-gradient(135deg,var(--kid2),var(--kid))}
p.note,.note{margin:8px 0;color:var(--dim);font-size:14px;line-height:1.45}
label.f{display:block;margin:16px 0 6px;color:var(--dim);font-size:13px;font-weight:650}
input,select,textarea{display:block;width:100%;min-width:0;max-width:100%;min-height:48px;margin:0;padding:12px 14px;border:1px solid transparent;border-radius:14px;outline:0;background:var(--fill);color:var(--text);font:inherit;font-size:16px;-webkit-appearance:none;appearance:none;transition:border-color .15s,box-shadow .15s}
input:focus,select:focus,textarea:focus{border-color:var(--acc);box-shadow:0 0 0 4px rgba(201,138,43,.18)}
.kidc input:focus,.kidc select:focus,.kidc textarea:focus{border-color:var(--kid);box-shadow:0 0 0 4px rgba(236,100,54,.16)}
input[type=time]{font-variant-numeric:tabular-nums;text-align:left}
input[type=time]::-webkit-date-and-time-value{text-align:left}
input::placeholder,textarea::placeholder{color:var(--dim);opacity:.8}
select{padding-right:40px;overflow:hidden;white-space:nowrap;text-overflow:ellipsis;background-image:url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%238f8b84' stroke-width='2.5' stroke-linecap='round' stroke-linejoin='round'><path d='M6 9l6 6 6-6'/></svg>");background-repeat:no-repeat;background-position:right 14px center;background-size:16px}
textarea{resize:vertical;line-height:1.5}
input[type=file]{padding:12px;font-size:14px}
button{min-height:48px;padding:12px 18px;border:0;border-radius:14px;background:linear-gradient(135deg,var(--acc2),var(--acc));color:#1d1404;font:inherit;font-weight:700;cursor:pointer;box-shadow:0 8px 18px -10px rgba(201,138,43,.9);transition:transform .12s,filter .15s}
button:active{transform:scale(.97);filter:brightness(.96)}
button.wide{display:block;width:100%;margin-top:12px}
button.ghost{min-height:42px;padding:8px 14px;background:var(--fill);color:var(--text);font-weight:650;box-shadow:none}
button.on{background:linear-gradient(135deg,#43e3a5,var(--ok));color:#fff;box-shadow:none}
button.kid{background:linear-gradient(135deg,var(--kid2),var(--kid));color:#fff;box-shadow:0 8px 18px -10px rgba(236,100,54,.9)}
.bt{display:flex;flex:none;gap:6px}
.row{display:flex;align-items:center;gap:12px;min-height:58px;padding:8px 0;border-top:1px solid var(--line)}
.row:first-child,h2+.row{border-top:0}
.row .t{flex:1;min-width:0}.row .t b{display:block;overflow:hidden;font-weight:650;white-space:nowrap;text-overflow:ellipsis}
.row small{display:block;overflow:hidden;color:var(--dim);font-size:13px;white-space:nowrap;text-overflow:ellipsis}
.sw{position:relative;flex:none;width:51px;height:31px}
.sw input{position:absolute;inset:0;z-index:1;min-height:0;margin:0;opacity:0;cursor:pointer}
.sw span{position:absolute;inset:0;border-radius:99px;background:var(--fill2);transition:background .25s}
.sw span:after{content:"";position:absolute;top:2px;left:2px;width:27px;height:27px;border-radius:50%;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.28);transition:transform .3s cubic-bezier(.3,1.5,.5,1)}
.sw input:checked+span{background:linear-gradient(135deg,var(--acc2),var(--acc))}
.kidc .sw input:checked+span{background:linear-gradient(135deg,var(--kid2),var(--kid))}
.angry .sw input:checked+span{background:linear-gradient(135deg,#ff7a6e,#c9271d)}
.sw input:checked+span:after{transform:translateX(20px)}
.two{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:10px}.two>*{min-width:0}
.seg{display:flex;gap:2px;margin-top:12px;padding:4px;border-radius:14px;background:var(--fill)}
.seg button{flex:1;min-width:0;min-height:38px;padding:6px 4px;font-size:14.5px;letter-spacing:-.01em;overflow:hidden;border-radius:10px;background:transparent;color:var(--dim);font-weight:650;white-space:nowrap;text-overflow:ellipsis;box-shadow:none}
.seg button.sel{background:var(--card);color:var(--text);box-shadow:0 1px 2px rgba(0,0,0,.1),0 4px 12px -4px rgba(0,0,0,.18)}
@media (prefers-color-scheme:dark){.seg button.sel{background:var(--fill2)}}
.kv{display:grid;grid-template-columns:auto minmax(0,1fr);gap:9px 16px;font-size:15px}.kv span{min-width:0;overflow-wrap:anywhere}.kv span:nth-child(odd){color:var(--dim)}
.ok{color:var(--ok)}.bad{color:var(--bad)}
input[type=range]{height:30px;min-height:30px;padding:0;border:0;background:transparent;box-shadow:none}
input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:99px;background:var(--fill2)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:26px;height:26px;margin-top:-10px;border-radius:50%;background:#fff;box-shadow:0 2px 8px rgba(0,0,0,.3),0 0 0 4px var(--acc)}
.kidc input[type=range]::-webkit-slider-thumb{box-shadow:0 2px 8px rgba(0,0,0,.3),0 0 0 4px var(--kid)}
input[type=range]::-moz-range-track{height:6px;border-radius:99px;background:var(--fill2)}
input[type=range]::-moz-range-thumb{width:22px;height:22px;border:4px solid var(--acc);border-radius:50%;background:#fff}
nav{position:fixed;right:12px;bottom:calc(10px + env(safe-area-inset-bottom));left:12px;z-index:6;display:flex;max-width:560px;margin:0 auto;padding:6px;border-radius:28px;background:var(--glass);-webkit-backdrop-filter:saturate(180%) blur(22px);backdrop-filter:saturate(180%) blur(22px);box-shadow:0 0 0 1px var(--line),0 14px 34px -12px rgba(0,0,0,.4)}
nav button{display:flex;flex:1;flex-direction:column;align-items:center;justify-content:center;gap:3px;min-width:0;min-height:56px;padding:6px 2px;border-radius:22px;background:none;color:var(--dim);font-size:11px;font-weight:650;box-shadow:none}
nav svg{width:24px;height:24px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}
nav button.sel{background:rgba(201,138,43,.15);color:var(--acc)}nav button.sel.k{background:rgba(236,100,54,.14);color:var(--kid)}
.tab{display:none}.tab.sel{display:block;animation:tin .28s ease}
@keyframes tin{from{opacity:.6;transform:translateY(8px)}}
#msg{position:fixed;bottom:calc(96px + env(safe-area-inset-bottom));left:50%;z-index:8;display:none;max-width:90vw;padding:12px 18px;border-radius:16px;background:var(--text);color:var(--bg);font-weight:650;transform:translateX(-50%);box-shadow:0 12px 30px -10px rgba(0,0,0,.45)}
#lock{position:fixed;inset:0;z-index:9;display:none;flex-direction:column;justify-content:center;max-width:420px;margin:0 auto;padding:24px;background:var(--bg)}
#lock h1{margin:0 0 4px;font-size:30px;letter-spacing:-.02em}
/* Player */
#np{min-height:44px;color:var(--text);font-size:15px}#np b{display:block;font-size:18px;letter-spacing:-.01em}
.ctl{display:flex;align-items:center;justify-content:center;gap:16px;margin-top:16px}
.ctl button{display:grid;place-items:center;width:52px;height:52px;min-height:52px;padding:0;border-radius:50%;background:var(--fill);color:var(--text);box-shadow:none}
.ctl svg{width:22px;height:22px;fill:currentColor;stroke:currentColor;stroke-width:1.6;stroke-linejoin:round}
.ctl #mtog{width:70px;height:70px;background:linear-gradient(135deg,var(--acc2),var(--acc));color:#1d1404;box-shadow:0 10px 22px -10px rgba(201,138,43,.9)}
.ctl #mtog svg{width:28px;height:28px}
/* Übersicht: Akku-Ring und Kacheln */
.hero{display:flex;align-items:center;gap:18px}
.ring{position:relative;flex:none;width:92px;height:92px}
.ring svg{width:100%;height:100%;transform:rotate(-90deg)}
.ring b{position:absolute;inset:0;display:grid;place-items:center;font-size:23px;font-weight:780;letter-spacing:-.03em}
.hero .t{min-width:0}.hero .t b{display:block;font-size:19px;font-weight:720;letter-spacing:-.01em}.hero .t small{display:block;margin-top:2px;color:var(--dim);font-size:14px}
.tiles{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:10px;margin-top:18px}
.tiles button{display:flex;flex-direction:column;align-items:flex-start;justify-content:space-between;gap:10px;min-height:92px;margin:0;padding:14px 16px;border-radius:20px;text-align:left;line-height:1.25}
.tiles button i{font-size:24px;font-style:normal}
/* Tagesablauf als Zeitleiste: Uhrzeit · Punkt · Karte (Szene, ✕, eigener Name) */
.tl{position:relative;margin-top:14px}
.tl:before{content:"";position:absolute;top:24px;bottom:24px;left:70px;width:2px;border-radius:2px;background:linear-gradient(var(--kid2),var(--kid))}
.dr{position:relative;display:grid;grid-template-columns:62px minmax(0,1fr);align-items:start;gap:0 20px;padding:5px 0}
.dr:after{content:"";position:absolute;top:19px;left:64px;width:14px;height:14px;border:3px solid var(--kid);border-radius:50%;background:var(--card)}
.dr.fix:after{background:var(--kid)}
.dr input[type=time]{min-height:42px;padding:8px 2px;font-size:15px;font-weight:720;text-align:center}
.dr input[type=time]::-webkit-date-and-time-value{text-align:center}
.dr input[type=time]::-webkit-calendar-picker-indicator{display:none}
.dr .tm{display:block;padding-top:10px;font-weight:720;text-align:center;font-variant-numeric:tabular-nums}
.dc{display:grid;grid-template-columns:minmax(0,1fr) 34px;align-items:center;gap:2px 6px;min-width:0;padding:4px;border-radius:16px;background:var(--fill)}
.dr.fix .dc{background:none;padding-left:0}
.dc b{display:block;padding:6px 0 6px 4px;font-weight:650}
.dc select{min-height:34px;padding:4px 26px 4px 6px;background-color:transparent;background-position:right 4px center;font-size:15px;font-weight:600}
.dc .x{width:34px;min-height:34px;padding:0;border-radius:10px;background:none;color:var(--dim);font-size:14px;box-shadow:none}
.dc input.nm{grid-column:1/-1;min-height:28px;padding:2px 8px 6px;border:0;background:transparent;color:var(--dim);font-size:13px}
.dc input.nm:focus{color:var(--text);box-shadow:none}
@media (min-width:760px){main{padding-top:24px}}
</style></head><body>
<header><h1><span id="name">Taschenfunk</span><span class="badge" id="kidb" hidden>Kindermodus</span></h1><div id="stat"><span class="chip">verbinde …</span></div></header>
<main>
<div class="tab sel" id="t-home">
 <section><div class="hero" id="hero"></div>
  <div class="tiles"><button id="phonego"><i>🎙</i>Funk vom Handy</button><button class="ghost" id="findgo"><i>🔔</i>Uhr klingeln lassen</button></div>
  <p class="note">Funk: Knopf halten und sprechen, die Uhr spielt deine Stimme ab. Antwort: auf der Uhr BOOT halten.</p></section>
 <section><h2>Uhr</h2><div class="kv" id="info"></div></section>
 <section><h2>Akku</h2><div class="kv" id="batkv"></div><svg id="batchart" viewBox="0 0 360 140" style="width:100%;height:auto;margin-top:10px"></svg>
  <div class="note" id="batleg"><span style="color:var(--acc)">━</span> entlädt &nbsp; <span style="color:var(--ok)">━</span> lädt &nbsp; <span style="color:var(--dim)">┄</span> Uhr aus / Tiefschlaf</div>
  <label class="f" for="pmode">Sparmodus</label><select id="pmode"><option value="1">Automatisch ab 20 %</option><option value="2">Immer an</option><option value="0">Aus</option></select>
  <p class="note">Sparmodus: Display höchstens 40 % und nach 10 s aus, kein Funk im Hintergrund, Tiefschlaf schon nach 3 Minuten.</p>
  <label class="f" for="pdeep">Tiefschlaf</label><select id="pdeep"><option value="0">Nie – immer erreichbar</option><option value="10">Nach 10 Min. dunkel</option><option value="30">Nach 30 Min. dunkel</option><option value="60">Nach 1 Std. dunkel</option></select>
  <div class="row"><div class="t"><b>Nachtruhe im Kindermodus</b><small>Tiefschlaf von Bettzeit + Einschlafzeit bis zum Aufstehen</small></div><label class="sw"><input type="checkbox" id="nkids"><span></span></label></div>
  <div class="row"><div class="t"><b>Nachtruhe sonst</b><small>Tiefschlaf zwischen diesen Zeiten</small></div><label class="sw"><input type="checkbox" id="nadult"><span></span></label></div>
  <div class="two"><div><label class="f" for="nfrom">von</label><input type="time" id="nfrom"></div><div><label class="f" for="nto">bis</label><input type="time" id="nto"></div></div>
  <p class="note">In der Nachtruhe spart die Uhr am meisten, ist aber nicht erreichbar (kein Funk, keine Weboberfläche). Der Wecker klingelt trotzdem, BOOT weckt sie jederzeit.</p>
  <p class="note">Mit dunklem Display döst die Uhr sparsam und bleibt für Funk und diese Seite erreichbar. Im Tiefschlaf hält der Akku am längsten, aber dann ist auch das WLAN aus: kein Funk, keine Weboberfläche, wecken nur mit BOOT.</p></section>
 <section class="kidc"><div class="row"><div class="t"><b>Kindermodus</b><small id="kidsum"></small></div><label class="sw"><input type="checkbox" id="kon2"><span></span></label></div>
  <div class="row"><div class="t"><b>Wutmodus</b><small>Nur das Wut-Glas</small></div><label class="sw"><input type="checkbox" id="kangry2"><span></span></label></div></section>
</div>

<div class="tab" id="t-kids">
 <section class="kidc"><div class="row"><div class="t"><b>Kindermodus</b><small>Auf der Uhr keine freie Auswahl mehr</small></div><label class="sw"><input type="checkbox" id="kon"><span></span></label></div>
  <div class="row angry"><div class="t"><b>Wutmodus</b><small id="angst"></small></div><label class="sw"><input type="checkbox" id="kangry"><span></span></label></div>
  <p class="note">Wutmodus: Auf der Uhr gibt es nur noch das Wut-Glas – kräftig schütteln, bis alle roten Funken draußen sind, dann wird es ruhig und ein Kreis atmet vor.</p></section>
 <section class="kidc"><h2>Zifferblatt</h2><p class="note">Eine Welt für die Pixel-Geschichte, ein Tier, das überall mitkommt, und in der Spielzeit ein Abenteuer mit jeden Tag einem neuen Kapitel.</p>
  <label class="f" for="kworld">Welt</label><select id="kworld"><option value="5">✨ Jede Woche eine andere</option><option value="0">🏠 Zuhause</option><option value="1">🚀 Weltraum</option><option value="2">🚜 Bauernhof</option><option value="3">🐠 Unterwasser</option><option value="4">🦄 Märchenland</option></select>
  <div class="two"><div><label class="f" for="kpet">Tier</label><select id="kpet"><option value="0">keins</option><option value="1">🐱 Katze</option><option value="2">🐰 Hase</option><option value="3">🐶 Hund</option><option value="4">🐉 Drache</option></select></div><div><label class="f" for="kpetn">Name</label><input id="kpetn" maxlength="12" autocomplete="off"></div></div>
  <p class="note" id="kpetst"></p>
  <div class="row"><div class="t"><b>Wochen-Abenteuer</b><small>In der Spielzeit eine Karte, jeden Tag ein Kapitel</small></div><label class="sw"><input type="checkbox" id="kadv"><span></span></label></div>
  <div class="row"><div class="t"><b>Tagesablauf zeigen</b><small>Aus: tagsüber spielen, nachts schlafen – mit Wochentag und Datum</small></div><label class="sw"><input type="checkbox" id="kphases"><span></span></label></div></section>
 <section class="kidc"><h2>Tagesablauf</h2><p class="note">Das Zifferblatt zeigt den Tag als Pixel-Geschichte.</p>
  <div class="two"><div><label class="f" for="kw">Aufstehen</label><input type="time" id="kw"></div><div><label class="f" for="kb">Ins Bett</label><input type="time" id="kb"></div></div>
  <div class="seg" id="kplace"><button data-v="0">Kita</button><button data-v="1">Schule</button></div>
  <label class="f">Ablauf <span class="note" id="dst"></span></label>
  <div class="seg" id="dday"><button data-v="weekday" class="sel">Mo–Fr</button><button data-v="weekend">Sa &amp; So</button></div>
  <div id="dlist"></div>
  <button class="wide ghost" id="dadd">+ Abschnitt</button><button class="wide kid" id="dsave">Ablauf speichern</button><button class="wide ghost" id="dreset">Standard wiederherstellen</button>
  <p class="note">Aufstehen und Schlafenszeit kommen aus den Zeiten oben. Jeder Abschnitt zeigt eine Szene der Pixel-Geschichte („Kita/Schule“ wird am Wochenende zu „Spielen“); der Name ist frei, z. B. „Schwimmkurs“ mit der Szene „Draußen spielen“.</p>
 </section>
 <section class="kidc"><h2>Routinen mit Sternen</h2>
  <div class="row"><div class="t"><b>Aufgaben auf der Uhr</b><small>Morgens und abends abhaken, Sterne sammeln</small></div><label class="sw"><input type="checkbox" id="ron"><span></span></label></div>
  <p class="note" id="rst2"></p><div id="rnow"></div>
  <label class="f" for="rmorn">Morgens (ab dem Aufstehen, eine Aufgabe pro Zeile)</label><textarea id="rmorn" rows="4"></textarea>
  <label class="f" for="reve">Abends (2 Stunden vor der Bettzeit)</label><textarea id="reve" rows="4"></textarea>
  <button class="wide kid" id="rgo">Routinen speichern</button><button class="wide ghost" id="rreset">Sterne zurücksetzen</button></section>
 <section class="kidc"><h2>Balance-Rekorde</h2><p class="note" id="grec"></p><button class="wide ghost" id="greset">Rekorde zurücksetzen</button></section>
 <section class="kidc"><h2>Schlafenszeit</h2>
  <div class="row"><div class="t"><b>Nachts sperren</b><small>Ab der Bettzeit bis zum Aufstehen: keine Apps, kein Funk, gedimmt</small></div><label class="sw"><input type="checkbox" id="knight"><span></span></label></div>
  <label class="f" for="kstory">Abends hören: so lange nach der Bettzeit</label>
  <select id="kstory"><option value="0">gar nicht – Musik endet zur Bettzeit</option><option value="15">15 Minuten</option><option value="30">30 Minuten</option><option value="45">45 Minuten</option><option value="60">1 Stunde</option><option value="90">1,5 Stunden</option><option value="120">2 Stunden</option></select>
  <p class="note" id="kstst"></p></section>
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
 <section><h2>Läuft auf der Uhr</h2><div id="np" class="note">Nichts</div>
  <div class="ctl"><button id="mstop" aria-label="Stopp"><svg viewBox="0 0 24 24"><rect x="7" y="7" width="10" height="10" rx="2"/></svg></button><button id="mprev" aria-label="Zurück"><svg viewBox="0 0 24 24"><path d="M18 6v12l-8.5-6zM6 6v12"/></svg></button><button id="mtog" aria-label="Wiedergabe/Pause"><svg viewBox="0 0 24 24"><path d="M7 5v14l7-7zM16 6v12M20 6v12"/></svg></button><button id="mnext" aria-label="Weiter"><svg viewBox="0 0 24 24"><path d="M6 6v12l8.5-6zM18 6v12"/></svg></button></div>
  <label class="f" for="mvol">Lautstärke <span id="mvolv"></span> %</label><input type="range" id="mvol" min="0" max="100" step="5">
  <label class="f" for="msleep">Sleep-Timer</label><select id="msleep"><option value="0">aus</option><option value="15">15 Min.</option><option value="30">30 Min.</option><option value="45">45 Min.</option><option value="60">60 Min.</option><option value="90">90 Min.</option></select></section>
 <section><h2>Abspielen</h2>
  <div class="seg" id="msrc"><button data-v="kids" class="sel">Freigegeben</button><button data-v="favs">Favoriten</button><button data-v="search">Suche</button></div>
  <div id="msearch" hidden><div class="seg" id="mss"><button data-v="radio" class="sel">Radio</button><button data-v="ard">Audiothek</button><button data-v="podcast">Podcasts</button></div>
   <input id="mq" type="search" placeholder="Sender, Sendung, Podcast" enterkeyhint="search" style="margin-top:10px"></div>
  <p class="note">🧸 + gibt Sender, ganze Sendungen und Podcasts oder einzelne Folgen für die Kinder frei, 🧸 ✓ heißt freigegeben.</p>
  <p class="note" id="mpath"></p><div id="mlist"></div></section>
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
 <section><h2>Display</h2><label class="f" for="dbr">Helligkeit <span id="dbrv"></span> %</label><input type="range" id="dbr" min="10" max="100" step="10">
  <label class="f" for="doff">Display aus nach</label><select id="doff"><option value="10">10 Sekunden</option><option value="15">15 Sekunden</option><option value="30">30 Sekunden</option><option value="60">1 Minute</option><option value="120">2 Minuten</option><option value="0">nie</option></select>
  <p class="note">AMOLED: Schwarz kostet nichts, Helligkeit viel. Wird die Uhr nur durch Funk geweckt, leuchtet sie gedimmt und geht 8 s nach dem Funk wieder aus.</p></section>
 <section><h2>WLAN</h2><div id="wifi"></div>
  <label class="f" for="ss">Netz hinzufügen</label><input id="ss" placeholder="Name (SSID)" autocapitalize="off">
  <label class="f" for="pw">Passwort</label><input id="pw" type="password" placeholder="leer = offenes Netz">
  <button class="wide" id="wgo">Hinzufügen und verbinden</button></section>
 <section><h2>Spracheingabe</h2><p id="gst" class="note"></p><label class="f" for="gem">Gemini-API-Schlüssel</label><input id="gem" type="password" autocomplete="off" placeholder="unverändert lassen"><button class="wide" id="gemgo">Speichern</button></section>
 <section><h2>Firmware</h2><p class="note" id="fwst"></p>
  <label class="f" for="fwfile">Neue Firmware (firmware.bin aus dem Build)</label><input id="fwfile" type="file" accept=".bin">
  <button class="wide" id="fwgo">Aktualisieren</button><p class="note" id="fwprog"></p></section>
 <section><h2>Sicherung</h2><p class="note">Alle Einstellungen als Datei – zum Wiederherstellen oder für eine zweite Uhr.</p>
  <div class="row"><div class="t"><b>Mit Geheimnissen</b><small>WLAN-Passwörter, Gemini-Schlüssel, n8n-Token, Eltern-PIN</small></div><label class="sw"><input type="checkbox" id="bsec"><span></span></label></div>
  <button class="wide" id="bgo">Sicherung herunterladen</button>
  <label class="f" for="bfile">Sicherung einspielen</label><input id="bfile" type="file" accept=".json,application/json">
  <button class="wide ghost" id="bload">Einspielen und neu starten</button></section>
 <section><button class="wide ghost" id="rst">Uhr neu starten</button></section>
</div>
</main>
<nav id="nav"><button data-t="home" class="sel"><svg viewBox="0 0 24 24"><rect x="6" y="6" width="12" height="12" rx="4"/><path d="M9 6l1-3h4l1 3M9 18l1 3h4l1-3M12 9.5V12l1.6 1.2"/></svg>Übersicht</button><button data-t="kids" class="k"><svg viewBox="0 0 24 24"><circle cx="6.5" cy="6.5" r="2.5"/><circle cx="17.5" cy="6.5" r="2.5"/><circle cx="12" cy="13.5" r="7"/><path d="M9.5 15.5c1.4 1.2 3.6 1.2 5 0"/><path d="M9.5 11.5h.01M14.5 11.5h.01" stroke-width="2.6"/></svg>Kinder</button><button data-t="music"><svg viewBox="0 0 24 24"><path d="M9 18V5l11-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="17" cy="16" r="3"/></svg>Musik</button><button data-t="ai"><svg viewBox="0 0 24 24"><path d="M11 3l1.9 5.1L18 10l-5.1 1.9L11 17l-1.9-5.1L4 10l5.1-1.9z"/><path d="M18.5 14.5l.8 2.2 2.2.8-2.2.8-.8 2.2-.8-2.2-2.2-.8 2.2-.8z"/></svg>KI</button><button data-t="dev"><svg viewBox="0 0 24 24"><path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/></svg>Gerät</button></nav>
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
 const bc=s.battery<0||s.charging?'g':s.battery<=20?'r':s.battery<=40?'y':'g';
 $('stat').innerHTML=`<span class="chip ${bc}">${s.battery<0?'USB':s.battery+' %'}${s.charging?' ⚡':''}</span><span class="chip">${s.ssid?esc(s.ssid):'offline'}</span>`;
 const pr=s.power||{},rm=pr.remainMin,ring=s.battery<0?100:s.battery,rc=bc=='r'?'var(--bad)':bc=='y'?'var(--warn)':'var(--ok)';
 $('hero').innerHTML=`<div class="ring"><svg viewBox="0 0 100 100"><circle cx="50" cy="50" r="42" fill="none" stroke="var(--fill2)" stroke-width="10"/><circle cx="50" cy="50" r="42" fill="none" stroke="${rc}" stroke-width="10" stroke-linecap="round" stroke-dasharray="${(ring*2.639).toFixed(1)} 264"/></svg><b>${s.battery<0?'USB':s.battery+'%'}</b></div>
  <div class="t"><b>${s.charging?'Lädt':rm>=0?'noch ca. '+(rm>=60?Math.floor(rm/60)+' h '+String(rm%60).padStart(2,'0')+' min':rm+' min'):'Akku'}</b><small>${esc(pr.sleep||'')}</small><small>${esc(s.name)} · ${s.peers} Funk-Nachbar${s.peers==1?'':'n'}</small></div>`;
 $('info').innerHTML=`<span>Akku</span><span>${s.battery<0?'kein Akku':s.battery+' %'}${s.charging?' · lädt':''}</span><span>WLAN</span><span>${esc(s.ssid||'–')}${s.rssi?' ('+s.rssi+' dBm)':''}</span>
  <span>Adresse</span><span>${esc(s.ip)}</span><span>Funk-Nachbarn</span><span>${s.peers}</span><span>Version</span><span>${esc(s.version)}</span>`;
 $('kon').checked=$('kon2').checked=k.on;$('kangry').checked=$('kangry2').checked=k.angry&&k.on;
 $('angst').textContent=(k.angryToday?`Heute ${k.angryToday}× Wut rausgeschüttelt`:'Heute noch nicht benutzt')+(k.angryLast?' · zuletzt '+new Date(k.angryLast*1000).toLocaleTimeString('de',{hour:'2-digit',minute:'2-digit'}):'');
 $('kidb').textContent=k.angry&&k.on?'Wutmodus':'Kindermodus';
 $('kidsum').textContent=k.on?`An · ${k.released.length} Freigaben · bis ${k.maxVol} %`:'Aus';
 if(document.activeElement!==$('kw'))$('kw').value=hm(k.wake);if(document.activeElement!==$('kb'))$('kb').value=hm(k.bed);
 [...$('kplace').children].forEach(b=>b.classList.toggle('sel',+b.dataset.v==+k.school));
 const r=s.routines;
 $('grec').innerHTML=s.game?`Ins Ziel: ${s.game.level?'<b>Stufe '+s.game.level+'</b>':'noch keiner'} · Alle Kugeln: ${s.game.ms?'<b>'+(s.game.ms/1000).toFixed(1).replace('.',',')+' s</b>':'noch keiner'}`:'';
 $('rst2').innerHTML=`⭐ heute <b>${r.today}</b> · Woche <b>${r.week}</b> · insgesamt <b>${r.total}</b>`;
 $('rnow').innerHTML=[['Morgens',r.morning],['Abends',r.evening]].map(([n,l])=>`<p class="note">${n}: `+l.map(i=>(i.done?'✅ ':'⬜ ')+esc(i.text)).join(' · ')+'</p>').join('');
 if(document.activeElement!==$('rmorn'))$('rmorn').value=r.morning.map(i=>i.text).join('\n');
 if(document.activeElement!==$('reve'))$('reve').value=r.evening.map(i=>i.text).join('\n');
 if(!dDirty&&k.day){dPlan=JSON.parse(JSON.stringify(k.day));dRender()}$('dst').textContent=k.day&&k.day.custom?'· eigener':'· Standard';$('knight').checked=k.nightLock;
 if(document.activeElement!==$('kworld'))$('kworld').value=k.world;if(document.activeElement!==$('kpet'))$('kpet').value=k.pet;
 if(document.activeElement!==$('kpetn'))$('kpetn').value=k.petName;$('kadv').checked=k.adventure;
 {const st=s.routines?s.routines.total:0,nm=esc(k.petName||'Das Tier');$('kpetst').innerHTML=!k.pet?'':st>=60?`${nm} trägt Halstuch und Krone – ${st} Sterne insgesamt ⭐`:st>=20?`${nm} hat ein Halstuch. Noch <b>${60-st}</b> Sterne bis zur Krone.`:`Noch <b>${20-st}</b> Sterne, dann bekommt ${nm} ein Halstuch. Bei Sternen von heute freut sich ${nm} mit Herzchen.`;}if(document.activeElement!==$('kstory'))$('kstory').value=k.storyMin;
 $('kstst').textContent=k.storyMin?`Tagsüber ohne Grenze. Ab ${hm(k.bed)} nur noch Musik, ab ${hm((k.bed+k.storyMin)%1440)} wird sie sanft ausgeblendet und ist bis ${hm(k.wake)} gesperrt.`:`Tagsüber ohne Grenze. Ab ${hm(k.bed)} wird Musik ausgeblendet und ist bis ${hm(k.wake)} gesperrt.`;
 $('kphases').checked=k.phases;if(s.routines)$('ron').checked=s.routines.on;$('kv').value=k.maxVol;$('kvv').textContent=k.maxVol;
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
  <span>Verbrauch</span><span>${pw.drain>0?pw.drain.toFixed(1)+' %/h':'–'}</span><span>Sparmodus</span><span>${pw.saving?'an':'aus'}</span>
  <span>Tiefschlaf</span><span>${pw.sleep||'–'}</span>`;
 if(document.activeElement!==$('pmode'))$('pmode').value=pw.mode;
 if(s.display&&document.activeElement!==$('dbr')){$('dbr').value=s.display.brightness;$('dbrv').textContent=s.display.brightness}
 if(s.display&&document.activeElement!==$('doff'))$('doff').value=[10,15,30,60,120,0].includes(s.display.screenOff)?s.display.screenOff:30;
 if(document.activeElement!==$('pdeep'))$('pdeep').value=pw.deep;
 $('nkids').checked=pw.nightKids;$('nadult').checked=pw.nightAdult;
 if(document.activeElement!==$('nfrom'))$('nfrom').value=hm(pw.nightFrom);if(document.activeElement!==$('nto'))$('nto').value=hm(pw.nightTo);
 batChart(pw.history);
 const mu=s.music;$('np').innerHTML=mu.active?`<b>${mu.playing?'▶':'⏸'} ${esc(mu.title)}</b><br>${esc(mu.artist)}${mu.dur?' · '+Math.floor(mu.pos/60)+':'+String(Math.floor(mu.pos%60)).padStart(2,'0')+' / '+Math.floor(mu.dur/60)+':'+String(mu.dur%60).padStart(2,'0'):''}${mu.msg?'<br><span class="bad">'+esc(mu.msg)+'</span>':''}`:mu.last?`<b>Zuletzt gehört</b>${esc(mu.last.title)}<br><span class="note">angehalten bei ${Math.floor(mu.last.pos/60)}:${String(mu.last.pos%60).padStart(2,'0')}${mu.last.dur?' von '+Math.floor(mu.last.dur/60)+':'+String(mu.last.dur%60).padStart(2,'0'):''}</span><br><button class="ghost" style="margin-top:8px" onclick="post('music/resume',{},true)">▶ Weiter hören</button> <button class="ghost" style="margin-top:8px" onclick="post('music/forget',{},true)">✕ Vergessen</button>`:'Gerade läuft nichts.';
 if(document.activeElement!==$('mvol')){$('mvol').value=mu.volume;$('mvolv').textContent=mu.volume}
 if(document.activeElement!==$('msleep'))$('msleep').value=[0,15,30,45,60,90].reduce((a,v)=>Math.abs(v-mu.sleepMin)<Math.abs(a-mu.sleepMin)?v:a,0);
 if($('t-music').classList.contains('sel')&&!mStack.length)mStart();
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
$('kworld').onchange=e=>kids({world:+e.target.value});$('kpet').onchange=e=>kids({pet:+e.target.value});
$('kpetn').onchange=e=>kids({petName:e.target.value.trim()});$('kadv').onchange=e=>kids({adventure:e.target.checked});
$('kphases').onchange=e=>kids({phases:e.target.checked});$('ron').onchange=e=>post('routines/on',{on:e.target.checked});
const lines=id=>$(id).value.split('\n').map(x=>x.trim()).filter(Boolean).slice(0,8);
$('rgo').onclick=()=>post('routines',{morning:lines('rmorn'),evening:lines('reve')});
$('greset').onclick=()=>confirm('Balance-Rekorde auf dieser Uhr löschen?')&&post('game/reset',{});
$('rreset').onclick=()=>confirm('Alle Sterne auf 0 setzen?')&&post('routines/reset',{});
$('bgo').onclick=async()=>{try{const r=await fetch('/api/backup'+($('bsec').checked?'?secrets=1':''),{headers:{'X-Pin':pin}});
 if(!r.ok)return toast('Fehler '+r.status);const b=await r.blob(),a=document.createElement('a');a.href=URL.createObjectURL(b);
 a.download=(S?S.name:'taschenfunk')+'-sicherung.json';a.click()}catch(e){toast('Uhr nicht erreichbar')}};
$('bload').onclick=async()=>{const f=$('bfile').files[0];if(!f)return toast('Erst eine Datei wählen');
 if(!confirm('Einstellungen aus der Datei übernehmen und neu starten?'))return;post('restore',JSON.parse(await f.text()))};
// ------------------------------------------------ Fernbedienung
let mItems=[],mStack=[],mSrc='kids',mSs='radio';
let mTitle='',mErr='';
async function mOpen(path,title,push){$('mlist').innerHTML='<p class="note">Lade …</p>';
 try{const j=await api('music/list?path='+encodeURIComponent(path));if(push)mStack.push({path,title});
  mTitle=title||j.title||'';mErr=j.err||'';mItems=j.items||[];mRender()}catch(e){$('mlist').innerHTML=''}}
// Freigeben für den Kindermodus: Radiosender, Audiothek-Sendungen und Podcasts (nicht einzelne Folgen)
const relOk=id=>/^(radio:|pc:feed:|ard:set:|ard:a:|https?:)/.test(id);  // Sender, Sendungen, Podcasts und einzelne Folgen
function mRelBtn(path,title,sub){const on=released(path);return btn(on?'🧸 ✓':'🧸 +',`mRel(${J({path,title,sub})},${!on})`,on?'on':'')}
function mRel(it,on){post('kids/release',{...it,on},true).then(()=>{toast(on?'Für die Kinder freigegeben':'Freigabe entfernt');mRender()})}
function mRender(){const cur=mStack[mStack.length-1];
  $('mpath').innerHTML=(mStack.length>1?`<a href="#" onclick="mBack();return false">← zurück</a> · `:'')+esc(mTitle)+
   (cur&&relOk(cur.path)?' '+mRelBtn(cur.path,mTitle,''):'');
  $('mlist').innerHTML=mItems.map((it,i)=>row(it.title,it.sub,'<span class="bt">'+(relOk(it.id)?mRelBtn(it.id,it.title,it.sub):'')+
   (it.kind==1?btn('Öffnen',`mOpen(${J(it.id)},${J(it.title)},true)`):btn('▶',`mPlay(${i})`))+'</span>')).join('')||'<p class="note">'+esc(mErr||'Nichts da')+'</p>'}
function mBack(){mStack.pop();const t=mStack.pop();if(t)mOpen(t.path,t.title,true)}
function mPlay(i){post('music/play',{items:mItems,index:i},true).then(()=>toast('Läuft auf der Uhr'))}
function mStart(){mStack=[];if(mSrc=='search'){if(val('mq'))mOpen((mSs=='radio'?'rlsearch:':mSs=='ard'?'ardsearch:':'pcsearch:')+val('mq'),'',true);else $('mlist').innerHTML=''}else mOpen(mSrc,'',true)}
$('msrc').onclick=e=>{if(!e.target.dataset.v)return;mSrc=e.target.dataset.v;[...$('msrc').children].forEach(b=>b.classList.toggle('sel',b==e.target));$('msearch').hidden=mSrc!='search';mStart()};
$('mss').onclick=e=>{if(!e.target.dataset.v)return;mSs=e.target.dataset.v;[...$('mss').children].forEach(b=>b.classList.toggle('sel',b==e.target));mStart()};
$('mq').onkeydown=e=>{if(e.key=='Enter')mStart()};
const ctl=c=>post('music/ctl',c,true);
$('mprev').onclick=()=>ctl({cmd:'prev'});$('mtog').onclick=()=>ctl({cmd:'toggle'});$('mnext').onclick=()=>ctl({cmd:'next'});$('mstop').onclick=()=>ctl({cmd:'stop'});
$('mvol').oninput=e=>$('mvolv').textContent=e.target.value;$('mvol').onchange=e=>ctl({volume:+e.target.value});
$('msleep').onchange=e=>ctl({sleep:+e.target.value});
document.querySelector('nav button[data-t=music]').addEventListener('click',()=>{if(!mStack.length)mStart()});
// Tagesablauf: Plan für Mo–Fr und Sa/So bearbeiten (bleibt beim automatischen Neuladen stehen, bis gespeichert)
const SC=[[2,'🥣 Frühstück'],[3,'🏫 Kita/Schule'],[4,'🍝 Mittagessen'],[5,'😴 Mittagsruhe'],[6,'🛝 Draußen spielen'],[7,'🍲 Abendessen'],[8,'🛁 Baden & Zähne putzen'],[9,'📖 Gute-Nacht-Geschichte']];
let dPlan=null,dDay='weekday',dDirty=false;
function dRender(){if(!dPlan||!S)return;const L=dPlan[dDay],k=S.kids;
 const fix=(t,n)=>`<div class="dr fix"><span class="tm">${hm(t)}</span><div class="dc"><b>${n}</b></div></div>`;
 $('dlist').innerHTML='<div class="tl">'+fix(k.wake,'☀️ Aufstehen')+L.map((e,i)=>`<div class="dr"><input type="time" value="${hm(e.t)}" aria-label="Uhrzeit" onchange="dSet(${i},'t',mins(this.value))"><div class="dc"><select aria-label="Szene" onchange="dSet(${i},'a',+this.value)">${SC.map(([v,l])=>`<option value="${v}"${v==e.a?' selected':''}>${l}</option>`).join('')}</select><button class="x" aria-label="Entfernen" onclick="dDel(${i})">✕</button><input class="nm" placeholder="✎ eigener Name" maxlength="23" value="${esc(e.n||'')}" oninput="dSet(${i},'n',this.value)"></div></div>`).join('')+fix(k.bed,'🌙 Schlafenszeit')+'</div>'}
function dSet(i,k,v){dPlan[dDay][i][k]=v;dDirty=true;if(k=='t'){dPlan[dDay].sort((a,b)=>a.t-b.t);dRender()}}
function dDel(i){dPlan[dDay].splice(i,1);dDirty=true;dRender()}
$('dadd').onclick=()=>{const L=dPlan[dDay];L.push({t:L.length?Math.min(L[L.length-1].t+60,23*60):12*60,a:6,n:''});dDirty=true;dRender()};
$('dday').onclick=e=>{if(!e.target.dataset.v)return;dDay=e.target.dataset.v;[...$('dday').children].forEach(b=>b.classList.toggle('sel',b==e.target));dRender()};
$('dsave').onclick=()=>{dDirty=false;post('kids/day',{weekday:dPlan.weekday,weekend:dPlan.weekend})};
$('dreset').onclick=()=>{if(!confirm('Eigenen Ablauf verwerfen und den Standard nehmen?'))return;dDirty=false;post('kids/day',{reset:true})};
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
$('phonego').onclick=()=>{location.href='/funk'};
$('findgo').onclick=()=>post('find',{on:true});
$('pmode').onchange=e=>post('power',{mode:+e.target.value});
$('dbr').oninput=e=>$('dbrv').textContent=e.target.value;$('dbr').onchange=e=>post('display',{brightness:+e.target.value},true);
$('doff').onchange=e=>post('display',{screenOff:+e.target.value});
$('pdeep').onchange=e=>post('power',{deep:+e.target.value});
$('nkids').onchange=e=>post('power',{nightKids:e.target.checked});$('nadult').onchange=e=>post('power',{nightAdult:e.target.checked});
$('nfrom').onchange=e=>post('power',{nightFrom:mins(e.target.value)});$('nto').onchange=e=>post('power',{nightTo:mins(e.target.value)});
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
// Akku-Verlauf: Prozent (0–100) über Uhrzeit; Entladen orange, Laden grün, Lücken (Uhr aus, Tiefschlaf) gestrichelt
function batChart(hist){
 const h=hist.slice().sort((a,b)=>b[0]-a[0]),el=$('batchart');
 if(h.length<2){el.innerHTML='<text x="0" y="60" font-size="12" fill="var(--dim)">Verlauf: alle 2 Minuten ein Messpunkt</text>';return}
 const span=Math.max(60,Math.ceil(h[0][0]/60)*60),X0=30,X1=350,Y0=8,Y1=118;
 const X=m=>(X1-m/span*(X1-X0)).toFixed(1),Y=p=>(Y1-p/100*(Y1-Y0)).toFixed(1);
 let g='';
 for(const p of [0,25,50,75,100])g+=`<line x1="${X0}" x2="${X1}" y1="${Y(p)}" y2="${Y(p)}" stroke="var(--line)"/><text x="${X0-5}" y="${+Y(p)+3}" font-size="9" text-anchor="end" fill="var(--dim)">${p} %</text>`;
 const now=Date.now(),step=span<=120?30:span<=240?60:120;
 for(let m=0;m<=span;m++){const t=new Date(now-m*60000);if((t.getHours()*60+t.getMinutes())%step)continue;
  g+=`<line x1="${X(m)}" x2="${X(m)}" y1="${Y0}" y2="${Y1}" stroke="var(--line)" stroke-dasharray="2 3"/><text x="${X(m)}" y="${Y1+14}" font-size="9" text-anchor="middle" fill="var(--dim)">${String(t.getHours()).padStart(2,'0')}:${String(t.getMinutes()).padStart(2,'0')}</text>`}
 for(let i=1;i<h.length;i++){const a=h[i-1],b=h[i],gap=a[0]-b[0]>6;
  g+=`<line x1="${X(a[0])}" y1="${Y(a[1])}" x2="${X(b[0])}" y2="${Y(b[1])}" stroke="${gap?'var(--dim)':b[3]?'var(--ok)':'var(--acc)'}" stroke-width="${gap?1.2:2.5}"${gap?' stroke-dasharray="3 3"':''} stroke-linecap="round"/>`}
 const e=h[h.length-1];
 g+=`<circle cx="${X(e[0])}" cy="${Y(e[1])}" r="3.5" fill="${e[3]?'var(--ok)':'var(--acc)'}"/><text x="${X(e[0])-6}" y="${+Y(e[1])-7}" font-size="10" text-anchor="end" fill="var(--text)">${e[1]} %</text>`;
 el.innerHTML=g}
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

static void otaPrep() {  // in der Hauptschleife: Wiedergabe und KI vor dem Schreiben anhalten
  music::stop();
  assist::close();
  srv->send(200, "text/plain", "");
}

static esp_err_t respond(httpd_req_t* r);
static bool runInLoop(void (*fn)(), uint32_t ms);

// Firmware kommt als multipart/form-data mit genau einer Datei; sie wird direkt in den HTTPS-Task geschrieben
static esp_err_t otaReceive(httpd_req_t* r) {
  static char* buf = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  otaErr = "";
  otaTag = false;
  otaTailLen = 0;
  char ct[128] = "";
  httpd_req_get_hdr_value_str(r, "Content-Type", ct, sizeof(ct));
  const char* bnd = strstr(ct, "boundary=");
  if (!pinOk()) otaErr = "PIN nötig";
  else if (!bnd) otaErr = "Ungültige Anfrage";
  else runInLoop(otaPrep, 5000);
  size_t left = r->content_len, have = 0, data = 0;
  char* hdrEnd = nullptr;
  while (!otaErr.length() && !hdrEnd && left && have < 1024) {  // Kopf des Datei-Teils
    int n = httpd_req_recv(r, buf + have, min(left, (size_t)1024 - have));
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) { otaErr = "Übertragung abgebrochen"; break; }
    have += n;
    left -= n;
    hdrEnd = (char*)memmem(buf, have, "\r\n\r\n", 4);
  }
  if (!otaErr.length()) {
    if (!hdrEnd) otaErr = "Ungültige Anfrage";
    else {
      // hinter der Datei folgt nur noch „\r\n--<boundary>--\r\n“
      size_t head = hdrEnd + 4 - buf, tail = strlen(bnd + 9) + 8;
      data = r->content_len > head + tail ? r->content_len - head - tail : 0;
      Serial.printf("Update: %u Bytes werden empfangen\n", (unsigned)data);
      if (!data || !Update.begin(data)) otaErr = data ? Update.errorString() : "Leere Datei";
      size_t k = min(have - head, data);
      otaScan((uint8_t*)buf + head, k);
      if (!otaErr.length() && Update.write((uint8_t*)buf + head, k) != k) otaErr = Update.errorString();
      data -= k;
    }
  }
  while (!otaErr.length() && left) {
    int n = httpd_req_recv(r, buf, min(left, (size_t)4096));
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) { otaErr = "Übertragung abgebrochen"; break; }
    left -= n;
    size_t k = min((size_t)n, data);
    if (!k) continue;
    otaScan((uint8_t*)buf, k);
    if (Update.write((uint8_t*)buf, k) != k) otaErr = Update.errorString();
    data -= k;
  }
  if (!otaErr.length() && !otaTag) otaErr = "Diese Firmware ist für ein anderes Board";
  if (!otaErr.length() && !Update.end(true)) otaErr = Update.errorString();
  if (otaErr.length()) {
    if (Update.isRunning()) Update.abort();
    Serial.printf("Update: abgelehnt (%s)\n", otaErr.c_str());
    msg(otaErr.c_str(), otaErr == "PIN nötig" ? 401 : 400);
    return respond(r);
  }
  Serial.println("Update: installiert, starte neu");
  msg("Update installiert – die Uhr startet neu");
  respond(r);
  delay(600);
  ESP.restart();
  return ESP_OK;
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
  d["phoneUrl"] = phone::url();
  music::State ms = music::state();
  JsonObject mu = d["music"].to<JsonObject>();
  mu["active"] = ms.active;
  mu["playing"] = ms.playing;
  mu["title"] = ms.title;
  mu["artist"] = ms.artist;
  mu["pos"] = (int)ms.pos;
  mu["dur"] = ms.dur;
  mu["msg"] = ms.msg;
  mu["volume"] = cfg.volume;
  mu["sleepMin"] = (music::sleepLeftS() + 59) / 60;
  {
    music::Item li;
    uint32_t ls;
    if (!ms.active && music::last(li, ls)) {
      JsonObject l = mu["last"].to<JsonObject>();
      l["title"] = li.title;
      l["pos"] = ls;
      l["dur"] = li.dur;
    }
  }
  JsonDocument rd;
  deserializeJson(rd, routines::stateJson());
  d["routines"] = rd;
  int gl;
  uint32_t gms;
  gameRecords(gl, gms);
  JsonObject gm = d["game"].to<JsonObject>();
  gm["level"] = gl;
  gm["ms"] = gms;
  JsonObject dp = d["display"].to<JsonObject>();
  dp["brightness"] = cfg.brightness;
  dp["screenOff"] = cfg.screenOff;
  JsonObject pw = d["power"].to<JsonObject>();
  pw["mode"] = (int)pwr::mode();
  pw["saving"] = pwr::saving();
  pw["mv"] = hwBatteryMv();
  pw["remainMin"] = pwr::remainingMin();
  pw["sleep"] = pwr::sleepText();
  pw["deep"] = pwr::deepSleepSetting();
  {
    bool k, a;
    uint16_t f, t;
    pwr::nightGet(k, a, f, t);
    pw["nightKids"] = k;
    pw["nightAdult"] = a;
    pw["nightFrom"] = f;
    pw["nightTo"] = t;
  }
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
  ko["world"] = kc.world;
  ko["pet"] = kc.pet;
  ko["petName"] = kc.petName;
  ko["adventure"] = kc.adventure;
  ko["phases"] = kc.phases;
  JsonDocument dd;
  deserializeJson(dd, kids::dayJson());
  ko["day"] = dd;
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
  c.world = d["world"] | c.world;
  c.pet = d["pet"] | c.pet;
  if (d["petName"].is<const char*>()) snprintf(c.petName, sizeof(c.petName), "%s", (const char*)d["petName"]);
  c.adventure = d["adventure"] | c.adventure;
  c.phases = d["phases"] | c.phases;
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


// ------------------------------------------------------------------ Übergabe HTTPS-Task ↔ Hauptschleife

static const char* statusText(int c) {
  switch (c) {
    case 200: return "200 OK";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 404: return "404 Not Found";
    case 413: return "413 Payload Too Large";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
  }
}

static esp_err_t respond(httpd_req_t* r) {
  httpd_resp_set_status(r, statusText(req.code));
  httpd_resp_set_type(r, req.type.c_str());
  if (req.disp.length()) httpd_resp_set_hdr(r, "Content-Disposition", req.disp.c_str());
  if (req.outP) return httpd_resp_send(r, req.outP, HTTPD_RESP_USE_STRLEN);
  return httpd_resp_send(r, req.out.c_str(), req.out.length());
}

// fn in der Hauptschleife ausführen und auf ihre Antwort warten (die Hauptschleife ruft poll() alle paar ms)
static bool runInLoop(void (*fn)(), uint32_t ms) {
  for (uint32_t t = millis(); req.state && millis() - t < ms;) delay(5);  // die vorige Anfrage läuft noch
  if (req.state) return false;
  xSemaphoreTake(req.ready, 0);
  req.code = 0;
  req.disp = "";
  req.fn = fn;
  req.state = 1;
  return xSemaphoreTake(req.ready, pdMS_TO_TICKS(ms)) == pdTRUE;
}

esp_err_t handle(httpd_req_t* r) {
  const char* q = strchr(r->uri, '?');
  String path = q ? String(r->uri).substring(0, q - r->uri) : String(r->uri);
  bool post = r->method == HTTP_POST;
  const Route* rt = nullptr;
  for (int i = 0; i < nRoutes && !rt; i++)
    if (routes[i].post == post && path == routes[i].path) rt = &routes[i];
  if (!rt) return httpd_resp_send_err(r, HTTPD_404_NOT_FOUND, "Nicht gefunden");
  for (uint32_t t = millis(); req.state && millis() - t < 30000;) delay(5);
  if (req.state) return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Die Uhr ist beschäftigt");
  char pin[24] = "";
  httpd_req_get_hdr_value_str(r, "X-Pin", pin, sizeof(pin));
  req.uri = path;
  req.query = q ? q + 1 : "";
  req.pin = pin;
  req.body = "";
  if (post && path == "/api/update") return otaReceive(r);
  if (r->content_len > 32768) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Zu groß");
  char b[512];
  for (size_t left = r->content_len; left;) {
    int n = httpd_req_recv(r, b, min(left, sizeof(b)));
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) return ESP_FAIL;
    req.body.concat(b, n);
    left -= n;
  }
  if (!runInLoop(rt->fn, 30000)) return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Keine Antwort");
  return respond(r);
}

void begin() {
  req.ready = xSemaphoreCreateBinary();
  // http:// leitet auf https:// um (der HTTPS-Server startet, sobald das WLAN steht)
  web80 = new WebServer(80);
  web80->onNotFound([]() {
    String host = web80->hostHeader().length() ? web80->hostHeader() : netIp();
    web80->sendHeader("Location", "https://" + host + web80->uri());
    web80->send(301, "text/plain", "Bitte https verwenden");
  });
  web80->begin();
  on("/", HTTP_GET, []() { srv->send_P(200, "text/html; charset=utf-8", PAGE); });
  on("/api/state", HTTP_GET, state);
  // Diagnose Stromsparen: Zeit in den Schlafarten, gehaltene Sperren, Laufzeit, Grund des letzten Starts
  on("/api/pm", HTTP_GET, []() {
    if (!authed()) return;
    char* buf = nullptr;
    size_t len = 0;
    FILE* f = open_memstream(&buf, &len);
    fprintf(f, "uptime %llu s, reset %d, wakeup %d, dozing %d\n", esp_timer_get_time() / 1000000ULL,
            (int)esp_reset_reason(), (int)esp_sleep_get_wakeup_cause(), (int)pwr::dozing());
    esp_pm_dump_locks(f);
    fclose(f);
    String out(buf, len);
    free(buf);
    srv->send(200, "text/plain; charset=utf-8", out);
  });
  on("/api/update", HTTP_POST, []() {});  // otaReceive
  on("/api/search", HTTP_GET, search);
  on("/api/display", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    if (d["brightness"].is<int>()) { cfg.brightness = constrain(d["brightness"].as<int>(), 10, 100); hwBrightness(cfg.brightness); }
    if (d["screenOff"].is<int>()) cfg.screenOff = constrain(d["screenOff"].as<int>(), 0, 600);
    settingsSave();
    msg("Gespeichert");
  });
  on("/api/kids", HTTP_POST, setKids);
  on("/api/kids/day", HTTP_POST, []() {
    if (!authed()) return;
    String err;
    if (!kids::setDay(srv->arg("plain"), err)) return msg(err.c_str(), 400);
    msg("Ablauf gespeichert");
  });
  on("/api/kids/release", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d) || !(d["path"] | "")[0]) return msg("Ungültige Anfrage", 400);
    kids::release(d["path"], d["title"] | "", d["sub"] | "", d["on"] | true);
    msg("Gespeichert");
  });
  on("/api/kids/pin", HTTP_POST, []() {
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
  on("/api/assist", HTTP_POST, []() {
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
  // Fernbedienung: Listen holen, abspielen, steuern (Eltern dürfen alles, auch nachts)
  on("/api/music/list", HTTP_GET, []() {
    if (!authed()) return;
    static music::Item* it = nullptr;
    if (!it) it = (music::Item*)heap_caps_calloc(80, sizeof(music::Item), MALLOC_CAP_SPIRAM);
    char title[72] = "", err[64] = "";
    int n = music::query(srv->arg("path").c_str(), title, sizeof(title), it, 80, err, sizeof(err));
    JsonDocument d;
    d["title"] = title;
    if (err[0]) d["err"] = err;
    JsonArray a = d["items"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject o = a.add<JsonObject>();
      o["id"] = it[i].id;
      o["title"] = it[i].title;
      o["sub"] = it[i].sub;
      o["kind"] = it[i].kind;
      o["dur"] = it[i].dur;
    }
    json(200, d);
  });
  on("/api/music/play", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    static music::Item* it = nullptr;
    if (!it) it = (music::Item*)heap_caps_calloc(80, sizeof(music::Item), MALLOC_CAP_SPIRAM);
    int n = 0;
    for (JsonObject o : d["items"].as<JsonArray>()) {
      if (n >= 80) break;
      music::Item& x = it[n++];
      x.kind = o["kind"] | 0;
      x.dur = o["dur"] | 0;
      snprintf(x.id, sizeof(x.id), "%s", (const char*)(o["id"] | ""));
      snprintf(x.title, sizeof(x.title), "%s", (const char*)(o["title"] | ""));
      snprintf(x.sub, sizeof(x.sub), "%s", (const char*)(o["sub"] | ""));
    }
    music::playList(it, n, d["index"] | 0, true);
    msg("Läuft auf der Uhr");
  });
  on("/api/music/ctl", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    const char* c = d["cmd"] | "";
    if (!strcmp(c, "toggle")) music::toggle();
    else if (!strcmp(c, "next")) music::next();
    else if (!strcmp(c, "prev")) music::prev();
    else if (!strcmp(c, "stop")) music::stop();
    if (d["volume"].is<int>()) { ui::volumeSet(d["volume"].as<int>()); settingsSave(); }
    if (d["sleep"].is<int>()) music::sleepSet(d["sleep"].as<int>());
    msg("OK");
  });
  on("/api/routines", HTTP_POST, []() {
    if (!authed()) return;
    routines::setConfig(srv->arg("plain"));
    msg("Routinen gespeichert");
  });
  on("/api/game/reset", HTTP_POST, []() {
    if (!authed()) return;
    gameResetRecords();
    msg("Rekorde zurückgesetzt");
  });
  on("/api/routines/on", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    routines::setEnabled(d["on"] | true);
    msg(routines::enabled() ? "Aufgaben an" : "Aufgaben aus");
  });
  on("/api/music/forget", HTTP_POST, []() {
    if (!authed()) return;
    music::forgetLast();
    msg("Vergessen");
  });
  on("/api/music/resume", HTTP_POST, []() {
    if (!authed()) return;
    music::resumeLast();
    msg("Läuft auf der Uhr");
  });
  on("/api/routines/reset", HTTP_POST, []() {
    if (!authed()) return;
    routines::resetStars();
    msg("Sterne zurückgesetzt");
  });
  on("/api/backup", HTTP_GET, []() {
    if (!authed()) return;
    srv->sendHeader("Content-Disposition", "attachment; filename=taschenfunk-sicherung.json");
    srv->send(200, "application/json", backup::make(srv->arg("secrets") == "1"));
  });
  on("/api/restore", HTTP_POST, []() {
    if (!authed()) return;
    String err;
    if (!backup::restore(srv->arg("plain"), err)) return msg(err.c_str(), 400);
    msg("Übernommen – die Uhr startet neu");
    delay(600);
    ESP.restart();
  });
  on("/api/find", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    body(d);
    ui::findMe(d["on"] | true, d["volume"] | 70);
    msg(d["on"] | true ? "Die Uhr klingelt 30 Sekunden" : "Beendet");
  });
  on("/api/power", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    if (d["mode"].is<int>()) pwr::setMode((pwr::Mode)d["mode"].as<int>());
    if (d["deep"].is<int>()) pwr::setDeepSleep(constrain(d["deep"].as<int>(), 0, 240));
    if (d["nightKids"].is<bool>() || d["nightAdult"].is<bool>() || d["nightFrom"].is<int>() || d["nightTo"].is<int>()) {
      bool k, a;
      uint16_t f, t;
      pwr::nightGet(k, a, f, t);
      pwr::nightSet(d["nightKids"] | k, d["nightAdult"] | a, d["nightFrom"] | f, d["nightTo"] | t);
    }
    msg("Gespeichert");
  });
  on("/api/models", HTTP_GET, []() {
    if (!authed()) return;
    String err, m = assist::models(err);
    if (!m.length()) { JsonDocument d; d["err"] = err; return json(200, d); }
    srv->send(200, "application/json", m);
  });
  on("/api/n8n/list", HTTP_GET, []() {
    if (!authed()) return;
    String err, list = n8n::listAll(err);
    if (!list.length()) { JsonDocument d; d["err"] = err; return json(200, d); }
    srv->send(200, "application/json", "{\"items\":" + list + "}");
  });
  on("/api/n8n/allow", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    String s;
    serializeJson(d["items"], s);
    n8n::setAllowed(s);
    msg("Freigabe gespeichert");
  });
  on("/api/name", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    const char* n = body(d) ? (d["name"] | "") : "";
    if (!n[0]) return msg("Name fehlt", 400);
    snprintf(cfg.name, sizeof(cfg.name), "%s", n);
    settingsSave();
    radio::nameChanged();
    msg("Name gespeichert");
  });
  on("/api/wifi", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d) || !(d["ssid"] | "")[0]) return msg("SSID fehlt", 400);
    knownAdd(d["ssid"], d["pass"] | "");
    settingsSave();
    msg("Netz gespeichert");
    netConnect(d["ssid"], d["pass"] | "");
  });
  on("/api/wifi/forget", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (body(d)) { knownForget(d["ssid"] | ""); settingsSave(); }
    msg("Netz vergessen");
  });
  on("/api/keys", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    const char* g = d["gemini"] | "";
    if (g[0]) keys::set(g);
    msg("Schlüssel gespeichert");
  });
  on("/api/fav/remove", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (body(d) && favs::has(d["path"] | "")) favs::toggle(d["path"] | "", "", "");
    msg("Favorit entfernt");
  });
  on("/api/restart", HTTP_POST, []() {
    if (!authed()) return;
    msg("Startet neu …");
    delay(300);
    ESP.restart();
  });
}

void poll() {
  if (web80) web80->handleClient();
  if (req.state != 1) return;
  req.state = 2;
  req.fn();
  if (!req.code) msg("Keine Antwort", 500);
  req.state = 0;
}

}  // namespace webui
