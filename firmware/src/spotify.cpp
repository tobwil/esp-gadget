#include "spotify.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <atomic>
#include <map>
#include "BellLogger.h"
#include "BellTask.h"
#include "CSpotContext.h"
#include "LoginBlob.h"
#include "MercurySession.h"
#include "SpircHandler.h"
#include "TrackPlayer.h"
#include "X509Bundle.h"
#include "art.h"
#include "audio.h"
#include "net.h"
#include "settings.h"
#include "web.h"
#include "keys.h"

// bell erwartet das alte Bundle-Format (siehe tools/make_crt_bundle.sh)
extern const uint8_t bellBundleStart[] asm("_binary_x509_crt_bundle_bell_start");
extern const uint8_t bellBundleEnd[] asm("_binary_x509_crt_bundle_bell_end");

namespace spotify {

static const int ZC_PORT = 8080;
static std::shared_ptr<cspot::LoginBlob> blob;
static std::shared_ptr<cspot::Context> ctx;
static std::shared_ptr<cspot::SpircHandler> handler;
static std::atomic<bool> haveBlob{false}, isConnected{false}, fromZeroconf{false};
static WebServer* zc = nullptr;
static bool mdnsDone = false;
static char userName[64] = "";
static SemaphoreHandle_t mtx;

// Wiedergabezustand (Ereignisse kommen aus den cspot-Aufgaben)
static music::State st;
static std::atomic<bool> paused{true};
static std::atomic<int> remoteVol{-1};
static std::string feedId, pendingId;
static std::atomic<bool> pending{false};
static uint32_t written = 0, pendingAt = 0, trackStartPlayed = 0;
static uint32_t trackBaseMs = 0, nextBaseMs = 0;
static uint32_t durMs = 0;

bool configured() { return keys::spotifyId()[0] && keys::spotifySecret()[0]; }
bool linked() { return haveBlob; }
bool connected() { return isConnected; }
const char* user() { return userName; }
int takeRemoteVolume() { return remoteVol.exchange(-1); }

static void setMsg(const char* m) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  snprintf(st.msg, sizeof(st.msg), "%s", m);
  xSemaphoreGive(mtx);
}

music::State state() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  music::State s = st;
  xSemaphoreGive(mtx);
  s.playing = s.active && !paused;
  uint32_t played = audio::musicPlayed();
  s.pos = (trackBaseMs + (played > trackStartPlayed ? (played - trackStartPlayed) : 0) * 1000.0f / audio::SR) / 1000.0f;
  if (s.dur && s.pos > s.dur) s.pos = s.dur;
  return s;
}

// ------------------------------------------------------------------ Konten (NVS)

struct Acc { String user, cred; };
static Acc accs[ACCOUNTS];
static int accN = 0, accCur = -1;
static std::atomic<bool> relogin{false};
static std::atomic<bool> accDirty{false};  // Konten geändert, poll() speichert sie

static void saveAccounts() {
  Preferences p;
  p.begin("spot", false);
  p.clear();
  p.putUChar("n", accN);
  p.putChar("cur", accCur);
  char k[4];
  for (int i = 0; i < accN; i++) {
    snprintf(k, sizeof(k), "u%d", i);
    p.putString(k, accs[i].user);
    snprintf(k, sizeof(k), "c%d", i);
    p.putString(k, accs[i].cred);
  }
  p.end();
}

static void loadAccounts() {
  Preferences p;
  p.begin("spot", true);
  accN = min((int)p.getUChar("n", 0), ACCOUNTS);
  accCur = p.getChar("cur", -1);
  char k[4];
  for (int i = 0; i < accN; i++) {
    snprintf(k, sizeof(k), "u%d", i);
    accs[i].user = p.getString(k, "");
    snprintf(k, sizeof(k), "c%d", i);
    accs[i].cred = p.getString(k, "");
  }
  p.end();
  if (accCur >= accN) accCur = accN - 1;
}

