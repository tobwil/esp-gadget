#include "music.h"
#include "art.h"
#include "favs.h"
#include "kids.h"
#include "audio.h"
#include "settings.h"
#include "podcast.h"
#include "ard.h"
#include "webradio.h"
#include "spotify.h"

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
  if (!strncmp(path, "sp:album:", 9)) return "Album";
  if (!strncmp(path, "sp:playlist:", 12)) return "Playlist";
  if (!strncmp(path, "sp:artist:", 10)) return "Interpret";
  if (!strncmp(path, "pc:feed:", 8)) return "Podcast";
  if (!strncmp(path, "ard:set:", 8)) return "Audiothek";
  return "";
}

static char lastTitle[72] = "";

// Startmenü der Musik-App
static void home(Item* it, int* n, char* title, size_t titleLen) {
  ucopy(title, titleLen, "Musik");
  addList(it, n, "rl:home", "Radio", "Sender aus aller Welt");
  addList(it, n, "ard:home", "ARD Audiothek", "Hörspiele, Wissen, Kinder …");
  addList(it, n, "pc:subs", "Podcasts", "Meine Podcasts und Suche");
  addList(it, n, "favs", "Favoriten", "Gemerkte Sender, Sendungen, Listen");
  addList(it, n, "sp:home", "Spotify", spotify::linked() ? "Suchen und abspielen" : "Noch nicht gekoppelt");
}

static void spotifyHome(Item* it, int* n, char* title, size_t titleLen) {
  ucopy(title, titleLen, "Spotify");
  addList(it, n, "@voice", "Sprechen", "Titel, Interpret, Album …");
  addList(it, n, "@search", "Suchen", "Mit der Tastatur");
  if (!spotify::linked())
    addList(it, n, "@spotifyhelp", "Spotify koppeln", "In der Spotify-App die Uhr als Lautsprecher wählen");
}

// Kindermodus: nur die freigegebenen Listen
static void kidsHome(Item* it, int* n, char* title, size_t titleLen, char* err, size_t errLen) {
  ucopy(title, titleLen, "Musik");
  *n = kids::list(it, BROWSE_MAX);
  if (!*n) ucopy(err, errLen, "Noch nichts freigegeben");
}

// Liste holen (läuft im Arbeits-Task)
static int fetch(const char* path, Item* tmp, char* title, size_t titleLen, char* err, size_t errLen) {
  int n = 0;
  if (!strncmp(path, "rl", 2)) wradio::browse(path, title, titleLen, tmp, BROWSE_MAX, &n, err, errLen);
  else if (!strncmp(path, "ard", 3)) ard::browse(path, title, titleLen, tmp, BROWSE_MAX, &n, err, errLen);
  else if (!strncmp(path, "pc", 2)) podcast::browse(path, title, titleLen, tmp, BROWSE_MAX, &n, err, errLen);
  else spotify::browse(path, title, titleLen, tmp, BROWSE_MAX, &n, err, errLen);
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
      qWanted = false;
      qDone = true;
    }
    if (!wanted) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }
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
    if (kid) {
      if (!strcmp(path, "home")) kidsHome(tmp, &n, title, sizeof(title), err, sizeof(err));
      else if (kids::released(path)) n = fetch(path, tmp, title, sizeof(title), err, sizeof(err));
      else ucopy(err, sizeof(err), "Nicht freigegeben");
    } else if (!strcmp(path, "home")) home(tmp, &n, title, sizeof(title));
    else if (!strcmp(path, "sp:home")) spotifyHome(tmp, &n, title, sizeof(title));
    else if (!strcmp(path, "favs")) {
      ucopy(title, sizeof(title), "Favoriten");
      n = favs::list(tmp, BROWSE_MAX);
      if (!n) ucopy(err, sizeof(err), "Noch keine – Sender lange drücken, Listen oben merken");
    } else n = fetch(path, tmp, title, sizeof(title), err, sizeof(err));
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

static void playStation(int i) {
  if (stations.empty()) return;
  stationIdx = (i % (int)stations.size() + stations.size()) % stations.size();
  const Item& s = stations[stationIdx];
  const char* url = s.id + 6;  // „radio:“
  podcast::playUrl(url, s.title, s.title, wradio::logo(url).c_str(), 0, true);
}

void playList(const Item* list, int n, int start) {
  if (start < 0 || start >= n || list[start].kind != K_TRACK) return;
  const char* id = list[start].id;
  if (!strncmp(id, "spotify:", 8)) {
    std::vector<std::string> uris;
    int index = 0;
    for (int i = 0; i < n; i++) {
      if (list[i].kind != K_TRACK || strncmp(list[i].id, "spotify:", 8)) continue;
      if (i == start) index = uris.size();
      uris.push_back(list[i].id);
    }
    if (active == SRC_PODCAST || active == SRC_RADIO) podcast::stop();
    if (spotify::play(uris, index, uris[index])) active = SRC_SPOTIFY;
    return;
  }
  if (active == SRC_SPOTIFY) spotify::stop();
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
    String show, image;
    ard::meta(id + 6, show, image);
    podcast::playUrl(id + 6, list[start].title, show.c_str(), image.c_str(), list[start].dur, false);
    active = SRC_PODCAST;
  } else {
    podcast::play(list[start]);
    active = SRC_PODCAST;
  }
}

static Source current() {
  // die Spotify-App kann jederzeit übernehmen
  if (spotify::state().active && active != SRC_PODCAST && active != SRC_RADIO) active = SRC_SPOTIFY;
  return active;
}

void toggle() {
  Source s = current();
  if (s == SRC_SPOTIFY) spotify::toggle();
  else if (s == SRC_PODCAST || s == SRC_RADIO) podcast::toggle();
}
void next() {  // Podcast: +30 s, Radio: nächster Sender
  Source s = current();
  if (s == SRC_SPOTIFY) spotify::next();
  else if (s == SRC_RADIO) playStation(stationIdx + 1);
  else if (s == SRC_PODCAST) podcast::skip(30);
}
void prev() {
  Source s = current();
  if (s == SRC_SPOTIFY) spotify::prev();
  else if (s == SRC_RADIO) playStation(stationIdx - 1);
  else if (s == SRC_PODCAST) podcast::skip(-15);
}
void stop() {
  if (active == SRC_SPOTIFY) spotify::stop();
  if (active == SRC_PODCAST || active == SRC_RADIO) podcast::stop();
  active = SRC_NONE;
}

State state() {
  Source s = current();
  if (s == SRC_SPOTIFY) return spotify::state();
  if (s == SRC_PODCAST || s == SRC_RADIO) return podcast::state();
  State e{};
  return e;
}

const uint16_t* art(uint32_t* version) { return art::image(version); }
int takeRemoteVolume() { return spotify::takeRemoteVolume(); }
void volumeChanged(int pct) {
  fadeInAt = 0;  // von Hand lauter/leiser: Einblenden beenden
  spotify::volumeChanged(pct);
}
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
  spotify::poll();
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
  spotify::begin();
  podcast::begin();
  xTaskCreatePinnedToCore(worker, "browse", 12288, nullptr, 3, nullptr, 1);
}

}  // namespace music
