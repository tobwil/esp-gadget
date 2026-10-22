#include "podcast.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <map>
#include <mp3dec.h>
#include "art.h"
#include "favs.h"
#include "audio.h"
#include "web.h"

namespace podcast {

static SemaphoreHandle_t mtx;
static music::State st;

static void ucopy(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  size_t len = strlen(src);
  if (len < n) { memcpy(dst, src, len + 1); return; }
  size_t cut = n - 1;
  while (cut > 0 && ((uint8_t)src[cut] & 0xC0) == 0x80) cut--;
  memcpy(dst, src, cut);
  dst[cut] = 0;
}

static uint32_t hash(const char* s) {  // FNV-1a
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

// Lange Folgen-Adressen (Tracking-Weiterleitungen) passen nicht in Item.id: Kurzform „pcurl:<hash>“
static std::map<uint32_t, String> longUrls;

static String episodeUrl(const char* id) {
  if (!strncmp(id, "pcurl:", 6)) {
    auto it = longUrls.find(strtoul(id + 6, nullptr, 16));
    return it == longUrls.end() ? String() : it->second;
  }
  return id;
}

// zuletzt gelesener Feed (Titel, Cover) – für Abos und die Anzeige beim Abspielen
static String feedTitle, feedArt, feedUrlCur;

// ------------------------------------------------------------------ Position je Folge (Bytes + Sekunden)

static void savePos(const char* url, uint32_t bytes, uint32_t sec) {
  char key[12];
  snprintf(key, sizeof(key), "p%08x", (unsigned)hash(url));
  Preferences p;
  p.begin("pcpos", false);
  if (p.freeEntries() < 4) p.clear();  // Platz knapp: alte Stände verwerfen
  if (bytes) p.putULong64(key, ((uint64_t)bytes << 20) | (sec & 0xFFFFF));
  else p.remove(key);
  p.end();
}

static void loadPos(const char* url, uint32_t* bytes, uint32_t* sec) {
  char key[12];
  snprintf(key, sizeof(key), "p%08x", (unsigned)hash(url));
  Preferences p;
  p.begin("pcpos", true);
  uint64_t v = p.getULong64(key, 0);
  p.end();
  *bytes = v >> 20;
  *sec = v & 0xFFFFF;
}

// ------------------------------------------------------------------ Feeds und Suche

static String between(const String& s, const char* a, const char* b, int from = 0) {
  int i = s.indexOf(a, from);
  if (i < 0) return "";
  i += strlen(a);
  int j = s.indexOf(b, i);
  if (j < 0) return "";
  String r = s.substring(i, j);
  r.trim();
  if (r.startsWith("<![CDATA[")) r = r.substring(9, r.endsWith("]]>") ? r.length() - 3 : r.length());
  // die häufigsten Entitäten
  r.replace("&amp;", "&");
  r.replace("&quot;", "\"");
  r.replace("&apos;", "'");
  r.replace("&#39;", "'");
  r.replace("&lt;", "<");
  r.replace("&gt;", ">");
  r.trim();
  return r;
}

// „Wed, 22 Oct 2026 06:00:00 +0000“ → „22. Okt. 2026“
static String germanDate(const String& d) {
  static const char* EN[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  static const char* DE[] = {"Jan.", "Feb.", "März", "Apr.", "Mai", "Juni", "Juli", "Aug.", "Sep.", "Okt.", "Nov.", "Dez."};
  int day = 0, year = 0;
  char mon[8] = "";
  const char* p = strchr(d.c_str(), ',');
  if (sscanf(p ? p + 1 : d.c_str(), "%d %7s %d", &day, mon, &year) != 3) return d.substring(0, 16);
  for (int i = 0; i < 12; i++)
    if (!strcmp(mon, EN[i])) return String(day) + ". " + DE[i] + " " + year;
  return d.substring(0, 16);
}

static int parseDur(const String& d) {
  if (!d.length()) return 0;
  int h = 0, m = 0, s = 0;
  int n = sscanf(d.c_str(), "%d:%d:%d", &h, &m, &s);
  if (n == 3) return h * 3600 + m * 60 + s;
  if (n == 2) return h * 60 + m;
  return h;
}

static bool readFeed(const char* url, char* title, size_t titleLen, music::Item* items, int max, int* count, char* err,
                     size_t errLen) {
  web::Conn* c = new web::Conn();
  bool ok = web::open(*c, url);
  if (!ok) {
    snprintf(err, errLen, "Feed nicht erreichbar (%d)", c->code);
    delete c;
    return false;
  }
  feedUrlCur = url;
  feedTitle = "";
  feedArt = "";
  NetworkClient* s = c->stream();
  String win;
  win.reserve(32768);
  uint8_t buf[2048];
  bool head = false;
  uint32_t idle = millis();
  while (*count < max && (s->connected() || s->available())) {
    int n = s->read(buf, sizeof(buf));
    if (n <= 0) {
      if (millis() - idle > 10000) break;
      delay(5);
      continue;
    }
    idle = millis();
    win.concat((const char*)buf, n);
    if (!head) {
      int it = win.indexOf("<item");
      if (it < 0 && win.length() < 65536) continue;
      String ch = it > 0 ? win.substring(0, it) : win;
      feedTitle = between(ch, "<title>", "</title>");
      feedArt = between(ch, "<itunes:image href=\"", "\"");
      if (!feedArt.length()) feedArt = between(ch, "<url>", "</url>");
      ucopy(title, titleLen, feedTitle.length() ? feedTitle.c_str() : "Podcast");
      head = true;
    }
    for (;;) {
      int a = win.indexOf("<item");
      int b = a < 0 ? -1 : win.indexOf("</item>", a);
      if (b < 0) break;
      String item = win.substring(a, b);
      win.remove(0, b + 7);
      String encl = between(item, "<enclosure", ">");
      String u = between(encl, "url=\"", "\"");
      if (!u.length()) u = between(encl, "url='", "'");
      if (!u.length() || *count >= max) continue;
      u.replace("&amp;", "&");
      music::Item& o = items[(*count)++];
      o.kind = music::K_TRACK;
      o.dur = parseDur(between(item, "<itunes:duration>", "</itunes:duration>"));
      if (u.length() < sizeof(o.id)) ucopy(o.id, sizeof(o.id), u.c_str());
      else {
        uint32_t h = hash(u.c_str());
        longUrls[h] = u;
        snprintf(o.id, sizeof(o.id), "pcurl:%08x", (unsigned)h);
      }
      ucopy(o.title, sizeof(o.title), between(item, "<title>", "</title>").c_str());
      ucopy(o.sub, sizeof(o.sub), germanDate(between(item, "<pubDate>", "</pubDate>")).c_str());
    }
    if (win.length() > 300000) break;  // eine Folge mit riesiger Beschreibung: aufhören
  }
  delete c;
  if (!*count) { snprintf(err, errLen, "Keine Folgen gefunden"); return false; }
  return true;
}

static bool search(const char* q, char* title, size_t titleLen, music::Item* items, int max, int* count, char* err,
                   size_t errLen) {
  String body;
  int code = web::get(String("https://itunes.apple.com/search?media=podcast&country=DE&limit=20&term=") +
                          web::urlencode(q), body);
  if (code != 200) { snprintf(err, errLen, "Suche nicht erreichbar (%d)", code); return false; }
  JsonDocument d;
  if (deserializeJson(d, body)) { snprintf(err, errLen, "Antwort unlesbar"); return false; }
  snprintf(title, titleLen, "„%s“", q);
  for (JsonObject r : d["results"].as<JsonArray>()) {
    const char* feed = r["feedUrl"] | "";
    if (!feed[0] || *count >= max || strlen(feed) + 8 >= sizeof(items[0].id)) continue;
    music::Item& o = items[(*count)++];
    o.kind = music::K_LIST;
    o.dur = 0;
    snprintf(o.id, sizeof(o.id), "pc:feed:%s", feed);
    ucopy(o.title, sizeof(o.title), r["collectionName"] | "");
    ucopy(o.sub, sizeof(o.sub), r["artistName"] | "");
  }
  if (!*count) snprintf(err, errLen, "Nichts gefunden");
  return *count > 0;
}

bool browse(const char* path, char* title, size_t titleLen, music::Item* items, int max, int* count, char* err,
            size_t errLen) {
  *count = 0;
  err[0] = 0;
  if (!strcmp(path, "pc:subs")) {
    ucopy(title, titleLen, "Meine Podcasts");
    music::Item& f = items[(*count)++];
    f.kind = music::K_LIST;
    f.dur = 0;
    ucopy(f.id, sizeof(f.id), "@pcsearch");
    ucopy(f.title, sizeof(f.title), "Podcast suchen");
    ucopy(f.sub, sizeof(f.sub), "Sprache oder Tastatur");
    *count += favs::list(items + *count, max - *count, "pc:feed:");  // gemerkte Podcasts
    return true;
  }
  if (!strncmp(path, "pcsearch:", 9)) return search(path + 9, title, titleLen, items, max, count, err, errLen);
  if (!strncmp(path, "pc:feed:", 8)) return readFeed(path + 8, title, titleLen, items, max, count, err, errLen);
  snprintf(err, errLen, "Unbekannte Liste");
  return false;
}

// ------------------------------------------------------------------ Wiedergabe

enum Cmd : uint8_t { C_NONE, C_PLAY, C_TOGGLE, C_SKIP, C_STOP };
static volatile Cmd cmd = C_NONE;
static volatile int skipBy = 0;
static String reqUrl, reqTitle, reqShow, reqArt;
static uint16_t reqDur = 0;
static volatile bool paused = false;
static volatile uint32_t baseSec = 0;

void play(const music::Item& e) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  reqUrl = episodeUrl(e.id);
  reqTitle = e.title;
  reqShow = feedTitle;
  reqArt = feedArt;
  reqDur = e.dur;
  xSemaphoreGive(mtx);
  cmd = C_PLAY;
}
void toggle() { cmd = C_TOGGLE; }
void skip(int s) { skipBy = s; cmd = C_SKIP; }
void stop() { cmd = C_STOP; }

music::State state() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  music::State s = st;
  xSemaphoreGive(mtx);
  s.playing = s.active && !paused;
  s.pos = baseSec + audio::musicPlayed() / (float)audio::SR;
  return s;
}

static void setMsg(const char* m) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  ucopy(st.msg, sizeof(st.msg), m);
  xSemaphoreGive(mtx);
}

