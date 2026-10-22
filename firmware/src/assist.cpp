#include "assist.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <mbedtls/base64.h>
#include <time.h>
#include "audio.h"
#include "clockwork.h"
#include "config.h"
#include "keys.h"
#include "kids.h"
#include "n8n.h"
#include "net.h"
#include "settings.h"
#include "weather.h"
#include "web.h"
#include "wsock.h"

namespace assist {

static const char* HOST = "generativelanguage.googleapis.com";


// ArduinoJson im PSRAM (Antworten mit Audio sind groß)
struct SpiAlloc : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
static SpiAlloc spi;

static volatile Phase ph = OFF;
static volatile bool wantOpen = false, down = false;
static SemaphoreHandle_t mtx;
static String saidTxt, heardTxt, statusTxt;
static String cfgName = "KIra", cfgModel = "gemini-3.8-live", cfgVoice = "Kore", cfgStt = GEMINI_MODEL;
static bool cfgSearch = true;

static void setText(String& dst, const String& v) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  dst = v;
  xSemaphoreGive(mtx);
}
static String getText(const String& src) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  String s = src;
  xSemaphoreGive(mtx);
  return s;
}

bool available() { return !kids::on() && keys::gemini()[0]; }
static void task(void*);
static TaskHandle_t taskH = nullptr;
void open() {
  if (!available()) return;
  wantOpen = true;
  if (!taskH) xTaskCreatePinnedToCore(task, "assist", 16384, nullptr, 3, &taskH, 1);
}
void close() { wantOpen = false; down = false; }
void talk(bool d) { if (d) open(); down = d; }
Phase phase() { return ph; }
float level() { return ph == LISTENING ? audio::micLevel() : audio::outLevel(); }
String said() { return getText(saidTxt); }
String heard() { return getText(heardTxt); }
String status() { return getText(statusTxt); }
String name() { return cfgName; }
bool search() { return cfgSearch; }
String model() { return cfgModel; }
String voice() { return cfgVoice; }
String sttModel() { return cfgStt; }

void configure(const char* n, int s, const char* m, const char* v, const char* stt) {
  Preferences p;
  p.begin("assist", false);
  if (n && n[0]) { cfgName = n; p.putString("name", cfgName); }
  if (s >= 0) { cfgSearch = s; p.putBool("search", cfgSearch); }
  if (m && m[0]) { cfgModel = m; p.putString("model", cfgModel); }
  if (v && v[0]) { cfgVoice = v; p.putString("voice", cfgVoice); }
  if (stt && stt[0]) { cfgStt = stt; p.putString("stt", cfgStt); }
  p.end();
}

String models(String& err) {
  JsonDocument filter;
  filter["models"][0]["name"] = true;
  filter["models"][0]["displayName"] = true;
  filter["models"][0]["supportedGenerationMethods"] = true;
  filter["nextPageToken"] = true;
  JsonDocument out;
  JsonArray live = out["live"].to<JsonArray>(), text = out["text"].to<JsonArray>();
  String page;
  for (int i = 0; i < 4; i++) {
    String body;
    String url = "https://generativelanguage.googleapis.com/v1beta/models?pageSize=100" + (page.length() ? "&pageToken=" + page : String());
    String hdr = String("x-goog-api-key: ") + keys::gemini();
    int code = web::get(url, body, hdr.c_str());
    if (code != 200) { err = "Gemini antwortet nicht (" + String(code) + ")"; return ""; }
    JsonDocument d(&spi);
    if (deserializeJson(d, body, DeserializationOption::Filter(filter))) { err = "Antwort unlesbar"; return ""; }
    for (JsonObject m : d["models"].as<JsonArray>()) {
      String n = m["name"] | "";
      n.replace("models/", "");
      bool bidi = false, gen = false;
      for (const char* g : m["supportedGenerationMethods"].as<JsonArray>()) {
        if (!strcmp(g, "bidiGenerateContent")) bidi = true;
        if (!strcmp(g, "generateContent")) gen = true;
      }
      // Live: Sprachdialog (ohne reine Übersetzer/Transkribierer); Spracheingabe: schnelle Text-Modelle
      if (bidi && n.indexOf("translate") < 0 && n.indexOf("transcribe") < 0 && n.indexOf("robotics") < 0) live.add(n);
      else if (gen && n.indexOf("flash") >= 0 && n.indexOf("tts") < 0 && n.indexOf("image") < 0 && n.indexOf("live") < 0)
        text.add(n);
    }
    page = d["nextPageToken"] | "";
    if (!page.length()) break;
  }
  String r;
  serializeJson(out, r);
  return r;
}

