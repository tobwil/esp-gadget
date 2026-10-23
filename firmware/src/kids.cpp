#include "kids.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <algorithm>
#include <time.h>
#include "favs.h"
#include "hw.h"
#include "settings.h"

namespace kids {

static Cfg c;
static favs::Store store("kids");

// eigener Tagesablauf: [0] = Mo–Fr, [1] = Sa/So; 0 Einträge = Standard
struct Entry { uint16_t t; uint8_t a; char n[24]; };
static const int PLAN_MAX = 14;
static Entry plan[2][PLAN_MAX];
static int planN[2] = {0, 0};
static void loadDay(const String& json);

void begin() {
  Preferences p;
  p.begin("kids", true);
  c.on = p.getBool("on", false);
  c.wake = p.getUShort("wake", 6 * 60 + 30);
  c.bed = p.getUShort("bed", 20 * 60 + 30);
  c.maxVol = p.getUChar("maxvol", 60);
  c.apps = p.getUChar("apps", A_RADIO | A_TIMER | A_GAME);
  c.school = p.getBool("school", false);
  c.nap = p.getBool("nap", true);
  p.getString("pin", c.pin, sizeof(c.pin));
  c.angry = p.getBool("angry", false);
  c.nightLock = p.getBool("night", true);
  c.storyMin = p.getUChar("story", 30);
  c.world = p.getUChar("world", 5);
  c.pet = p.getUChar("pet", 1);
  if (!p.getString("petname", c.petName, sizeof(c.petName))) snprintf(c.petName, sizeof(c.petName), "Mimi");
  c.adventure = p.getBool("adv", true);
  c.phases = p.getBool("phases", true);
  c.brushHard = p.getBool("brushhard", false);
  c.brushOn = p.getBool("brushon", true);
  String day = p.getString("day", "");
  p.end();
  store.begin();
  loadDay(day);
}

const Cfg& cfg() { return c; }
bool on() { return c.on; }

// Minuten seit der Bettzeit, −1 = nicht Nacht
static int nightMinutes() {
  if (!c.on || !c.nightLock || time(nullptr) < 1700000000) return -1;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int m = t.tm_hour * 60 + t.tm_min;
  bool isNight = c.bed > c.wake ? (m >= c.bed || m < c.wake) : (m >= c.bed && m < c.wake);
  if (!isNight) return -1;
  return (m - c.bed + 1440) % 1440;
}

bool night() { return nightMinutes() >= 0; }
bool musicLocked() { int n = nightMinutes(); return n >= 0 && n >= c.storyMin; }
int storyLeftS() {
  int n = nightMinutes();
  if (n < 0 || n >= c.storyMin) return 0;
  time_t now = time(nullptr);
  return (c.storyMin - n) * 60 - now % 60;
}
bool angry() { return c.on && c.angry; }

static int doneToday = 0, doneDay = -1;
static uint32_t doneLast = 0;

void angryDone() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_yday != doneDay) { doneDay = t.tm_yday; doneToday = 0; }
  doneToday++;
  doneLast = now;
}

int angryToday() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return t.tm_yday == doneDay ? doneToday : 0;
}

uint32_t angryLast() { return doneLast; }
bool allowed(App a) { return !c.on || (c.apps & a); }
int limitVolume(int pct) { return c.on ? min(pct, (int)c.maxVol) : pct; }

void set(const Cfg& n) {
  c = n;
  c.wake = min<uint16_t>(c.wake, 23 * 60);
  c.bed = constrain(c.bed, c.wake + 120, 24 * 60 - 1);
  c.maxVol = constrain(c.maxVol, 10, 100);
  Preferences p;
  p.begin("kids", false);
  p.putBool("on", c.on);
  p.putUShort("wake", c.wake);
  p.putUShort("bed", c.bed);
  p.putUChar("maxvol", c.maxVol);
  p.putUChar("apps", c.apps);
  p.putBool("school", c.school);
  p.putBool("nap", c.nap);
  p.putString("pin", c.pin);
  p.putBool("angry", c.angry);
  p.putBool("night", c.nightLock);
  p.putUChar("story", c.storyMin);
  p.putUChar("world", c.world);
  p.putUChar("pet", c.pet);
  p.putString("petname", c.petName);
  p.putBool("adv", c.adventure);
  p.putBool("phases", c.phases);
  p.putBool("brushhard", c.brushHard);
  p.putBool("brushon", c.brushOn);
  p.end();
}

bool released(const char* path) { return store.has(path); }
void release(const char* path, const char* title, const char* sub, bool on) { store.set(path, title, sub, on); }
int list(music::Item* out, int max) { return store.list(out, max); }