// erfolgreich angemeldet: Konto (wieder)verwenden bzw. vorne einfügen.
// Läuft im Spotify-Task, dessen Stack im PSRAM liegt – von dort darf nicht in den Flash geschrieben werden
// (der Cache ist dabei aus). Gespeichert wird deshalb erst in poll() aus der Hauptschleife.
static void rememberAccount(const std::string& user, const std::string& cred) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  int i = 0;
  while (i < accN && accs[i].user != user.c_str()) i++;
  if (i == accN) {
    if (accN < ACCOUNTS) accN++;
    for (i = accN - 1; i > 0; i--) accs[i] = accs[i - 1];
    i = 0;
  }
  accs[i].user = user.c_str();
  accs[i].cred = cred.c_str();
  accCur = i;
  xSemaphoreGive(mtx);
  accDirty = true;
}

int accounts(char names[][64], int max, int* current) {
  int n = min(max, accN);
  for (int i = 0; i < n; i++) snprintf(names[i], 64, "%s", accs[i].user.c_str());
  *current = accCur;
  return n;
}

void useAccount(int i) {
  if (i < 0 || i >= accN) return;
  accCur = i;
  saveAccounts();
  blob->loadJson(accs[i].cred.c_str());
  snprintf(userName, sizeof(userName), "%s", accs[i].user.c_str());
  haveBlob = true;
  relogin = true;
  Serial.printf("Spotify: wechsle zu %s\n", userName);
}

void removeAccount(int i) {
  if (i < 0 || i >= accN) return;
  for (int k = i; k < accN - 1; k++) accs[k] = accs[k + 1];
  accN--;
  if (accCur == i) {
    accCur = -1;
    if (accN) useAccount(0);
    else { haveBlob = false; relogin = true; userName[0] = 0; }
  } else if (accCur > i) {
    accCur--;
  }
  saveAccounts();
}

// ------------------------------------------------------------------ Ton

static void flush() {
  audio::musicFlush();
  written = 0;
  trackStartPlayed = 0;
  pending = false;
  feedId.clear();
}

// PCM von cspot: 44,1 kHz Stereo, 16 Bit → mono in den Musikpuffer
static size_t feed(uint8_t* data, size_t bytes, std::string_view id) {
  if (paused && audio::musicSpace() < audio::SR) return 0;  // pausiert: Puffer nicht weiter füllen
  if (id != feedId) {
    // neuer Titel: melden, sobald er wirklich zu hören ist (siehe poll)
    feedId = id;
    pendingId = std::string(id);
    pendingAt = written;
    pending = true;
  }
  size_t frames = min(bytes / 4, audio::musicSpace());
  if (!frames) return 0;
  const int16_t* in = (const int16_t*)data;
  static int16_t mono[512];
  size_t done = 0;
  while (done < frames) {
    size_t n = min(frames - done, sizeof(mono) / sizeof(mono[0]));
    for (size_t i = 0; i < n; i++) mono[i] = ((int)in[2 * (done + i)] + in[2 * (done + i) + 1]) / 2;
    audio::musicWrite(mono, n);
    done += n;
  }
  written += frames;
  return frames * 4;
}

