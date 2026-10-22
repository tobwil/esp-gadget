#include "voice.h"
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include "audio.h"
#include "config.h"
#include "web.h"
#include "keys.h"

namespace voice {

static const int RATE = 16000, MAX_S = 8;
static int16_t* rec = nullptr;
static volatile int recN = 0;
static volatile Phase ph = IDLE;
static volatile bool wantFinish = false;
static char result[96];

bool configured() { return keys::gemini()[0] != 0; }
Phase phase() { return ph; }
const char* text() { return result; }
float level() { return audio::micLevel(); }

void start() {
  if (ph == LISTENING || ph == THINKING) return;
  if (!configured()) { snprintf(result, sizeof(result), "Gemini-Schlüssel fehlt (Weboberfläche)"); ph = FAILED; return; }
  recN = 0;
  wantFinish = false;
  result[0] = 0;
  audio::micStart();
  ph = LISTENING;
}

void finish() { if (ph == LISTENING) wantFinish = true; }

void cancel() {
  if (ph == LISTENING) audio::micStop();
  ph = IDLE;
}

static void putLE(String& s, uint32_t v, int n) { for (int i = 0; i < n; i++) s += (char)((v >> (8 * i)) & 0xFF); }

static bool transcribe() {
  // WAV-Kopf + PCM, dann Base64 für den JSON-Aufruf
  size_t pcmBytes = recN * 2;
  String wav;
  wav.reserve(44 + pcmBytes);
  wav += "RIFF"; putLE(wav, 36 + pcmBytes, 4); wav += "WAVEfmt ";
  putLE(wav, 16, 4); putLE(wav, 1, 2); putLE(wav, 1, 2); putLE(wav, RATE, 4); putLE(wav, RATE * 2, 4);
  putLE(wav, 2, 2); putLE(wav, 16, 2); wav += "data"; putLE(wav, pcmBytes, 4);
  wav.concat((const char*)rec, pcmBytes);
  size_t b64len = 0;
  mbedtls_base64_encode(nullptr, 0, &b64len, (const uint8_t*)wav.c_str(), wav.length());
  char* b64 = (char*)heap_caps_malloc(b64len + 1, MALLOC_CAP_SPIRAM);
  if (!b64) return false;
  mbedtls_base64_encode((uint8_t*)b64, b64len + 1, &b64len, (const uint8_t*)wav.c_str(), wav.length());
  b64[b64len] = 0;
  wav = String();

  String body;
  body.reserve(b64len + 600);
  body += "{\"contents\":[{\"parts\":[{\"text\":\"";
  body += "Das ist eine gesprochene Suchanfrage für Musik oder Podcasts (meist Deutsch, Namen oft englisch). "
          "Gib nur den Suchbegriff zurück: Titel, Interpret, Album oder Podcastname, korrekt geschrieben, ohne "
          "Anführungszeichen und ohne Satzzeichen am Ende. Lass Füllwörter wie 'spiel' oder 'such' weg. "
          "Wenn nichts verständlich ist, antworte mit einem leeren Text.";
  body += "\"},{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"";
  body += b64;
  body += "\"}}]}],\"generationConfig\":{\"temperature\":0,\"maxOutputTokens\":40}}";
  free(b64);

  String out;
  String url = String("https://generativelanguage.googleapis.com/v1beta/models/") + GEMINI_MODEL + ":generateContent";
  String key = String("x-goog-api-key: ") + keys::gemini();
  int code = web::post(url, body, "application/json", out, key.c_str());
  if (code != 200) {
    Serial.printf("Sprache: Gemini-Fehler %d %s\n", code, out.substring(0, 200).c_str());
    snprintf(result, sizeof(result), code < 0 ? "Keine Verbindung" : "Spracherkennung: Fehler %d", code);
    return false;
  }
  JsonDocument d;
  if (deserializeJson(d, out)) { snprintf(result, sizeof(result), "Antwort unlesbar"); return false; }
  String t = d["candidates"][0]["content"]["parts"][0]["text"] | "";
  t.trim();
  t.replace("\"", "");
  while (t.endsWith(".") || t.endsWith("!") || t.endsWith("?")) t.remove(t.length() - 1);
  if (!t.length()) { snprintf(result, sizeof(result), "Nicht verstanden"); return false; }
  snprintf(result, sizeof(result), "%s", t.c_str());
  Serial.printf("Sprache: „%s“\n", result);
  return true;
}

static void task(void*) {
  for (;;) {
    if (ph != LISTENING) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }
    int n = audio::micRead(rec + recN, RATE * MAX_S - recN);
    recN = recN + n;
    if (recN >= RATE * MAX_S) wantFinish = true;
    if (!wantFinish) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    audio::micStop();
    if (recN < RATE * 4 / 10) { snprintf(result, sizeof(result), "Zu kurz – Knopf beim Sprechen halten"); ph = FAILED; continue; }
    ph = THINKING;
    ph = transcribe() ? DONE : FAILED;
  }
}

void begin() {
  rec = (int16_t*)heap_caps_malloc(RATE * MAX_S * 2, MALLOC_CAP_SPIRAM);
  xTaskCreatePinnedToCore(task, "voice", 12288, nullptr, 3, nullptr, 1);  // HTTPS zu Gemini braucht Stack
}

}  // namespace voice
