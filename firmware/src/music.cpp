#include "music.h"
#include <Preferences.h>
#include "power.h"
#include "art.h"
#include "favs.h"
#include "kids.h"
#include "audio.h"
#include "settings.h"
#include "podcast.h"
#include "ard.h"
#include "webradio.h"

namespace music {

static const int BROWSE_MAX = 80;
static SemaphoreHandle_t mtx;
static Item* items;  // PSRAM
static int itemCount = 0;
static char listTitle[72] = "", listErr[64] = "";
static volatile uint32_t seq = 0;
static char want[240] = "";
static volatile bool wanted = false, busy = false;
static Source active = SRC_NONE;
static uint32_t fadeInAt = 0, sleepEnd = 0;  // Einblenden (Wecker), Sleep-Timer
// Abfragen der Weboberfläche (eigener Puffer, damit die Liste auf der Uhr unberührt bleibt)
static char qPath[240];
static Item* qItems;
static int qCount;
static char qTitle[72], qErr[64];
static volatile bool qWanted = false, qDone = false;
// Radiowecker: neueste Folge einer Sendung im Arbeits-Task holen
static char wakePath[40];
static volatile bool wakeWanted = false;

static void ucopy(char* dst, size_t n, const char* src) {
  size_t len = strlen(src);
  if (len < n) { memcpy(dst, src, len + 1); return; }
  size_t cut = n - 1;
  while (cut > 0 && ((uint8_t)src[cut] & 0xC0) == 0x80) cut--;
  memcpy(dst, src, cut);
  dst[cut] = 0;
}

static void addList(Item* it, int* n, const char* id, const char* title, const char* sub) {
  Item& o = it[(*n)++];
  o.kind = K_LIST;
  o.dur = 0;
  ucopy(o.id, sizeof(o.id), id);
  ucopy(o.title, sizeof(o.title), title);
  ucopy(o.sub, sizeof(o.sub), sub);
}

static void addListAt(Item* it, int i, const char* id, const char* title, const char* sub) {
  int n = i;
  addList(it, &n, id, title, sub);
}

// Art einer merkbaren Liste (Untertitel im Favoriten-Menü), "" = nicht merkbar
static const char* favKind(const char* path) {
  if (!strncmp(path, "pc:feed:", 8)) return "Podcast";
  if (!strncmp(path, "ard:set:", 8)) return "Audiothek";
  return "";
}

static char lastTitle[72] = "";

// „Zuletzt gehört“: die letzten Folgen und Sender (neueste zuerst), im PSRAM und in NVS („music“/„recent“).
// Früher stand nur die letzte Folge oben in jeder Liste – jetzt ein eigener Menüpunkt, Einträge lassen sich entfernen.
static const int RECENT_MAX = 10;
struct RecentRec { Item item; bool kid; };
static RecentRec* rec = nullptr;
static int recN = 0;

static void recentSave() {
  Preferences p;
  p.begin("music", false);
  if (recN) p.putBytes("recent", rec, recN * sizeof(RecentRec));
  else p.remove("recent");
  p.end();
}

static void remember(const Item& it) {  // nach vorn; doppelte fallen weg
  if (!rec || it.kind != K_TRACK || !it.id[0]) return;
  bool kid = kids::on();
  xSemaphoreTake(mtx, portMAX_DELAY);
  int at = recN < RECENT_MAX ? recN : RECENT_MAX - 1;
  for (int i = 0; i < recN; i++)
    if (!strcmp(rec[i].item.id, it.id)) { at = i; break; }
  bool same = at == 0 && recN && !strcmp(rec[0].item.id, it.id) && rec[0].kid == kid;
  if (!same) {
    memmove(rec + 1, rec, at * sizeof(RecentRec));
    rec[0] = {it, kid};
    if (at >= recN) recN = std::min(recN + 1, RECENT_MAX);
  }
  xSemaphoreGive(mtx);
  if (!same) recentSave();
}

int recent(Recent* out, int max) {
  int n = 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < recN && n < max; i++) out[n++] = {rec[i].item, rec[i].kid, 0};
  xSemaphoreGive(mtx);
  for (int i = 0; i < n; i++)
    if (strncmp(out[i].item.id, "radio:", 6)) out[i].posSec = podcast::resumeSec(out[i].item.id);
  return n;
}