static void onEvent(std::unique_ptr<cspot::SpircHandler::Event> ev) {
  using E = cspot::SpircHandler::EventType;
  switch (ev->eventType) {
    case E::PLAY_PAUSE: {
      bool p = std::get<bool>(ev->data);
      paused = p;
      audio::musicPause(p);
      xSemaphoreTake(mtx, portMAX_DELAY);
      st.active = true;
      st.msg[0] = 0;
      xSemaphoreGive(mtx);
      break;
    }
    case E::TRACK_INFO: {
      auto info = std::get<cspot::TrackInfo>(ev->data);
      xSemaphoreTake(mtx, portMAX_DELAY);
      st.active = true;
      st.src = music::SRC_SPOTIFY;
      snprintf(st.title, sizeof(st.title), "%s", info.name.c_str());
      snprintf(st.artist, sizeof(st.artist), "%s", info.artist.c_str());
      snprintf(st.id, sizeof(st.id), "%s", info.trackId.c_str());
      st.dur = info.duration / 1000;
      st.msg[0] = 0;
      xSemaphoreGive(mtx);
      art::request(info.imageUrl.c_str());
      Serial.printf("Spotify: %s – %s\n", info.artist.c_str(), info.name.c_str());
      break;
    }
    case E::PLAYBACK_START:
      nextBaseMs = std::get<int>(ev->data);
      break;
    case E::FLUSH:
    case E::NEXT:
    case E::PREV:
      flush();
      nextBaseMs = 0;
      break;
    case E::SEEK:
      flush();
      nextBaseMs = std::get<int>(ev->data);
      break;
    case E::VOLUME:
      remoteVol = (std::get<int>(ev->data) * 100 + 32767) / 65535;
      break;
    case E::DISC:  // ein anderes Gerät hat übernommen
      paused = true;
      audio::musicPause(true);
      flush();
      xSemaphoreTake(mtx, portMAX_DELAY);
      st.active = false;
      xSemaphoreGive(mtx);
      break;
    case E::DEPLETED:
      paused = true;
      break;
    default:
      break;
  }
}

// ------------------------------------------------------------------ Verbindung

class Runner : public bell::Task {
 public:
  Runner() : bell::Task("spotify", 32 * 1024, 1, 1) { startTask(); }

  void runTask() override {
    for (;;) {
      while (!haveBlob || !netOnline() || !configured()) delay(500);
      relogin = false;
      setMsg("Verbinde mit Spotify …");
      auto c = cspot::Context::createFromBlob(blob);
      c->config.clientId = keys::spotifyId();
      c->config.clientSecret = keys::spotifySecret();
      c->config.volume = cfg.volume * 65535 / 100;
      try {
        c->session->connectWithRandomAp();
        auto token = c->session->authenticate(blob);
        if (token.empty()) {
          Serial.println("Spotify: Anmeldung abgelehnt");
          setMsg("Spotify-Anmeldung abgelehnt");
          if (fromZeroconf) { haveBlob = false; fromZeroconf = false; }  // neu koppeln
          delay(15000);
          continue;
        }
        rememberAccount(c->config.username, c->getCredentialsJson());
        snprintf(userName, sizeof(userName), "%s", c->config.username.c_str());
        c->session->startTask();
        auto h = std::make_shared<cspot::SpircHandler>(c);
        h->subscribeToMercury();
        h->getTrackPlayer()->setDataCallback(feed);
        h->setEventHandler(onEvent);
        ctx = c;
        handler = h;
        isConnected = true;
        relogin = false;
        setMsg("");
        Serial.printf("Spotify: verbunden als %s\n", userName);
        while (!relogin) c->session->handlePacket();
        // anderes Konto: Sitzung sauber beenden, dann mit den neuen Zugangsdaten von vorn
        Serial.println("Spotify: Konto wird gewechselt");
        isConnected = false;
        handler = nullptr;
        h->disconnect();
        audio::musicPause(true);
        flush();
        xSemaphoreTake(mtx, portMAX_DELAY);
        st.active = false;
        xSemaphoreGive(mtx);
        ctx = nullptr;
        continue;
      } catch (const std::exception& e) {
        Serial.printf("Spotify: Fehler %s\n", e.what());
        setMsg("Spotify nicht erreichbar");
      }
      isConnected = false;
      delay(10000);
    }
  }
};
static Runner* runner = nullptr;

// Zeroconf: die Spotify-App findet die Uhr per mDNS und übergibt die Zugangsdaten
static void zcInfo() { zc->send(200, "application/json", blob->buildZeroconfInfo().c_str()); }