// Der Tag hängt an Aufstehen und Bettzeit: morgens ab dem Aufstehen, abends rückwärts von der Bettzeit.
int schedule(Slot* out, bool weekend) {
  int w = c.wake, b = c.bed, n = 0;
  auto add = [&](Act a, int start, const char* name = nullptr) {
    if (n && start <= out[n - 1].start) return;  // zu kurzer Tag: Abschnitt fällt weg
    if (start >= b || n >= SLOTS_MAX - 1) return;
    out[n++] = {a, (uint16_t)start, name};
  };
  add(WAKE, w);
  int pi = weekend ? 1 : 0;
  if (planN[pi]) {  // eigener Plan
    for (int i = 0; i < planN[pi]; i++) add((Act)plan[pi][i].a, plan[pi][i].t, plan[pi][i].n[0] ? plan[pi][i].n : nullptr);
    out[n++] = {SLEEP, (uint16_t)b, nullptr};
    return n;
  }
  add(BREAKFAST, w + 30);
  add(DAYCARE, w + 60);  // am Wochenende: spielen
  add(LUNCH, 12 * 60);
  if (c.nap) add(NAP, 12 * 60 + 45);
  add(PLAY, c.nap ? 14 * 60 : 13 * 60);
  add(DINNER, b - 150);
  add(BATH, b - 90);
  add(STORY, b - 45);
  out[n++] = {SLEEP, (uint16_t)b, nullptr};
  return n;
}

const char* slotName(const Slot& s, bool weekend) { return s.name ? s.name : actName(s.act, weekend); }

static void loadDay(const String& json) {
  planN[0] = planN[1] = 0;
  JsonDocument d;
  if (!json.length() || deserializeJson(d, json)) return;
  const char* key[2] = {"weekday", "weekend"};
  for (int k = 0; k < 2; k++) {
    for (JsonObject e : d[key[k]].as<JsonArray>()) {
      int t = e["t"] | -1, a = e["a"] | -1;
      if (t < 0 || t >= 24 * 60 || a <= WAKE || a >= ACTS || planN[k] >= PLAN_MAX) continue;
      Entry& x = plan[k][planN[k]++];
      x.t = t;
      x.a = a;
      snprintf(x.n, sizeof(x.n), "%s", (const char*)(e["n"] | ""));
    }
    // nach Uhrzeit sortieren
    std::sort(plan[k], plan[k] + planN[k], [](const Entry& p, const Entry& q) { return p.t < q.t; });
  }
}

String dayJson() {
  JsonDocument d;
  d["custom"] = planN[0] || planN[1];
  const char* key[2] = {"weekday", "weekend"};
  for (int k = 0; k < 2; k++) {
    JsonArray a = d[key[k]].to<JsonArray>();
    Slot sl[SLOTS_MAX];
    int n = schedule(sl, k == 1);
    for (int i = 0; i < n; i++) {  // Aufstehen und Schlafenszeit kommen aus den Zeiten oben
      if (sl[i].act == WAKE || sl[i].act == SLEEP) continue;
      JsonObject e = a.add<JsonObject>();
      e["t"] = sl[i].start;
      e["a"] = (int)sl[i].act;
      e["n"] = sl[i].name ? sl[i].name : "";
    }
  }
  String s;
  serializeJson(d, s);
  return s;
}

bool setDay(const String& json, String& err) {
  JsonDocument d;
  if (json.length() && deserializeJson(d, json)) { err = "Ungültiger Ablauf"; return false; }
  String store = (!json.length() || d["reset"]) ? "" : json;
  loadDay(store);
  if (store.length()) {  // so speichern, wie es verstanden wurde (geprüft und sortiert)
    JsonDocument o;
    const char* key[2] = {"weekday", "weekend"};
    for (int k = 0; k < 2; k++) {
      JsonArray a = o[key[k]].to<JsonArray>();
      for (int i = 0; i < planN[k]; i++) {
        JsonObject e = a.add<JsonObject>();
        e["t"] = plan[k][i].t;
        e["a"] = plan[k][i].a;
        if (plan[k][i].n[0]) e["n"] = plan[k][i].n;
      }
    }
    store = "";
    serializeJson(o, store);
  }
  Preferences p;
  p.begin("kids", false);
  p.putString("day", store);
  p.end();
  return true;
}

const char* actName(Act a, bool weekend) {
  switch (a) {
    case SLEEP: return "Schlafenszeit";
    case WAKE: return "Aufstehen";
    case BREAKFAST: return "Frühstück";
    case DAYCARE: return weekend ? "Spielen" : c.school ? "Schule" : "Kita";
    case LUNCH: return "Mittagessen";
    case NAP: return "Mittagsruhe";
    case PLAY: return "Draußen spielen";
    case DINNER: return "Abendessen";
    case BATH: return "Baden & Zähne putzen";
    case STORY: return "Gute-Nacht-Geschichte";
    default: return "";
  }
}

// Nachts: Musik nach der Einschlafzeit sanft beenden, Display gedimmt
void poll() {
  static bool wasNight = false, wasLocked = false;
  bool n = night(), l = musicLocked();
  if (n && !wasNight && music::state().active) music::sleepSet(max(1, storyLeftS() / 60));  // Einschlafzeit läuft
  if (l && !wasLocked && music::state().active) music::sleepSet(1);                        // Zeit um: ausblenden
  (void)0;  // Helligkeit: power.cpp (Obergrenze nachts und im Sparmodus)
  wasNight = n;
  wasLocked = l;
}

}  // namespace kids