bool recentRemove(const char* id) {
  bool found = false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < recN; i++)
    if (!strcmp(rec[i].item.id, id)) {
      memmove(rec + i, rec + i + 1, (recN - i - 1) * sizeof(RecentRec));
      recN--;
      found = true;
      break;
    }
  xSemaphoreGive(mtx);
  if (!found) return false;
  if (strncmp(id, "radio:", 6)) podcast::forgetPos(id);
  recentSave();
  return true;
}

void recentClear() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  int n = recN;
  recN = 0;
  xSemaphoreGive(mtx);
  for (int i = 0; i < n; i++)
    if (strncmp(rec[i].item.id, "radio:", 6)) podcast::forgetPos(rec[i].item.id);
  recentSave();
}

// Liste „recent“ für die Uhr (im Kindermodus nur die eigenen); die Stelle hängt markResume an
static int recentItems(Item* out, int max, bool kidOnly) {
  int n = 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < recN && n < max; i++) {
    if (kidOnly && !rec[i].kid) continue;
    out[n] = rec[i].item;
    if (!out[n].sub[0] && !strncmp(out[n].id, "radio:", 6)) ucopy(out[n].sub, sizeof(out[n].sub), "Radio");
    n++;
  }
  xSemaphoreGive(mtx);
  return n;
}

static void addRecentFolder(Item* it, int* n, bool kidOnly) {
  if (*n >= BROWSE_MAX) return;
  int c = 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < recN; i++) c += !kidOnly || rec[i].kid;
  xSemaphoreGive(mtx);
  if (!c) return;
  char sub[56];
  snprintf(sub, sizeof(sub), "%d %s · lange drücken = entfernen", c, c == 1 ? "Eintrag" : "Einträge");
  addList(it, n, "recent", "Zuletzt gehört", sub);
}

// angefangene Folgen: „· bei 12:34“ an den Untertitel
static void markResume(Item* it, int n) {
  for (int i = 0; i < n; i++) {
    if (it[i].kind != K_TRACK || !strncmp(it[i].id, "radio:", 6)) continue;
    uint32_t sec = podcast::resumeSec(it[i].id);
    if (sec < 30) continue;
    char sub[56];
    snprintf(sub, sizeof(sub), "%s%sbei %u:%02u", it[i].sub, it[i].sub[0] ? " · " : "", (unsigned)(sec / 60), (unsigned)(sec % 60));
    ucopy(it[i].sub, sizeof(it[i].sub), sub);
  }
}

// Startmenü der Musik-App
static void home(Item* it, int* n, char* title, size_t titleLen) {
  ucopy(title, titleLen, "Musik");
  addList(it, n, "rl:home", "Radio", "Sender aus aller Welt");
  addList(it, n, "ard:home", "ARD Audiothek", "Hörspiele, Wissen, Kinder …");
  addList(it, n, "pc:subs", "Podcasts", "Meine Podcasts und Suche");
  addList(it, n, "favs", "Favoriten", "Gemerkte Sender, Sendungen, Podcasts");
  addRecentFolder(it, n, false);
}

// Kindermodus: nur die freigegebenen Listen
static void kidsHome(Item* it, int* n, char* title, size_t titleLen, char* err, size_t errLen) {
  ucopy(title, titleLen, "Musik");
  *n = kids::list(it, BROWSE_MAX);
  markResume(it, *n);
  addRecentFolder(it, n, true);
  if (!*n) ucopy(err, errLen, "Noch nichts freigegeben");
}

// Liste holen (läuft im Arbeits-Task)
static int fetch(const char* path, Item* tmp, char* title, size_t titleLen, char* err, size_t errLen) {
  int n = 0;
  if (!strncmp(path, "rl", 2)) wradio::browse(path, title, titleLen, tmp, BROWSE_MAX, &n, err, errLen);
  else if (!strncmp(path, "ard", 3)) ard::browse(path, title, titleLen, tmp, BROWSE_MAX, &n, err, errLen);
  else if (!strncmp(path, "pc", 2)) podcast::browse(path, title, titleLen, tmp, BROWSE_MAX, &n, err, errLen);
  else if (!strcmp(path, "favs")) { ucopy(title, titleLen, "Favoriten"); n = favs::list(tmp, BROWSE_MAX); }
  else if (!strcmp(path, "kids")) { ucopy(title, titleLen, "Freigegeben"); n = kids::list(tmp, BROWSE_MAX); }
  else ucopy(err, errLen, "Unbekannte Liste");
  return n;
}