static void zcAdd() {
  std::map<std::string, std::string> q;
  for (int i = 0; i < zc->args(); i++) q[zc->argName(i).c_str()] = zc->arg(i).c_str();
  if (q.count("action") && q["action"] == "getInfo") { zcInfo(); return; }
  try {
    blob->loadZeroconfQuery(q);
  } catch (const std::exception& e) {
    // meist ein veralteter Schlüssel in der App: Fehler melden, die App fragt dann neu nach
    Serial.printf("Spotify: Kopplung fehlgeschlagen (%s)\n", e.what());
    zc->send(200, "application/json", "{\"status\":105,\"spotifyError\":1,\"statusString\":\"ERROR-LOGIN-FAILED\"}");
    return;
  }
  fromZeroconf = true;
  haveBlob = true;
  if (isConnected) relogin = true;  // neues Konto aus der App: umschalten
  zc->send(200, "application/json", "{\"status\":101,\"spotifyError\":0,\"statusString\":\"ERROR-OK\"}");
  Serial.println("Spotify: von der App gekoppelt");
}

void poll() {
  if (!netOnline()) return;
  if (!zc) {
    zc = new WebServer(ZC_PORT);
    zc->on("/spotify_info", HTTP_GET, zcInfo);
    zc->on("/spotify_info", HTTP_POST, zcAdd);
    zc->begin();
  }
  // erst wenn mDNS läuft (startet in netPoll), sonst geht die Anmeldung still verloren
  if (!mdnsDone && netMdnsReady() && MDNS.addService("spotify-connect", "tcp", ZC_PORT)) {
    MDNS.addServiceTxt("spotify-connect", "tcp", "VERSION", "1.0");
    MDNS.addServiceTxt("spotify-connect", "tcp", "CPath", "/spotify_info");
    MDNS.addServiceTxt("spotify-connect", "tcp", "Stack", "SP");
    mdnsDone = true;
  }
  zc->handleClient();
  if (accDirty) {
    accDirty = false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    saveAccounts();
    xSemaphoreGive(mtx);
  }
  // neuer Titel ist im Lautsprecher angekommen → an Spotify melden (Titelinfo, Position)
  auto h = handler;
  if (pending && h && audio::musicPlayed() >= pendingAt) {
    pending = false;
    trackStartPlayed = pendingAt;
    trackBaseMs = nextBaseMs;
    nextBaseMs = 0;
    h->notifyAudioReachedPlayback(pendingId);
  }
}

void begin() {
  mtx = xSemaphoreCreateMutex();
  bell::setDefaultLogger();
  try {
    bell::X509Bundle::init(bellBundleStart, bellBundleEnd - bellBundleStart);
  } catch (const std::exception& e) {
    Serial.printf("Spotify: Zertifikate nicht lesbar (%s), HTTPS ohne Prüfung\n", e.what());
  }
  blob = std::make_shared<cspot::LoginBlob>(cfg.name);
  // Schlüssel der Kopplung über Neustarts behalten: die Spotify-App merkt sich den öffentlichen Teil
  {
    Preferences p;
    p.begin("spot", false);
    std::vector<uint8_t> k(96);
    if (p.getBytesLength("dh") == k.size() && p.getBytes("dh", k.data(), k.size()) == k.size()) blob->setDhKey(k);
    else { k = blob->dhKey(); p.putBytes("dh", k.data(), k.size()); }
    p.end();
  }
  loadAccounts();
  if (accCur >= 0) {
    blob->loadJson(accs[accCur].cred.c_str());
    snprintf(userName, sizeof(userName), "%s", accs[accCur].user.c_str());
    haveBlob = true;
    Serial.printf("Spotify: gespeicherte Anmeldung für %s\n", userName);
  }
  if (!configured()) Serial.println("Spotify: Client-ID/Secret fehlen (Weboberfläche oder include/secrets.h)");
  runner = new Runner();
}

// ------------------------------------------------------------------ Steuerung

bool play(const std::vector<std::string>& uris, int index, const std::string& context) {
  if (!handler || !isConnected) {
    setMsg(!linked() ? "In der Spotify-App die Uhr wählen" : "Spotify nicht verbunden");
    return false;
  }
  flush();
  handler->loadLocal(uris, index, context);
  xSemaphoreTake(mtx, portMAX_DELAY);
  st.active = true;
  st.src = music::SRC_SPOTIFY;
  st.msg[0] = 0;
  xSemaphoreGive(mtx);
  return true;
}

