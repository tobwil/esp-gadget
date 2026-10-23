#include "backup.h"
#include <ArduinoJson.h>
#include "assist.h"
#include "config.h"
#include "favs.h"
#include "keys.h"
#include "kids.h"
#include "n8n.h"
#include "power.h"
#include "routines.h"
#include "settings.h"

namespace backup {

static void items(JsonArray a, int (*list)(music::Item*, int)) {
  music::Item* it = (music::Item*)heap_caps_malloc(sizeof(music::Item) * favs::MAX, MALLOC_CAP_SPIRAM);
  if (!it) return;
  int n = list(it, favs::MAX);
  for (int i = 0; i < n; i++) {
    JsonObject o = a.add<JsonObject>();
    o["path"] = it[i].id;
    o["title"] = it[i].title;
    o["sub"] = it[i].sub;
  }
  heap_caps_free(it);
}

static int favList(music::Item* o, int m) { return favs::list(o, m); }
static int kidList(music::Item* o, int m) { return kids::list(o, m); }

String make(bool secrets) {
  JsonDocument d;
  d["taschenfunk"] = FW_VERSION;
  d["secrets"] = secrets;
  JsonObject g = d["device"].to<JsonObject>();
  g["name"] = cfg.name;
  g["volume"] = cfg.volume;
  g["brightness"] = cfg.brightness;
  g["screenOff"] = cfg.screenOff;
  g["radioRx"] = cfg.radioRx;
  g["face"] = cfg.face;
  g["timerS"] = cfg.timerS;
  g["alarmOn"] = cfg.alarmOn;
  g["alarmH"] = cfg.alarmH;
  g["alarmM"] = cfg.alarmM;
  g["alarmSrc"] = cfg.alarmSrc;
  g["alarmName"] = cfg.alarmName;
  g["power"] = (int)pwr::mode();
  g["deepSleep"] = pwr::deepSleepSetting();
  {
    bool k, a;
    uint16_t f, t;
    pwr::nightGet(k, a, f, t);
    JsonObject n = g["night"].to<JsonObject>();
    n["kids"] = k;
    n["adult"] = a;
    n["from"] = f;
    n["to"] = t;
  }
  JsonArray w = d["wifi"].to<JsonArray>();
  for (int i = 0; i < knownCount; i++) {
    JsonObject o = w.add<JsonObject>();
    o["ssid"] = known[i].ssid;
    if (secrets) o["pass"] = known[i].pass;
  }
  const kids::Cfg& k = kids::cfg();
  JsonObject ko = d["kids"].to<JsonObject>();
  ko["on"] = k.on;
  ko["wake"] = k.wake;
  ko["bed"] = k.bed;
  {
    JsonDocument dd;
    deserializeJson(dd, kids::dayJson());
    if (dd["custom"]) ko["day"] = dd;  // nur ein eigener Ablauf
  }
  ko["maxVol"] = k.maxVol;
  ko["apps"] = k.apps;
  ko["school"] = k.school;
  ko["nap"] = k.nap;
  ko["angry"] = k.angry;
  ko["nightLock"] = k.nightLock;
  ko["storyMin"] = k.storyMin;
  if (secrets) ko["pin"] = k.pin;
  items(ko["released"].to<JsonArray>(), kidList);
  JsonDocument rt;
  deserializeJson(rt, routines::configJson());
  d["routines"] = rt;
  items(d["favs"].to<JsonArray>(), favList);
  JsonObject a = d["assist"].to<JsonObject>();
  a["name"] = assist::name();
  a["search"] = assist::search();
  a["model"] = assist::model();
  a["voice"] = assist::voice();
  a["stt"] = assist::sttModel();
  a["n8nUrl"] = n8n::url();
  JsonDocument al;
  deserializeJson(al, n8n::allowedJson());
  a["n8nAllowed"] = al;
  if (secrets) {
    a["n8nToken"] = n8n::token();
    d["gemini"] = keys::gemini();
  }
  String out;
  serializeJsonPretty(d, out);
  return out;
}

bool restore(const String& json, String& err) {
  JsonDocument d;
  if (deserializeJson(d, json) || !d["taschenfunk"]) { err = "Keine Taschenfunk-Sicherung"; return false; }
  JsonObject g = d["device"];
  if (g) {
    snprintf(cfg.name, sizeof(cfg.name), "%s", (const char*)(g["name"] | cfg.name));
    cfg.volume = g["volume"] | cfg.volume;
    cfg.brightness = g["brightness"] | cfg.brightness;
    cfg.screenOff = g["screenOff"] | cfg.screenOff;
    cfg.radioRx = g["radioRx"] | cfg.radioRx;
    cfg.face = g["face"] | cfg.face;
    cfg.timerS = g["timerS"] | cfg.timerS;
    cfg.alarmOn = g["alarmOn"] | cfg.alarmOn;
    cfg.alarmH = g["alarmH"] | cfg.alarmH;
    cfg.alarmM = g["alarmM"] | cfg.alarmM;
    snprintf(cfg.alarmSrc, sizeof(cfg.alarmSrc), "%s", (const char*)(g["alarmSrc"] | cfg.alarmSrc));
    snprintf(cfg.alarmName, sizeof(cfg.alarmName), "%s", (const char*)(g["alarmName"] | cfg.alarmName));
    if (g["power"].is<int>()) pwr::setMode((pwr::Mode)g["power"].as<int>());
    if (g["deepSleep"].is<int>()) pwr::setDeepSleep(g["deepSleep"].as<int>());
    if (g["night"]) {
      JsonObject n = g["night"];
      pwr::nightSet(n["kids"] | true, n["adult"] | false, n["from"] | 23 * 60, n["to"] | 7 * 60);
    }
  }
  for (JsonObject o : d["wifi"].as<JsonArray>()) {  // nur Netze mit Passwort (oder offene) übernehmen
    const char* ss = o["ssid"] | "";
    if (ss[0] && (o["pass"].is<const char*>() || knownIndex(ss) < 0)) knownAdd(ss, o["pass"] | "");
  }
  settingsSave();
  JsonObject ko = d["kids"];
  if (ko) {
    kids::Cfg k = kids::cfg();
    k.on = ko["on"] | k.on;
    k.wake = ko["wake"] | k.wake;
    k.bed = ko["bed"] | k.bed;
    k.maxVol = ko["maxVol"] | k.maxVol;
    k.apps = ko["apps"] | k.apps;
    k.school = ko["school"] | k.school;
    k.nap = ko["nap"] | k.nap;
    k.angry = ko["angry"] | k.angry;
    k.nightLock = ko["nightLock"] | k.nightLock;
    k.storyMin = ko["storyMin"] | k.storyMin;
    if (ko["pin"].is<const char*>()) snprintf(k.pin, sizeof(k.pin), "%s", (const char*)ko["pin"]);
    kids::set(k);
    if (ko["day"]) {
      String day, err;
      serializeJson(ko["day"], day);
      kids::setDay(day, err);
    }
    for (JsonObject r : ko["released"].as<JsonArray>()) kids::release(r["path"] | "", r["title"] | "", r["sub"] | "", true);
  }
  if (d["routines"]) {
    String r;
    serializeJson(d["routines"], r);
    routines::setConfig(r);
  }
  for (JsonObject f : d["favs"].as<JsonArray>()) {
    const char* p = f["path"] | "";
    if (p[0] && !favs::has(p)) favs::toggle(p, f["title"] | "", f["sub"] | "");
  }
  JsonObject a = d["assist"];
  if (a) {
    assist::configure(a["name"] | (const char*)nullptr, a["search"].is<bool>() ? (int)a["search"].as<bool>() : -1,
                      a["model"] | (const char*)nullptr, a["voice"] | (const char*)nullptr, a["stt"] | (const char*)nullptr);
    n8n::set(a["n8nUrl"] | (const char*)nullptr, a["n8nToken"] | (const char*)nullptr);
    if (a["n8nAllowed"]) {
      String s;
      serializeJson(a["n8nAllowed"], s);
      n8n::setAllowed(s);
    }
  }
  if (d["gemini"].is<const char*>() && ((const char*)d["gemini"])[0]) keys::set(d["gemini"]);
  return true;
}

}  // namespace backup