int query(const char* path, char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen) {
  while (qWanted) delay(10);
  ucopy(qPath, sizeof(qPath), path);
  qDone = false;
  qWanted = true;
  uint32_t t0 = millis();
  while (!qDone && millis() - t0 < 20000) delay(20);
  if (!qDone) { ucopy(err, errLen, "Keine Antwort"); return 0; }
  ucopy(title, titleLen, qTitle);
  ucopy(err, errLen, qErr);
  int n = min(max, qCount);
  memcpy(out, qItems, n * sizeof(Item));
  return n;
}

void browse(const char* path) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  ucopy(want, sizeof(want), path);
  wanted = true;
  busy = true;
  xSemaphoreGive(mtx);
}

bool browseBusy() { return busy; }
uint32_t browseSeq() { return seq; }

int browseCopy(char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  ucopy(title, titleLen, listTitle);
  ucopy(err, errLen, listErr);
  int n = min(max, itemCount);
  memcpy(out, items, n * sizeof(Item));
  xSemaphoreGive(mtx);
  return n;
}

static void worker(void*) {
  Item* tmp = (Item*)heap_caps_calloc(BROWSE_MAX, sizeof(Item), MALLOC_CAP_SPIRAM);
  for (;;) {
    if (wakeWanted) {
      wakeWanted = false;
      char t[72], e[64];
      int n = fetch(wakePath, tmp, t, sizeof(t), e, sizeof(e));
      for (int i = 0; i < n; i++)
        if (tmp[i].kind == K_TRACK) { playList(tmp, n, i); break; }
    }
    if (qWanted) {
      qTitle[0] = qErr[0] = 0;
      qCount = fetch(qPath, qItems, qTitle, sizeof(qTitle), qErr, sizeof(qErr));
      markResume(qItems, qCount);
      qWanted = false;
      qDone = true;
    }
    if (!wanted) { vTaskDelay(pdMS_TO_TICKS(pwr::dozing() ? 1000 : 30)); continue; }  // dösend seltener nachsehen
    char path[240], title[72] = "", err[64] = "";
    xSemaphoreTake(mtx, portMAX_DELAY);
    ucopy(path, sizeof(path), want);
    wanted = false;
    xSemaphoreGive(mtx);
    int n = 0;
    bool kid = kids::on();
    // „fav:<Liste>“: Favorit umschalten, danach die Liste selbst wieder zeigen
    if (!kid && !strncmp(path, "fav:", 4)) {
      favs::toggle(path + 4, lastTitle, favKind(path + 4));
      memmove(path, path + 4, strlen(path + 4) + 1);
    }
    if (kid && kids::musicLocked()) {
      char e[64];
      snprintf(e, sizeof(e), "Schlafenszeit – Musik gibt es morgen ab %d:%02d", kids::cfg().wake / 60, kids::cfg().wake % 60);
      ucopy(title, sizeof(title), "Gute Nacht");
      ucopy(err, sizeof(err), e);
    } else if (kid) {
      if (!strcmp(path, "home")) kidsHome(tmp, &n, title, sizeof(title), err, sizeof(err));
      else if (!strcmp(path, "recent")) {
        ucopy(title, sizeof(title), "Zuletzt gehört");
        n = recentItems(tmp, BROWSE_MAX, true);
        if (!n) ucopy(err, sizeof(err), "Noch nichts gehört");
      }
      else if (kids::released(path)) n = fetch(path, tmp, title, sizeof(title), err, sizeof(err));
      else ucopy(err, sizeof(err), "Nicht freigegeben");
    } else if (!strcmp(path, "home")) home(tmp, &n, title, sizeof(title));
    else if (!strcmp(path, "recent")) {
      ucopy(title, sizeof(title), "Zuletzt gehört");
      n = recentItems(tmp, BROWSE_MAX, false);
      if (!n) ucopy(err, sizeof(err), "Noch nichts gehört");
    }
    else if (!strcmp(path, "favs")) {
      ucopy(title, sizeof(title), "Favoriten");
      n = favs::list(tmp, BROWSE_MAX);
      if (!n) ucopy(err, sizeof(err), "Noch keine – Sender lange drücken, Listen oben merken");
    } else n = fetch(path, tmp, title, sizeof(title), err, sizeof(err));
    if (strcmp(path, "home")) markResume(tmp, n);
    // merkbare Listen: oben ein Eintrag zum Merken / Entfernen
    if (!kid && favKind(path)[0] && n > 0 && n < BROWSE_MAX) {
      memmove(tmp + 1, tmp, n * sizeof(Item));
      n++;
      char id[240];
      snprintf(id, sizeof(id), "fav:%s", path);
      bool f = favs::has(path);
      addListAt(tmp, 0, id, f ? "Gemerkt" : "Als Favorit merken", f ? "Antippen = entfernen" : "Erscheint unter „Favoriten“");
      ucopy(lastTitle, sizeof(lastTitle), title);
    }
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (!wanted) {  // inzwischen schon wieder etwas anderes gewünscht? dann verwerfen
      memcpy(items, tmp, n * sizeof(Item));
      itemCount = n;
      ucopy(listTitle, sizeof(listTitle), title);
      ucopy(listErr, sizeof(listErr), err);
      busy = false;
      seq = seq + 1;
    }
    xSemaphoreGive(mtx);
  }
}