// ------------------------------------------------------------------ Sitzung einrichten

static String setupJson() {
  JsonDocument d(&spi);
  JsonObject s = d["setup"].to<JsonObject>();
  s["model"] = "models/" + cfgModel;
  JsonObject g = s["generationConfig"].to<JsonObject>();
  g["responseModalities"].add("AUDIO");
  g["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"] = cfgVoice;
  g["speechConfig"]["languageCode"] = "de-DE";
  s["realtimeInputConfig"]["automaticActivityDetection"]["disabled"] = true;  // Push-to-Talk
  s["outputAudioTranscription"].to<JsonObject>();
  s["inputAudioTranscription"].to<JsonObject>();

  // Lage: Datum, Uhrzeit, Ort
  char when[64] = "unbekannt";
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    static const char* WD[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
    snprintf(when, sizeof(when), "%s, %d.%d.%d, %02d:%02d Uhr", WD[t.tm_wday], t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min);
  }
  std::vector<n8n::Entry> flows = n8n::configured() ? n8n::allowed() : std::vector<n8n::Entry>();
  String sys = "Du bist " + cfgName +
               ", die Sprachassistentin von KI/sruptiv auf einer kleinen Uhr. Du sprichst Deutsch, natürlich und "
               "knapp: meist ein bis drei Sätze, keine Aufzählungen, keine Formatierung, denn alles wird vorgelesen. "
               "Jetzt ist " + String(when) + ". Ort: " + (weather::city()[0] ? weather::city() : "unbekannt") + ". ";
  if (cfgSearch) sys += "Für aktuelle Fakten, Nachrichten, Sport und Wetter nutze die Google-Suche. ";
  sys += "Du kannst Timer und den Wecker der Uhr stellen. ";
  if (flows.size()) {
    sys += "Du kannst außerdem diese n8n-Workflows und -Agenten der Firma benutzen (Name – Art – Beschreibung – ID): ";
    for (auto& f : flows) sys += f.name + " – " + f.kind + " – " + f.desc + " – " + f.id + "; ";
    sys += "Workflows startest du mit n8n_workflow_starten, Agenten fragst du mit n8n_agent_fragen. Bevor du etwas "
           "startest, das Nachrichten verschickt, Daten ändert oder etwas bestellt, nenne kurz, was du tun wirst, und "
           "warte auf ein Ja. Fasse Ergebnisse kurz und gesprochen zusammen. ";
  }
  s["systemInstruction"]["parts"][0]["text"] = sys;

  JsonArray tools = s["tools"].to<JsonArray>();
  if (cfgSearch) tools.add<JsonObject>()["googleSearch"].to<JsonObject>();
  JsonArray fns = tools.add<JsonObject>()["functionDeclarations"].to<JsonArray>();
  auto fn = [&](const char* n, const char* desc) {
    JsonObject f = fns.add<JsonObject>();
    f["name"] = n;
    f["description"] = desc;
    f["parameters"]["type"] = "OBJECT";
    return f;
  };
  auto param = [](JsonObject f, const char* n, const char* type, const char* desc) {
    f["parameters"]["properties"][n]["type"] = type;
    f["parameters"]["properties"][n]["description"] = desc;
    f["parameters"]["required"].add(n);
  };
  JsonObject t = fn("timer_stellen", "Stellt den Timer der Uhr.");
  param(t, "sekunden", "INTEGER", "Dauer in Sekunden");
  JsonObject w = fn("wecker_stellen", "Stellt den Wecker der Uhr und schaltet ihn ein.");
  param(w, "stunde", "INTEGER", "Stunde 0–23");
  param(w, "minute", "INTEGER", "Minute 0–59");
  if (flows.size()) {
    JsonObject a = fn("n8n_workflow_starten", "Startet einen freigegebenen n8n-Workflow und liefert sein Ergebnis.");
    param(a, "id", "STRING", "ID des Workflows");
    param(a, "eingabe", "STRING", "Text, der an den Workflow geht (Frage, Auftrag), sonst leer");
    JsonObject b = fn("n8n_agent_fragen", "Schickt eine Nachricht an einen freigegebenen n8n-Agenten und liefert seine Antwort.");
    param(b, "id", "STRING", "ID des Agenten");
    param(b, "nachricht", "STRING", "Die Nachricht an den Agenten");
  }
  String out;
  serializeJson(d, out);
  return out;
}

// ------------------------------------------------------------------ Werkzeuge

