#include "music.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "audio.h"
#include "hw.h"
#include "net.h"
#include "radio.h"
#include "settings.h"

namespace music {

static const int QMAX = 200, BROWSE_MAX = 80;

struct Track {
  char id[16];
  char title[72];
  char artist[56];
  uint16_t dur;
};

static Track* queue;  // PSRAM
static int qCount = 0, qIndex = 0;
static SemaphoreHandle_t mtx;
static State st;

static char bridge[100] = "";
static volatile bool bridgeOk = false, bridgeDirty = true;

// ------------------------------------------------------------------ Hilfen

// UTF-8-sicher kopieren: nie mitten in einem Zeichen abschneiden
static void ucopy(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  size_t len = strlen(src);
  if (len < n) { memcpy(dst, src, len + 1); return; }
  size_t cut = n - 1;
  while (cut > 0 && ((uint8_t)src[cut] & 0xC0) == 0x80) cut--;
  memcpy(dst, src, cut);
  dst[cut] = 0;
}

static String urlencode(const char* s) {
  String o;
  for (; *s; s++) {
    uint8_t c = *s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') o += (char)c;
    else { char b[4]; snprintf(b, sizeof(b), "%%%02X", c); o += b; }
  }
  return o;
}

static void setMsg(const char* m) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  ucopy(st.msg, sizeof(st.msg), m);
  xSemaphoreGive(mtx);
}

bool bridgeKnown() { return bridgeOk; }
const char* bridgeUrl() { return bridge; }
void bridgeChanged() { bridgeDirty = true; bridgeOk = false; }

State state() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  State s = st;
  xSemaphoreGive(mtx);
  return s;
}

// ------------------------------------------------------------------ Cover

static uint16_t* artBuf[2];
static volatile int artFront = -1;
static volatile uint32_t artVer = 0;
static char artWant[16] = "";
static volatile bool artReq = false;

const uint16_t* art(uint32_t* version) {
  *version = artVer;
  return artFront < 0 ? nullptr : artBuf[artFront];
}

static void fetchArt(const char* id) {
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(8000);
  String url = String(bridge) + "/art/" + id + "?size=" + ART;
  if (!http.begin(url)) return;
  int code = http.GET();
  const size_t need = ART * ART * 2;
  if (code == 200 && http.getSize() == (int)need) {
    int back = artFront == 0 ? 1 : 0;
    WiFiClient* s = http.getStreamPtr();
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < need && millis() - t0 < 8000) {
      int n = s->read((uint8_t*)artBuf[back] + got, need - got);
      if (n > 0) got += n; else delay(5);
    }
    if (got == need) { artFront = back; artVer = artVer + 1; }
  }
  http.end();
}

// ------------------------------------------------------------------ Listen (browse)

static Item* browseItems;  // PSRAM
static int browseCount = 0;
static char browseTitle[72] = "", browseErr[64] = "";
static volatile uint32_t bSeq = 0;
static char browseWant[128] = "";
static volatile bool browseReq = false, browsing = false;

void browse(const char* path) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  ucopy(browseWant, sizeof(browseWant), path);
  browseReq = true;
  browsing = true;
  xSemaphoreGive(mtx);
}

bool browseBusy() { return browsing; }
uint32_t browseSeq() { return bSeq; }

int browseCopy(char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  ucopy(title, titleLen, browseTitle);
  ucopy(err, errLen, browseErr);
  int n = min(max, browseCount);
  memcpy(out, browseItems, n * sizeof(Item));
  xSemaphoreGive(mtx);
  return n;
}

// Liest {"title": …, "items": [{kind, id, title, sub, dur}]} in die Liste
static bool fetchList(const char* path, Item* items, int max, int* count, char* title, size_t titleLen, char* err,
                      size_t errLen) {
  *count = 0;
  err[0] = 0;
  if (!bridgeOk) { ucopy(err, errLen, "Musik-Brücke nicht gefunden"); return false; }
  HTTPClient http;
  http.setTimeout(20000);
  String url = String(bridge) + "/api/browse?path=" + urlencode(path);
  if (!http.begin(url)) { ucopy(err, errLen, "Ungültige Adresse"); return false; }
  int code = http.GET();
  if (code != 200) {
    snprintf(err, errLen, code < 0 ? "Brücke nicht erreichbar" : "Brücke: Fehler %d", code);
    http.end();
    if (code < 0) bridgeOk = false;
    return false;
  }
  String body = http.getString();
  http.end();
  JsonDocument doc;
  if (deserializeJson(doc, body)) { ucopy(err, errLen, "Antwort unlesbar"); return false; }
  if (doc["error"].is<const char*>()) { ucopy(err, errLen, doc["error"]); return false; }
  ucopy(title, titleLen, doc["title"] | "");
  for (JsonObject it : doc["items"].as<JsonArray>()) {
    if (*count >= max) break;
    Item& o = items[(*count)++];
    o.kind = strcmp(it["kind"] | "track", "track") ? K_LIST : K_TRACK;
    o.dur = it["dur"] | 0;
    ucopy(o.id, sizeof(o.id), it["id"] | "");
    ucopy(o.title, sizeof(o.title), it["title"] | "");
    ucopy(o.sub, sizeof(o.sub), it["sub"] | "");
  }
  return true;
}