void toggle() { if (handler) handler->setPause(!paused); }
void next() { if (handler && handler->nextSong()) flush(); }
void prev() { if (handler && handler->previousSong()) flush(); }
void stop() {
  if (handler) handler->setPause(true);
  flush();
  xSemaphoreTake(mtx, portMAX_DELAY);
  st.active = false;
  xSemaphoreGive(mtx);
}
void volumeChanged(int pct) { if (handler && isConnected) handler->setRemoteVolume(pct * 65535 / 100); }

// ------------------------------------------------------------------ Web-API (Katalog)

static String token;
static uint32_t tokenUntil = 0;

void keysChanged() {
  token = "";
  tokenUntil = 0;
}

static bool webToken() {
  if (token.length() && millis() < tokenUntil) return true;
  String out;
  String body = String("grant_type=client_credentials&client_id=") + keys::spotifyId() + "&client_secret=" + keys::spotifySecret();
  if (web::post("https://accounts.spotify.com/api/token", body, "application/x-www-form-urlencoded", out) != 200) return false;
  JsonDocument d;
  if (deserializeJson(d, out)) return false;
  token = d["access_token"] | "";
  tokenUntil = millis() + ((d["expires_in"] | 3600) - 60) * 1000UL;
  return token.length() > 0;
}

static int api(const String& path, JsonDocument& doc, const JsonDocument* filter = nullptr) {
  if (!webToken()) return -2;
  String body;
  String auth = "Authorization: Bearer " + token;
  int code = web::get("https://api.spotify.com/v1/" + path, body, auth.c_str(), 1024 * 1024);
  if (code != 200) return code;
  DeserializationError e = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter)) : deserializeJson(doc, body);
  return e ? -3 : 200;
}

static void ucopy(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  size_t len = strlen(src);
  if (len < n) { memcpy(dst, src, len + 1); return; }
  size_t cut = n - 1;
  while (cut > 0 && ((uint8_t)src[cut] & 0xC0) == 0x80) cut--;
  memcpy(dst, src, cut);
  dst[cut] = 0;
}

static String artists(JsonArrayConst a) {
  String s;
  for (JsonObjectConst x : a) {
    if (s.length()) s += ", ";
    s += (const char*)(x["name"] | "");
  }
  return s;
}

static void addTrack(JsonObjectConst t, music::Item* items, int max, int* n, const char* sub = nullptr) {
  if (*n >= max || !t["uri"].is<const char*>()) return;
  music::Item& o = items[(*n)++];
  o.kind = music::K_TRACK;
  o.dur = (t["duration_ms"] | 0) / 1000;
  ucopy(o.id, sizeof(o.id), t["uri"]);
  ucopy(o.title, sizeof(o.title), t["name"] | "");
  ucopy(o.sub, sizeof(o.sub), sub ? sub : artists(t["artists"]).c_str());
}

static void addList(const char* id, const char* title, const char* sub, music::Item* items, int max, int* n) {
  if (*n >= max) return;
  music::Item& o = items[(*n)++];
  o.kind = music::K_LIST;
  o.dur = 0;
  ucopy(o.id, sizeof(o.id), id);
  ucopy(o.title, sizeof(o.title), title);
  ucopy(o.sub, sizeof(o.sub), sub);
}