static String agentSession;

static String runTool(const char* name, JsonObjectConst args) {
  if (!strcmp(name, "timer_stellen")) {
    int s = args["sekunden"] | 0;
    if (s <= 0) return "Ungültige Dauer";
    cw::timerStart(s);
    return "Timer läuft";
  }
  if (!strcmp(name, "wecker_stellen")) {
    int h = args["stunde"] | -1, m = args["minute"] | 0;
    if (h < 0 || h > 23 || m < 0 || m > 59) return "Ungültige Uhrzeit";
    cfg.alarmH = h;
    cfg.alarmM = m;
    cfg.alarmOn = true;
    settingsSave();
    return "Wecker gestellt";
  }
  String err, r;
  if (!strcmp(name, "n8n_workflow_starten")) {
    setText(statusTxt, "n8n arbeitet …");
    r = n8n::runWorkflow(args["id"] | "", args["eingabe"] | "", err);
  } else if (!strcmp(name, "n8n_agent_fragen")) {
    setText(statusTxt, "Agent denkt nach …");
    r = n8n::askAgent(args["id"] | "", args["nachricht"] | "", agentSession, err);
  } else {
    return "Unbekanntes Werkzeug";
  }
  setText(statusTxt, "");
  return err.length() ? "Fehler: " + err : r;
}

// ------------------------------------------------------------------ Audio

// 24 kHz → 16 kHz (Sprachkanal), linear
static float rsPhase = 0;
static int16_t rsPrev = 0;

static void playPcm24(const uint8_t* raw, size_t bytes) {
  static int16_t out[1024];
  const int16_t* in = (const int16_t*)raw;
  size_t n = bytes / 2, o = 0;
  for (size_t i = 0; i < n; i++) {
    int16_t x = in[i];
    while (rsPhase < 1) {
      out[o++] = rsPrev + (x - rsPrev) * rsPhase;
      rsPhase += 1.5f;  // 24/16
      if (o == sizeof(out) / 2) {
        while (audio::voiceSpace() < o && wantOpen) vTaskDelay(pdMS_TO_TICKS(10));
        audio::voiceWrite(out, o);
        o = 0;
      }
    }
    rsPhase -= 1;
    rsPrev = x;
  }
  if (o) {
    while (audio::voiceSpace() < o && wantOpen) vTaskDelay(pdMS_TO_TICKS(10));
    audio::voiceWrite(out, o);
  }
}

// ------------------------------------------------------------------ Nachrichten der Gegenseite

static bool turnDone = false;

static void handle(wsock::Client& ws, const char* msg, size_t len) {
  JsonDocument d(&spi);
  if (deserializeJson(d, msg, len)) return;
  if (d["toolCall"]) {
    ph = TOOL;
    JsonDocument r(&spi);
    JsonArray fr = r["toolResponse"]["functionResponses"].to<JsonArray>();
    for (JsonObjectConst c : d["toolCall"]["functionCalls"].as<JsonArrayConst>()) {
      const char* n = c["name"] | "";
      Serial.printf("Assistent: Werkzeug %s\n", n);
      String res = runTool(n, c["args"]);
      JsonObject o = fr.add<JsonObject>();
      o["name"] = n;
      if (c["id"]) o["id"] = c["id"];
      o["response"]["result"] = res;
    }
    String out;
    serializeJson(r, out);
    ws.sendText(out);
    ph = THINKING;
    return;
  }
  JsonObjectConst sc = d["serverContent"];
  if (!sc) return;
  if (sc["interrupted"] | false) audio::voiceFlush();
  for (JsonObjectConst p : sc["modelTurn"]["parts"].as<JsonArrayConst>()) {
    const char* b64 = p["inlineData"]["data"] | (const char*)nullptr;
    if (!b64) continue;
    size_t bl = strlen(b64), outLen = 0;
    uint8_t* pcm = (uint8_t*)heap_caps_malloc(bl * 3 / 4 + 4, MALLOC_CAP_SPIRAM);
    if (!pcm) continue;
    if (!mbedtls_base64_decode(pcm, bl * 3 / 4 + 4, &outLen, (const uint8_t*)b64, bl)) {
      ph = SPEAKING;
      playPcm24(pcm, outLen);
    }
    heap_caps_free(pcm);
  }
  if (sc["outputTranscription"]["text"]) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (turnDone) { saidTxt = ""; turnDone = false; }
    saidTxt += (const char*)(sc["outputTranscription"]["text"] | "");
    if (saidTxt.length() > 600) saidTxt = saidTxt.substring(saidTxt.length() - 600);
    xSemaphoreGive(mtx);
  }
  if (sc["inputTranscription"]["text"]) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    heardTxt += (const char*)(sc["inputTranscription"]["text"] | "");
    xSemaphoreGive(mtx);
  }
  if (sc["turnComplete"] | false) turnDone = true;
}