// ------------------------------------------------------------------ Wiedergabe

// Radio: die Senderliste, aus der gespielt wurde – Weiter/Zurück wechseln den Sender
static std::vector<Item> stations;
static int stationIdx = 0;
static String favPath, favTitle, favSub;  // merkbar: was gerade läuft

static void playStation(int i) {
  if (stations.empty()) return;
  stationIdx = (i % (int)stations.size() + stations.size()) % stations.size();
  const Item& s = stations[stationIdx];
  const char* url = s.id + 6;  // „radio:“
  favPath = s.id;
  favTitle = s.title;
  favSub = "Sender";
  podcast::playUrl(url, s.title, s.title, wradio::logo(url).c_str(), 0, true);
}

void playList(const Item* list, int n, int start, bool force) {
  if (start < 0 || start >= n || list[start].kind != K_TRACK) return;
  if (kids::musicLocked() && !force) return;  // Schlafenszeit
  const char* id = list[start].id;
  remember(list[start]);
  if (!strncmp(id, "radio:", 6)) {
    stations.clear();
    int index = 0;
    for (int i = 0; i < n; i++) {
      if (list[i].kind != K_TRACK || strncmp(list[i].id, "radio:", 6)) continue;
      if (i == start) index = stations.size();
      stations.push_back(list[i]);
    }
    playStation(index);
    active = SRC_RADIO;
  } else if (!strncmp(id, "ard:a:", 6)) {
    String show, image, set;
    ard::meta(id + 6, show, image, set);
    favPath = set;
    favTitle = show;
    favSub = "Audiothek";
    podcast::playUrl(id + 6, list[start].title, show.c_str(), image.c_str(), list[start].dur, false);
    active = SRC_PODCAST;
  } else {
    podcast::play(list[start]);
    active = SRC_PODCAST;
    String feed;
    podcast::currentFeed(feed, favTitle);
    favPath = feed.length() && feed.length() + 9 < sizeof(Item::id) ? "pc:feed:" + feed : String();
    favSub = "Podcast";
  }
}

bool favTarget(String& path, String& title, String& sub) {
  if (active == SRC_NONE || !favPath.length()) return false;
  path = favPath;
  title = favTitle.length() ? favTitle : favPath;
  sub = favSub;
  return true;
}