bool browse(const char* path, char* title, size_t titleLen, music::Item* items, int max, int* count, char* err,
            size_t errLen) {
  *count = 0;
  err[0] = 0;
  if (!configured()) { ucopy(err, errLen, "Spotify-Schlüssel fehlen (Weboberfläche)"); return false; }
  JsonDocument doc;
  int code;
  char buf[160];
  if (!strncmp(path, "search:", 7)) {
    String q = web::urlencode(path + 7);
    code = api("search?type=track,artist,album,playlist&limit=8&market=DE&q=" + q, doc);
    if (code != 200) goto fail;
    snprintf(title, titleLen, "„%s“", path + 7);
    for (JsonObjectConst t : doc["tracks"]["items"].as<JsonArrayConst>()) addTrack(t, items, max, count);
    int k = 0;
    for (JsonObjectConst a : doc["artists"]["items"].as<JsonArrayConst>()) {
      if (k++ == 3 || a.isNull()) break;
      snprintf(buf, sizeof(buf), "sp:artist:%s", (const char*)(a["id"] | ""));
      addList(buf, a["name"] | "", "Interpret", items, max, count);
    }
    for (JsonObjectConst a : doc["albums"]["items"].as<JsonArrayConst>()) {
      if (a.isNull()) continue;
      snprintf(buf, sizeof(buf), "sp:album:%s", (const char*)(a["id"] | ""));
      String sub = artists(a["artists"]) + " · " + String((const char*)(a["release_date"] | "")).substring(0, 4);
      addList(buf, a["name"] | "", sub.c_str(), items, max, count);
    }
    for (JsonObjectConst a : doc["playlists"]["items"].as<JsonArrayConst>()) {
      if (a.isNull()) continue;  // Spotify liefert hier teils null-Einträge
      snprintf(buf, sizeof(buf), "sp:playlist:%s", (const char*)(a["id"] | ""));
      String sub = String("Playlist · ") + (const char*)(a["owner"]["display_name"] | "");
      addList(buf, a["name"] | "", sub.c_str(), items, max, count);
    }
    return true;
  }
  if (!strncmp(path, "sp:album:", 9)) {
    code = api(String("albums/") + (path + 9) + "?market=DE", doc);
    if (code != 200) goto fail;
    ucopy(title, titleLen, doc["name"] | "Album");
    String sub = artists(doc["artists"]);
    for (JsonObjectConst t : doc["tracks"]["items"].as<JsonArrayConst>()) addTrack(t, items, max, count, sub.c_str());
    return true;
  }
  if (!strncmp(path, "sp:playlist:", 12)) {
    JsonDocument filter;
    filter["name"] = true;
    filter["tracks"]["items"][0]["track"]["uri"] = true;
    filter["tracks"]["items"][0]["track"]["name"] = true;
    filter["tracks"]["items"][0]["track"]["duration_ms"] = true;
    filter["tracks"]["items"][0]["track"]["artists"][0]["name"] = true;
    code = api(String("playlists/") + (path + 12) + "?market=DE", doc, &filter);
    if (code == 404) { ucopy(err, errLen, "Diese Playlist gibt Spotify nicht frei"); return false; }
    if (code != 200) goto fail;
    ucopy(title, titleLen, doc["name"] | "Playlist");
    for (JsonObjectConst it : doc["tracks"]["items"].as<JsonArrayConst>()) addTrack(it["track"], items, max, count);
    return true;
  }
  if (!strncmp(path, "sp:artist:", 10)) {
    const char* id = path + 10;
    code = api(String("artists/") + id + "/top-tracks?market=DE", doc);
    if (code != 200) goto fail;
    {
      JsonDocument a;
      if (api(String("artists/") + id, a) == 200) ucopy(title, titleLen, a["name"] | "Interpret");
    }
    for (JsonObjectConst t : doc["tracks"].as<JsonArrayConst>()) addTrack(t, items, max, count);
    {
      JsonDocument al;
      if (api(String("artists/") + id + "/albums?include_groups=album,single&market=DE&limit=12", al) == 200)
        for (JsonObjectConst a : al["items"].as<JsonArrayConst>()) {
          snprintf(buf, sizeof(buf), "sp:album:%s", (const char*)(a["id"] | ""));
          String sub = String((const char*)(a["release_date"] | "")).substring(0, 4);
          addList(buf, a["name"] | "", sub.c_str(), items, max, count);
        }
    }
    return true;
  }
  ucopy(err, errLen, "Unbekannte Liste");
  return false;
fail:
  snprintf(err, errLen, code == -2 ? "Spotify-Anmeldung der App fehlgeschlagen" : code < 0 ? "Spotify nicht erreichbar"
                                                                                  : "Spotify: Fehler %d", code);
  return false;
}

}  // namespace spotify
