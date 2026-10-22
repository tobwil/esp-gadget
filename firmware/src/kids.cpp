#include "kids.h"
#include <Preferences.h>
#include <time.h>
#include "favs.h"

namespace kids {

static Cfg c;
static favs::Store store("kids");

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
  p.end();
  store.begin();
}

const Cfg& cfg() { return c; }
bool on() { return c.on; }
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
  p.end();
}

bool released(const char* path) { return store.has(path); }
void release(const char* path, const char* title, const char* sub, bool on) { store.set(path, title, sub, on); }
int list(music::Item* out, int max) { return store.list(out, max); }

// Der Tag hängt an Aufstehen und Bettzeit: morgens ab dem Aufstehen, abends rückwärts von der Bettzeit.
int schedule(Slot* out, bool weekend) {
  int w = c.wake, b = c.bed, n = 0;
  auto add = [&](Act a, int start) {
    if (n && start <= out[n - 1].start) return;  // zu kurzer Tag: Abschnitt fällt weg
    if (start >= b) return;
    out[n++] = {a, (uint16_t)start};
  };
  add(WAKE, w);
  add(BREAKFAST, w + 30);
  add(DAYCARE, w + 60);  // am Wochenende: spielen
  add(LUNCH, 12 * 60);
  if (c.nap) add(NAP, 12 * 60 + 45);
  add(PLAY, c.nap ? 14 * 60 : 13 * 60);
  add(DINNER, b - 150);
  add(BATH, b - 90);
  add(STORY, b - 45);
  out[n++] = {SLEEP, (uint16_t)b};
  (void)weekend;
  return n;
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

}  // namespace kids