// Länge eines ID3v2-Kopfs am Dateianfang (0 = keiner)
static uint32_t id3Size(const uint8_t* b, int n) {
  if (n < 10 || memcmp(b, "ID3", 3)) return 0;
  return 10 + ((b[6] & 0x7F) << 21 | (b[7] & 0x7F) << 14 | (b[8] & 0x7F) << 7 | (b[9] & 0x7F));
}

static void task(void*) {
  HMP3Decoder dec = MP3InitDecoder();
  static uint8_t in[8192];
  static int16_t pcm[1152 * 2];
  static int16_t mono[1152 * 3];
  web::Conn* c = nullptr;
  String url;
  uint32_t offset = 0, pausedAt = 0, savedAt = 0, kbps = 128;
  int have = 0;
  uint32_t skipId3 = 0;
  bool id3Checked = false;
  float rsPhase = 0;
  int16_t rsPrev = 0;
  bool reopen = false;

  auto close = [&]() { if (c) { delete c; c = nullptr; } };
  auto openAt = [&](uint32_t at, uint32_t sec) -> bool {
    close();
    audio::musicPause(true);
    audio::musicFlush();
    baseSec = sec;
    have = 0;
    c = new web::Conn();
    char range[40];
    snprintf(range, sizeof(range), "Range: bytes=%u-", (unsigned)at);
    if (!web::open(*c, url, at ? range : nullptr, 15000)) {
      Serial.printf("Podcast: Folge nicht erreichbar (%d)\n", c->code);
      close();
      setMsg("Folge nicht erreichbar");
      return false;
    }
    if (at && c->code != 206) { at = 0; baseSec = 0; }  // Server kann keine Bereiche: von vorn
    offset = at;
    skipId3 = 0;
    id3Checked = false;
    rsPhase = 0;
    setMsg("");
    if (!paused) audio::musicPause(false);
    return true;
  };

  for (;;) {
    Cmd k = cmd;
    if (k != C_NONE) {
      cmd = C_NONE;
      if (k == C_PLAY) {
        if (c && url.length()) savePos(url.c_str(), offset, baseSec + audio::musicPlayed() / audio::SR);
        xSemaphoreTake(mtx, portMAX_DELAY);
        url = reqUrl;
        st.active = true;
        st.src = music::SRC_PODCAST;
        ucopy(st.title, sizeof(st.title), reqTitle.c_str());
        ucopy(st.artist, sizeof(st.artist), reqShow.c_str());
        snprintf(st.id, sizeof(st.id), "%08x", (unsigned)hash(url.c_str()));
        st.dur = reqDur;
        xSemaphoreGive(mtx);
        art::request(reqArt.c_str());
        uint32_t at, sec;
        loadPos(url.c_str(), &at, &sec);  // dort weiter, wo man aufgehört hat
        paused = false;
        Serial.printf("Podcast: %s (ab %u s)\n", reqTitle.c_str(), (unsigned)sec);
        openAt(at, sec);
      } else if (k == C_TOGGLE && st.active) {
        paused = !paused;
        audio::musicPause(paused);
        if (paused) { pausedAt = millis(); savePos(url.c_str(), offset, baseSec + audio::musicPlayed() / audio::SR); }
        else if (reopen || !c) { reopen = false; openAt(offset, baseSec + audio::musicPlayed() / audio::SR); }
      } else if (k == C_SKIP && st.active) {
        uint32_t sec = baseSec + audio::musicPlayed() / audio::SR;
        int target = max(0, (int)sec + skipBy);
        // Byte-Position aus der Bitrate schätzen (MP3 meist mit konstanter Bitrate):
        // gelesen − noch im Eingangspuffer − noch im Musikpuffer, dann um die Sprungweite verschieben
        int32_t bps = (int32_t)kbps * 125;
        int32_t heard = (int32_t)offset - have - (int32_t)(audio::musicUsed() / audio::SR) * bps;
        int32_t at = max((int32_t)0, heard + (target - (int)sec) * bps);
        openAt(at, target);
      } else if (k == C_STOP) {
        if (c) savePos(url.c_str(), offset, baseSec + audio::musicPlayed() / audio::SR);
        close();
        audio::musicPause(true);
        audio::musicFlush();
        xSemaphoreTake(mtx, portMAX_DELAY);
        st.active = false;
        xSemaphoreGive(mtx);
      }
    }
    if (!c) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

    // lange Pause: Verbindung schließen, beim Weiterhören mit Range neu öffnen
    if (paused && millis() - pausedAt > 30000) { close(); reopen = true; continue; }
    // Position regelmäßig sichern
    if (millis() - savedAt > 15000) { savedAt = millis(); savePos(url.c_str(), offset, baseSec + audio::musicPlayed() / audio::SR); }

    // Puffer füllen
    NetworkClient* s = c->stream();
    if (have < (int)sizeof(in) / 2) {
      int n = s->read(in + have, sizeof(in) - have);
      if (n > 0) { have += n; offset += n; }
      else if (!s->connected() && !s->available() && have < 4) {
        // Folge zu Ende: nach dem Leerlaufen des Puffers als gehört markieren
        close();
        while (audio::musicUsed() && cmd == C_NONE) vTaskDelay(pdMS_TO_TICKS(100));
        if (cmd == C_NONE) { savePos(url.c_str(), 0, 0); xSemaphoreTake(mtx, portMAX_DELAY); st.active = false; xSemaphoreGive(mtx); }
        continue;
      }
    }
    if (!id3Checked && have >= 10) {  // ID3-Kopf (oft mit großem Bild) am Dateianfang überspringen
      id3Checked = true;
      if (offset == (uint32_t)have) skipId3 = id3Size(in, have);
    }
    if (skipId3) {
      uint32_t k2 = min((uint32_t)have, skipId3);
      memmove(in, in + k2, have - k2);
      have -= k2;
      skipId3 -= k2;
      continue;
    }
    if (have < 2048) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
    // Platz im Musikpuffer abwarten
    if (audio::musicSpace() < 1152 * 3) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
    int sync = MP3FindSyncWord(in, have);
    if (sync < 0) { have = 0; continue; }
    unsigned char* p = in + sync;
    int left = have - sync;
    int err = MP3Decode(dec, &p, &left, pcm, 0);
    int used = (have - sync) - left;
    if (err) {  // kaputter Rahmen: ein Byte weiter suchen
      used = max(used, 1);
    } else {
      MP3FrameInfo fi;
      MP3GetLastFrameInfo(dec, &fi);
      if (fi.bitrate) kbps = fi.bitrate / 1000;
      int frames = fi.outputSamps / max(1, fi.nChans);
      // auf 44,1 kHz mono umrechnen (linear)
      float step = (float)fi.samprate / audio::SR;
      int on = 0;
      for (int i = 0; i < frames; i++) {
        int16_t x = fi.nChans == 2 ? ((int)pcm[2 * i] + pcm[2 * i + 1]) / 2 : pcm[i];
        while (rsPhase < 1 && on < (int)(sizeof(mono) / sizeof(mono[0]))) {
          mono[on++] = rsPrev + (x - rsPrev) * rsPhase;
          rsPhase += step;
        }
        rsPhase -= 1;
        rsPrev = x;
      }
      audio::musicWrite(mono, on);
    }
    memmove(in, in + sync + used, have - sync - used);
    have -= sync + used;
  }
}

void begin() {
  mtx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "podcast", 12288, nullptr, 5, nullptr, 1);
}

}  // namespace podcast