// ------------------------------------------------------------------ Task

static void sendAudio(wsock::Client& ws, const int16_t* s, size_t n) {
  static unsigned char* b64 = (unsigned char*)heap_caps_malloc(4400, MALLOC_CAP_SPIRAM);
  size_t ol = 0;
  mbedtls_base64_encode(b64, 4400, &ol, (const unsigned char*)s, n * 2);
  String m;
  m.reserve(ol + 96);
  m = "{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"";
  m.concat((const char*)b64, ol);
  m += "\"}}}";
  ws.sendText(m);
}

// Der Task lebt nur, solange die Assistentin offen ist – sein Stack (16 KB, intern) ist sonst frei

static void task(void*) {
  static int16_t* mic = (int16_t*)heap_caps_malloc(1600 * 2, MALLOC_CAP_SPIRAM);  // 100 ms
  for (;;) {
    if (!wantOpen) { taskH = nullptr; vTaskDelete(nullptr); }
    if (!netOnline()) { setText(statusTxt, "Kein WLAN"); ph = FAILED; vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    ph = CONNECTING;
    setText(statusTxt, "");
    wsock::Client* ws = new wsock::Client();
    String path = String("/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=") + keys::gemini();
    bool ok = ws->connect(HOST, path) && ws->sendText(setupJson());
    const char* msg;
    int n = 0;
    uint32_t t0 = millis();
    while (ok && (n = ws->recv(msg, 200)) >= 0 && millis() - t0 < 10000)
      if (n > 0 && strstr(msg, "setupComplete")) break;
    if (!ok || n <= 0) {
      Serial.println("Assistent: Verbindung fehlgeschlagen");
      setText(statusTxt, "Keine Verbindung zu Gemini");
      ph = FAILED;
      delete ws;
      for (int i = 0; i < 50 && wantOpen && !down; i++) vTaskDelay(pdMS_TO_TICKS(100));  // nicht im Kreis versuchen
      if (!down) wantOpen = false;
      continue;
    }
    Serial.println("Assistent: verbunden");
    ph = READY;
    bool talking = false;
    size_t have = 0;
    uint32_t idleAt = millis();
    agentSession = "";
    while (wantOpen && ws->connected()) {
      if (down && !talking) {  // Taste gedrückt: Frage beginnt (unterbricht eine laufende Antwort)
        audio::voiceFlush();
        ws->sendText("{\"realtimeInput\":{\"activityStart\":{}}}");
        audio::micStart();
        talking = true;
        have = 0;
        turnDone = false;
        setText(heardTxt, "");
        setText(saidTxt, "");
        ph = LISTENING;
      }
      if (talking) {
        have += audio::micRead(mic + have, 1600 - have);
        if (have == 1600) { sendAudio(*ws, mic, have); have = 0; }
        if (!down) {
          if (have) sendAudio(*ws, mic, have);
          ws->sendText("{\"realtimeInput\":{\"activityEnd\":{}}}");
          audio::micStop();
          talking = false;
          ph = THINKING;
        }
        idleAt = millis();
      }
      n = ws->recv(msg, talking ? 2 : 20);
      if (n < 0) break;
      if (n > 0) { handle(*ws, msg, n); idleAt = millis(); }
      if (ph == SPEAKING && turnDone && !audio::voiceUsed()) ph = READY;
      if (ph == THINKING && turnDone && !audio::voiceUsed()) ph = READY;
      if (millis() - idleAt > 180000 && !talking) break;  // 3 Minuten nichts: Sitzung schließen
    }
    if (talking) audio::micStop();
    ws->close();
    delete ws;
    Serial.println("Assistent: getrennt");
    ph = OFF;
    if (!down) wantOpen = false;
  }
}

void begin() {
  mtx = xSemaphoreCreateMutex();
  Preferences p;
  p.begin("assist", true);
  cfgName = p.getString("name", "KIra");
  cfgSearch = p.getBool("search", true);
  cfgModel = p.getString("model", cfgModel);
  cfgVoice = p.getString("voice", cfgVoice);
  cfgStt = p.getString("stt", cfgStt);
  p.end();
  n8n::begin();
}

}  // namespace assist