// ------------------------------------------------------------------ Wiedergabe

enum Cmd : uint8_t { C_PLAY, C_NEXT, C_PREV, C_STOP, C_TOGGLE };
static QueueHandle_t cmdq;
static char radioWant[16] = "";
static volatile bool radioReq = false, radioReplace = false;
static volatile int remoteVol = -1;

int takeRemoteVolume() { int v = remoteVol; remoteVol = -1; return v; }

static void setQueue(const Item* items, int n, int start) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  qCount = 0;
  int idx = 0;
  for (int i = 0; i < n && qCount < QMAX; i++) {
    if (items[i].kind != K_TRACK) continue;
    if (i == start) idx = qCount;
    Track& t = queue[qCount++];
    ucopy(t.id, sizeof(t.id), items[i].id);
    ucopy(t.title, sizeof(t.title), items[i].title);
    ucopy(t.artist, sizeof(t.artist), items[i].sub);
    t.dur = items[i].dur;
  }
  qIndex = idx;
  xSemaphoreGive(mtx);
}

void playList(const Item* items, int n, int start) {
  setQueue(items, n, start);
  if (!qCount) return;
  Cmd c = C_PLAY;
  xQueueSend(cmdq, &c, 0);
}

void playRadio(const char* videoId) {
  ucopy(radioWant, sizeof(radioWant), videoId);
  radioReplace = true;
  radioReq = true;
  setMsg("Radio wird geladen …");
}

static void cmd(Cmd c) { xQueueSend(cmdq, &c, 0); }
void toggle() { cmd(C_TOGGLE); }
void next() { cmd(C_NEXT); }
void prev() { cmd(C_PREV); }
void stop() { cmd(C_STOP); }

