#include "routines.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "kids.h"

namespace routines {

static const int MAX = 8;
static String items[3][MAX];
static int n[3] = {0, 0, 0};
static uint8_t doneBits[3] = {0, 0, 0};  // heute erledigt (Bit je Aufgabe)
static int day = -1;                      // Tag (yday), für den doneBits gilt
static int today = 0, total = 0;
static uint16_t week[7] = {0};            // Sterne der letzten 7 Tage (Index = Wochentag)

static void defaults() {
  const char* m[] = {"Anziehen", "Zähne putzen", "Frühstücken", "Schuhe an"};
  const char* e[] = {"Spielzeug aufräumen", "Schlafanzug an", "Zähne putzen", "Ab ins Bett"};
  n[MORNING] = 4;
  n[EVENING] = 4;
  for (int i = 0; i < 4; i++) { items[MORNING][i] = m[i]; items[EVENING][i] = e[i]; }
}

static void load() {
  Preferences p;
  p.begin("rout", true);
  String c = p.getString("cfg", "");
  day = p.getShort("day", -1);
  doneBits[MORNING] = p.getUChar("dm", 0);
  doneBits[EVENING] = p.getUChar("de", 0);
  today = p.getUShort("today", 0);
  total = p.getUInt("total", 0);
  p.getBytes("week", week, sizeof(week));
  p.end();
  if (!c.length()) { defaults(); return; }
  JsonDocument d;
  if (deserializeJson(d, c)) { defaults(); return; }
  const char* key[3] = {"", "morning", "evening"};
  for (int w = 1; w <= 2; w++) {
    n[w] = 0;
    for (const char* t : d[key[w]].as<JsonArray>())
      if (n[w] < MAX && t && t[0]) items[w][n[w]++] = t;
  }
}

static void save() {
  Preferences p;
  p.begin("rout", false);
  p.putShort("day", day);
  p.putUChar("dm", doneBits[MORNING]);
  p.putUChar("de", doneBits[EVENING]);
  p.putUShort("today", today);
  p.putUInt("total", total);
  p.putBytes("week", week, sizeof(week));
  p.end();
}

// neuer Tag: Haken zurücksetzen, Wochen-Speicher für den Wochentag leeren
static void rollDay() {
  if (time(nullptr) < 1700000000) return;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_yday == day) return;
  day = t.tm_yday;
  doneBits[MORNING] = doneBits[EVENING] = 0;
  today = 0;
  week[t.tm_wday] = 0;
  save();
}

void begin() { load(); }

String configJson() {
  JsonDocument d;
  const char* key[3] = {"", "morning", "evening"};
  for (int w = 1; w <= 2; w++) {
    JsonArray a = d[key[w]].to<JsonArray>();
    for (int i = 0; i < n[w]; i++) a.add(items[w][i]);
  }
  String s;
  serializeJson(d, s);
  return s;
}

void setConfig(const String& json) {
  Preferences p;
  p.begin("rout", false);
  p.putString("cfg", json);
  p.end();
  load();
  doneBits[MORNING] = doneBits[EVENING] = 0;
  save();
}

static int nowMin() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return t.tm_hour * 60 + t.tm_min;
}

Which active() {
  if (!kids::on() || time(nullptr) < 1700000000) return NONE;
  rollDay();
  int m = nowMin(), w = kids::cfg().wake, b = kids::cfg().bed;
  if (n[MORNING] && m >= w && m < w + 120) return MORNING;
  if (n[EVENING] && m >= b - 120 && m < b) return EVENING;
  return NONE;
}

Which next(int* startMin) {
  int m = nowMin(), w = kids::cfg().wake, b = kids::cfg().bed - 120;
  if (m < w) { *startMin = w; return MORNING; }
  if (m < b) { *startMin = b; return EVENING; }
  *startMin = w;
  return MORNING;
}

const char* name(Which w) { return w == MORNING ? "Morgens" : w == EVENING ? "Abends" : ""; }
int count(Which w) { return w ? n[w] : 0; }
String item(Which w, int i) { return w && i < n[w] ? items[w][i] : ""; }
bool done(Which w, int i) { return w && (doneBits[w] >> i & 1); }
bool complete(Which w) { return w && n[w] && doneBits[w] == (1 << n[w]) - 1; }

int check(Which w, int i, bool on) {
  if (!w || i >= n[w] || done(w, i) == on) return 0;
  rollDay();
  bool was = complete(w);
  int stars = 0;
  if (on) { doneBits[w] |= 1 << i; stars = 1; }
  else { doneBits[w] &= ~(1 << i); stars = -1; }
  if (!was && complete(w)) stars += 2;          // alles geschafft: zwei Extrasterne
  else if (was && !complete(w)) stars -= 2;
  today = max(0, today + stars);
  total = max(0, total + stars);
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  week[t.tm_wday] = max(0, (int)week[t.tm_wday] + stars);
  save();
  return stars;
}

int starsToday() { rollDay(); return today; }
int starsWeek() { int s = 0; for (int i = 0; i < 7; i++) s += week[i]; return s; }
int starsTotal() { return total; }

void resetStars() {
  today = total = 0;
  memset(week, 0, sizeof(week));
  save();
}

String stateJson() {
  rollDay();
  JsonDocument d;
  d["active"] = (int)active();
  d["today"] = today;
  d["week"] = starsWeek();
  d["total"] = total;
  const char* key[3] = {"", "morning", "evening"};
  for (int w = 1; w <= 2; w++) {
    JsonArray a = d[key[w]].to<JsonArray>();
    for (int i = 0; i < n[w]; i++) {
      JsonObject o = a.add<JsonObject>();
      o["text"] = items[w][i];
      o["done"] = done((Which)w, i);
    }
  }
  String s;
  serializeJson(d, s);
  return s;
}

}  // namespace routines