void toggle() { if (active != SRC_NONE) podcast::toggle(); }
void next() {  // Podcast: +30 s, Radio: nächster Sender
  if (active == SRC_RADIO) playStation(stationIdx + 1);
  else if (active == SRC_PODCAST) podcast::skip(30);
}
void prev() {
  if (active == SRC_RADIO) playStation(stationIdx - 1);
  else if (active == SRC_PODCAST) podcast::skip(-15);
}
void stop() {
  if (active != SRC_NONE) podcast::stop();
  active = SRC_NONE;
}

State state() {
  if (active != SRC_NONE) return podcast::state();
  State e{};
  return e;
}

const uint16_t* art(uint32_t* version) { return art::image(version); }
void volumeChanged(int) { fadeInAt = 0; }  // von Hand lauter/leiser: Einblenden beenden
// Lautstärke-Verläufe: Einblenden beim Wecken, Ausblenden beim Sleep-Timer
static int lastFadeVol = -1;

static void fadeVolume(int pct) {
  if (pct == lastFadeVol) return;
  lastFadeVol = pct;
  audio::setVolume(pct);
}

void wake(const char* src, const char* name) {
  if (!strncmp(src, "radio:", 6)) {
    Item it{};
    it.kind = K_TRACK;
    ucopy(it.id, sizeof(it.id), src);
    ucopy(it.title, sizeof(it.title), name);
    playList(&it, 1, 0);
  } else {
    ucopy(wakePath, sizeof(wakePath), src);
    wakeWanted = true;
  }
  fadeInAt = millis();
  lastFadeVol = -1;
  fadeVolume(max(3, cfg.volume / 6));
}

void sleepSet(int minutes) {
  sleepEnd = minutes > 0 ? millis() + minutes * 60000UL : 0;
  if (!sleepEnd) { lastFadeVol = -1; audio::setVolume(cfg.volume); }
}

int sleepLeftS() { return sleepEnd ? max(0L, (long)(sleepEnd - millis()) / 1000) : 0; }

void poll() {
  uint32_t now = millis();
  if (fadeInAt) {  // 45 s von leise auf die eingestellte Lautstärke
    float k = (now - fadeInAt) / 45000.0f;
    if (k >= 1) { fadeInAt = 0; lastFadeVol = -1; audio::setVolume(cfg.volume); }
    else fadeVolume(max(3, (int)(cfg.volume * (0.16f + 0.84f * k))));
  }
  if (sleepEnd) {
    if (!state().active) sleepSet(0);  // nichts läuft mehr: Timer aus
    else if ((int32_t)(sleepEnd - now) <= 0) {
      sleepEnd = 0;
      stop();
      lastFadeVol = -1;
      audio::setVolume(cfg.volume);
    } else if (sleepEnd - now < 30000) {
      fadeVolume((int)(cfg.volume * (sleepEnd - now) / 30000.0f));
    }
  }
}

void begin() {
  mtx = xSemaphoreCreateMutex();
  items = (Item*)heap_caps_calloc(BROWSE_MAX, sizeof(Item), MALLOC_CAP_SPIRAM);
  qItems = (Item*)heap_caps_calloc(BROWSE_MAX, sizeof(Item), MALLOC_CAP_SPIRAM);
  art::begin();
  favs::begin();
  podcast::begin();
  rec = (RecentRec*)heap_caps_calloc(RECENT_MAX, sizeof(RecentRec), MALLOC_CAP_SPIRAM);
  Preferences p;
  p.begin("music", false);
  size_t len = p.getBytesLength("recent");
  if (rec && len && len % sizeof(RecentRec) == 0 && len <= RECENT_MAX * sizeof(RecentRec)) {
    p.getBytes("recent", rec, len);
    recN = len / sizeof(RecentRec);
  } else if (rec && p.getBytesLength("last") == sizeof(Item)) {  // früher: nur die letzte Folge
    p.getBytes("last", &rec[0].item, sizeof(Item));
    rec[0].kid = p.getBool("lastkid", false);
    recN = rec[0].item.id[0] ? 1 : 0;
    if (recN) p.putBytes("recent", rec, sizeof(RecentRec));
  }
  p.remove("last");
  p.remove("lastkid");
  p.end();
  xTaskCreatePinnedToCore(worker, "browse", 12288, nullptr, 3, nullptr, 1);
}

}  // namespace music