static void streamTask(void*) {
  static uint8_t buf[4096];
  HTTPClient http;
  WiFiClient* s = nullptr;
  bool open = false, streamDone = false, userPaused = false, reopen = false, buffering = false;
  int carry = -1;           // übrig gebliebenes Byte (ungerade Paketgrenze)
  uint32_t posBase = 0, bytes = 0, pausedAt = 0, failAt = 0;
  int retries = 0;
  Track cur{};

  auto closeStream = [&]() {
    if (open) http.end();
    open = false;
    s = nullptr;
  };

  auto position = [&]() -> float { return posBase + audio::musicPlayed() / (float)audio::SR; };

  auto start = [&](uint32_t at) {
    closeStream();
    audio::musicPause(true);
    audio::musicFlush();
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (qIndex < 0 || qIndex >= qCount) { xSemaphoreGive(mtx); return; }
    cur = queue[qIndex];
    st.active = true;
    st.index = qIndex;
    st.count = qCount;
    ucopy(st.title, sizeof(st.title), cur.title);
    ucopy(st.artist, sizeof(st.artist), cur.artist);
    ucopy(st.id, sizeof(st.id), cur.id);
    st.dur = cur.dur;
    st.pos = at;
    st.buffering = true;
    st.playing = true;
    st.msg[0] = 0;
    xSemaphoreGive(mtx);
    if (at == 0) { ucopy(artWant, sizeof(artWant), cur.id); artReq = true; retries = 0; }
    posBase = at;
    bytes = 0;
    carry = -1;
    userPaused = false;
    reopen = false;
    streamDone = false;
    buffering = true;
    http.useHTTP10(true);
    http.setTimeout(20000);
    http.setReuse(false);
    String url = String(bridge) + "/stream/" + cur.id + "?start=" + at;
    Serial.printf("Musik: %s – %s (ab %u s)\n", cur.artist, cur.title, at);
    if (!bridgeOk || !http.begin(url)) { setMsg("Musik-Brücke nicht gefunden"); failAt = millis(); return; }
    const char* hdrs[] = {"X-Duration"};
    http.collectHeaders(hdrs, 1);
    int code = http.GET();
    if (code == 200 && !cur.dur && http.hasHeader("X-Duration")) {  // Länge fehlte in der Liste
      cur.dur = at + http.header("X-Duration").toInt();
      xSemaphoreTake(mtx, portMAX_DELAY);
      st.dur = cur.dur;
      if (qIndex < qCount) queue[qIndex].dur = cur.dur;
      xSemaphoreGive(mtx);
    }
    if (code != 200) {
      char m[48];
      snprintf(m, sizeof(m), code < 0 ? "Brücke nicht erreichbar" : "Titel nicht abspielbar (%d)", code);
      setMsg(m);
      http.end();
      failAt = millis();
      return;
    }
    s = http.getStreamPtr();
    open = true;
  };

  auto advance = [&](int dir) -> bool {
    xSemaphoreTake(mtx, portMAX_DELAY);
    int ni = qIndex + dir;
    bool ok = ni >= 0 && ni < qCount;
    if (ok) qIndex = ni;
    xSemaphoreGive(mtx);
    return ok;
  };

  for (;;) {
    Cmd c;
    bool idle = !open && !st.active;
    while (xQueueReceive(cmdq, &c, idle ? pdMS_TO_TICKS(200) : 0) == pdTRUE) {
      switch (c) {
        case C_PLAY: start(0); break;
        case C_NEXT:
          if (advance(1)) start(0);
          else if (cur.id[0]) { ucopy(radioWant, sizeof(radioWant), cur.id); radioReplace = false; radioReq = true; setMsg("Radio geht weiter …"); }
          break;
        case C_PREV:
          if (position() > 5 || !advance(-1)) start(0);
          else start(0);
          break;
        case C_STOP:
          closeStream();
          audio::musicPause(true);
          audio::musicFlush();
          xSemaphoreTake(mtx, portMAX_DELAY);
          st.active = false;
          st.playing = false;
          xSemaphoreGive(mtx);
          cur.id[0] = 0;
          break;
        case C_TOGGLE:
          if (!st.active) break;
          userPaused = !userPaused;
          if (userPaused) { audio::musicPause(true); pausedAt = millis(); }
          else if (reopen) start((uint32_t)position());
          else if (!buffering) audio::musicPause(false);
          xSemaphoreTake(mtx, portMAX_DELAY);
          st.playing = !userPaused;
          xSemaphoreGive(mtx);
          break;
      }
    }

    if (open) {
      // Lange Pause: Verbindung schließen, beim Weiterspielen an derselben Stelle neu öffnen
      if (userPaused && millis() - pausedAt > 25000) {
        uint32_t at = (uint32_t)position();
        closeStream();
        audio::musicFlush();
        posBase = at;
        reopen = true;
      } else {
        int avail = s->available();
        size_t space = audio::musicSpace() * 2;
        if (avail > 0 && space >= 2048) {
          int n = s->read(buf, min((size_t)avail, min(sizeof(buf), space - 2)));
          if (n > 0) {
            bytes += n;
            uint8_t* p = buf;
            static int16_t tmp[2048 + 1];
            int k = 0;
            if (carry >= 0) { tmp[k++] = (int16_t)(carry | (p[0] << 8)); p++; n--; carry = -1; }
            for (int i = 0; i + 1 < n; i += 2) tmp[k++] = (int16_t)(p[i] | (p[i + 1] << 8));
            if (n & 1) carry = p[n - 1];
            audio::musicWrite(tmp, k);
          }
        } else if (avail <= 0 && !s->connected()) {
          closeStream();
          streamDone = true;
        } else {
          vTaskDelay(pdMS_TO_TICKS(5));
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(open ? 0 : 20));
    }

    // Vorpuffern und Aussetzer abfangen
    size_t used = audio::musicUsed();
    if (st.active && !reopen) {
      if (buffering && (used >= audio::SR * 3 / 4 || (streamDone && used))) {
        buffering = false;
        if (!userPaused) audio::musicPause(false);
      } else if (!buffering && open && used == 0) {
        buffering = true;
        audio::musicPause(true);
      }
    }

    // Titel zu Ende (oder abgebrochen)
    if (streamDone && used == 0 && !userPaused) {
      streamDone = false;
      float expected = cur.dur * (float)audio::SR * 2 * 0.95f;
      if (cur.dur && posBase == 0 && bytes < expected && bytes > 0 && retries < 2) {
        retries++;
        Serial.println("Musik: Strom abgebrochen, setze fort");
        start((uint32_t)position());
      } else if (advance(1)) {
        start(0);
      } else {
        ucopy(radioWant, sizeof(radioWant), cur.id);
        radioReplace = false;
        radioReq = true;
      }
    }
    // Fehler: nach kurzer Pause zum nächsten Titel
    if (failAt && !open && millis() - failAt > 3000) {
      failAt = 0;
      if (advance(1)) start(0);
      else { xSemaphoreTake(mtx, portMAX_DELAY); st.buffering = false; st.playing = false; xSemaphoreGive(mtx); }
    }

    xSemaphoreTake(mtx, portMAX_DELAY);
    st.pos = position();
    st.buffering = st.active && (buffering || reopen) && !userPaused;
    xSemaphoreGive(mtx);
  }
}

// ------------------------------------------------------------------ Hintergrund: Brücke suchen, Listen, Cover, Radio

static void discover() {
  if (cfg.bridge[0]) {
    ucopy(bridge, sizeof(bridge), cfg.bridge);
    size_t n = strlen(bridge);
    if (n && bridge[n - 1] == '/') bridge[n - 1] = 0;
  } else {
    int n = MDNS.queryService("ytbridge", "tcp");
    if (n <= 0) { bridgeOk = false; return; }
    // IPv4 aus der Antwort, sonst aus dem TXT-Eintrag „ip“, sonst den Hostnamen auflösen
    IPAddress ip = MDNS.address(0);
    if (ip == IPAddress() && MDNS.hasTxt(0, "ip")) ip.fromString(MDNS.txt(0, "ip"));
    if (ip == IPAddress()) ip = MDNS.queryHost(MDNS.hostname(0));
    if (ip == IPAddress()) { bridgeOk = false; Serial.println("Musik-Brücke: gefunden, aber ohne Adresse"); return; }
    snprintf(bridge, sizeof(bridge), "http://%s:%u", ip.toString().c_str(), MDNS.port(0));
  }
  HTTPClient http;
  http.setTimeout(4000);
  bool ok = false;
  if (http.begin(String(bridge) + "/api/ping")) { ok = http.GET() == 200; http.end(); }
  bridgeOk = ok;
  Serial.printf("Musik-Brücke: %s %s\n", bridge, ok ? "ok" : "nicht erreichbar");
}

static void workerTask(void*) {
  static Item* tmp = (Item*)heap_caps_malloc(sizeof(Item) * BROWSE_MAX, MALLOC_CAP_SPIRAM);
  uint32_t discAt = 0;
  for (;;) {
    // sofort suchen, wenn eine Liste gewünscht ist und die Brücke noch fehlt; sonst alle 8 s
    bool want = browseReq || radioReq;
    if (netOnline() && (bridgeDirty || (!bridgeOk && (want || millis() - discAt > 8000)))) {
      bridgeDirty = false;
      discAt = millis();
      discover();
    }
    if (browseReq) {
      char path[128], title[72] = "", err[64];
      xSemaphoreTake(mtx, portMAX_DELAY);
      ucopy(path, sizeof(path), browseWant);
      browseReq = false;
      xSemaphoreGive(mtx);
      int n = 0;
      fetchList(path, tmp, BROWSE_MAX, &n, title, sizeof(title), err, sizeof(err));
      xSemaphoreTake(mtx, portMAX_DELAY);
      if (!browseReq) {  // inzwischen schon wieder etwas anderes gewünscht? dann verwerfen
        memcpy(browseItems, tmp, n * sizeof(Item));
        browseCount = n;
        ucopy(browseTitle, sizeof(browseTitle), title);
        ucopy(browseErr, sizeof(browseErr), err);
        browsing = false;
        bSeq = bSeq + 1;
      }
      xSemaphoreGive(mtx);
      continue;
    }
    if (radioReq) {
      radioReq = false;
      char path[40], title[72], err[64];
      snprintf(path, sizeof(path), "radio:%s", radioWant);
      int n = 0;
      if (fetchList(path, tmp, BROWSE_MAX, &n, title, sizeof(title), err, sizeof(err)) && n) {
        if (radioReplace) {
          playList(tmp, n, 0);
        } else {  // anhängen, den Starttitel selbst überspringen
          xSemaphoreTake(mtx, portMAX_DELAY);
          int before = qCount;
          for (int i = 0; i < n && qCount < QMAX; i++) {
            if (tmp[i].kind != K_TRACK || !strcmp(tmp[i].id, radioWant)) continue;
            Track& t = queue[qCount++];
            ucopy(t.id, sizeof(t.id), tmp[i].id);
            ucopy(t.title, sizeof(t.title), tmp[i].title);
            ucopy(t.artist, sizeof(t.artist), tmp[i].sub);
            t.dur = tmp[i].dur;
          }
          bool added = qCount > before;
          xSemaphoreGive(mtx);
          if (added) next();
        }
      } else {
        setMsg(err[0] ? err : "Kein Radio gefunden");
      }
      continue;
    }
    if (artReq && bridgeOk) {
      artReq = false;
      char id[16];
      ucopy(id, sizeof(id), artWant);
      fetchArt(id);
      continue;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// Fernbedienung: Long-Poll bei der Brücke; die Weboberfläche schickt darüber Befehle
static void remoteTask(void*) {
  static Item* items = (Item*)heap_caps_malloc(sizeof(Item) * QMAX, MALLOC_CAP_SPIRAM);
  for (;;) {
    if (!bridgeOk || !netOnline()) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
    State s = state();
    char q[300];
    snprintf(q, sizeof(q), "/api/remote/wait?dev=%08X&name=%s&state=%s&id=%s&pos=%d&vol=%d&bat=%d", (unsigned)radio::myId(),
             urlencode(cfg.name).c_str(), !s.active ? "stopped" : s.playing ? "playing" : "paused", s.id, (int)s.pos,
             cfg.volume, hwBattery());
    HTTPClient http;
    http.setTimeout(30000);
    if (!http.begin(String(bridge) + q)) { vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
    int code = http.GET();
    if (code == 200) {
      String body = http.getString();
      http.end();
      JsonDocument doc;
      if (deserializeJson(doc, body)) continue;
      const char* c = doc["cmd"] | "";
      Serial.printf("Fernbedienung: %s\n", c);
      if (!strcmp(c, "play")) {
        int n = 0;
        for (JsonObject it : doc["tracks"].as<JsonArray>()) {
          if (n >= QMAX) break;
          Item& o = items[n++];
          o.kind = K_TRACK;
          o.dur = it["dur"] | 0;
          ucopy(o.id, sizeof(o.id), it["id"] | "");
          ucopy(o.title, sizeof(o.title), it["title"] | "");
          ucopy(o.sub, sizeof(o.sub), it["sub"] | "");
        }
        if (n) playList(items, n, doc["index"] | 0);
      } else if (!strcmp(c, "radio")) playRadio(doc["id"] | "");
      else if (!strcmp(c, "toggle")) toggle();
      else if (!strcmp(c, "pause")) { if (state().playing) toggle(); }
      else if (!strcmp(c, "resume")) { if (!state().playing) toggle(); }
      else if (!strcmp(c, "next")) next();
      else if (!strcmp(c, "prev")) prev();
      else if (!strcmp(c, "stop")) stop();
      else if (!strcmp(c, "volume")) remoteVol = constrain((int)(doc["value"] | 50), 0, 100);
    } else {
      http.end();
      if (code < 0) vTaskDelay(pdMS_TO_TICKS(3000));
    }
  }
}

void begin() {
  mtx = xSemaphoreCreateMutex();
  cmdq = xQueueCreate(8, sizeof(Cmd));
  queue = (Track*)heap_caps_calloc(QMAX, sizeof(Track), MALLOC_CAP_SPIRAM);
  browseItems = (Item*)heap_caps_calloc(BROWSE_MAX, sizeof(Item), MALLOC_CAP_SPIRAM);
  artBuf[0] = (uint16_t*)heap_caps_malloc(ART * ART * 2, MALLOC_CAP_SPIRAM);
  artBuf[1] = (uint16_t*)heap_caps_malloc(ART * ART * 2, MALLOC_CAP_SPIRAM);
  xTaskCreatePinnedToCore(streamTask, "stream", 8192, nullptr, 6, nullptr, 1);
  xTaskCreatePinnedToCore(workerTask, "musicw", 8192, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(remoteTask, "remote", 8192, nullptr, 2, nullptr, 1);
}

}  // namespace music
